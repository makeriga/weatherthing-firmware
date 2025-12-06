#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include "weather.h"
#include "settings.h"

static float g_lat = 56.9496f;  // Default: Riga, Latvia
static float g_lon = 24.1052f;
static WeatherData g_current = {0, WEATHER_SUNNY, false};
static ForecastSlot g_forecast[12];
static uint32_t g_lastFetch = 0;
static const uint32_t FETCH_INTERVAL = 600000; // 10 minutes
static bool g_simulating = false;  // True when simulating weather (don't fetch)
static uint32_t g_simStartMs = 0;  // When simulation started

static Preferences g_weatherPrefs;

static WeatherType codeToType(int code)
{
    // WMO weather codes: https://open-meteo.com/en/docs
    // Complete mapping for accurate weather representation
    
    switch (code) {
        // Clear conditions
        case 0:  return WEATHER_SUNNY;         // Clear sky
        case 1:  return WEATHER_SUNNY;         // Mainly clear
        
        // Partly cloudy
        case 2:  return WEATHER_PARTLY_CLOUDY; // Partly cloudy
        
        // Overcast/cloudy
        case 3:  return WEATHER_CLOUDY;        // Overcast
        
        // Fog conditions
        case 45: return WEATHER_FOG;           // Fog
        case 48: return WEATHER_FOG;           // Depositing rime fog
        
        // Drizzle - light precipitation
        case 51: return WEATHER_DRIZZLE;       // Light drizzle
        case 53: return WEATHER_DRIZZLE;       // Moderate drizzle
        case 55: return WEATHER_DRIZZLE;       // Dense drizzle
        
        // Freezing drizzle
        case 56: return WEATHER_SLEET;         // Light freezing drizzle
        case 57: return WEATHER_SLEET;         // Dense freezing drizzle
        
        // Rain
        case 61: return WEATHER_RAIN;          // Slight rain
        case 63: return WEATHER_RAIN;          // Moderate rain
        case 65: return WEATHER_HEAVY_RAIN;    // Heavy rain
        
        // Freezing rain
        case 66: return WEATHER_SLEET;         // Light freezing rain
        case 67: return WEATHER_SLEET;         // Heavy freezing rain
        
        // Snow fall
        case 71: return WEATHER_SNOW;          // Slight snow
        case 73: return WEATHER_SNOW;          // Moderate snow
        case 75: return WEATHER_SNOW;          // Heavy snow
        case 77: return WEATHER_SNOW;          // Snow grains
        
        // Rain showers
        case 80: return WEATHER_RAIN;          // Slight rain showers
        case 81: return WEATHER_RAIN;          // Moderate rain showers
        case 82: return WEATHER_HEAVY_RAIN;    // Violent rain showers
        
        // Snow showers
        case 85: return WEATHER_SNOW;          // Slight snow showers
        case 86: return WEATHER_SNOW;          // Heavy snow showers
        
        // Thunderstorms
        case 95: return WEATHER_STORM;         // Thunderstorm (slight/moderate)
        case 96: return WEATHER_STORM;         // Thunderstorm with slight hail
        case 99: return WEATHER_STORM;         // Thunderstorm with heavy hail
        
        default:
            // Unknown codes - default to cloudy
            Serial.printf("Unknown weather code: %d\n", code);
            return WEATHER_CLOUDY;
    }
}

const char* weather_type_name(WeatherType type)
{
    static const char* names[] = {
        "Sunny", "Partly Cloudy", "Cloudy", "Fog", "Drizzle",
        "Rain", "Heavy Rain", "Storm", "Snow", "Sleet", "Wind",
        "Clear Night"
    };
    if (type < WEATHER_TYPE_COUNT) return names[type];
    return "Unknown";
}

static void loadLocation()
{
    if (g_weatherPrefs.begin("wtweather", true))
    {
        g_lat = g_weatherPrefs.getFloat("lat", 56.9496f);
        g_lon = g_weatherPrefs.getFloat("lon", 24.1052f);
        g_weatherPrefs.end();
    }
}

static void saveLocation()
{
    if (g_weatherPrefs.begin("wtweather", false))
    {
        g_weatherPrefs.putFloat("lat", g_lat);
        g_weatherPrefs.putFloat("lon", g_lon);
        g_weatherPrefs.end();
    }
}

void weather_begin()
{
    loadLocation();
    
    // Initialize forecast with dummy data
    for (int i = 0; i < 12; ++i)
    {
        g_forecast[i].temp = 15;
        g_forecast[i].type = WEATHER_CLOUDY;
    }
}

void weather_update()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        return;
    }

    // Check simulation timeout
    if (g_simulating) {
        extern Settings& settings_get();
        uint16_t timeout = settings_get().simTimeoutSecs;
        if (timeout > 0) {
            uint32_t elapsed = (millis() - g_simStartMs) / 1000;
            if (elapsed >= timeout) {
                g_simulating = false;
                g_lastFetch = 0;  // Force refresh
                Serial.println("Simulation timeout - returning to real weather");
            }
        }
        if (g_simulating) {
            return;  // Still simulating
        }
    }
    
    uint32_t now = millis();
    if (g_current.valid && (now - g_lastFetch) < FETCH_INTERVAL)
    {
        return;
    }
    g_lastFetch = now;

    HTTPClient http;
    
    // Build Open-Meteo URL with current + hourly forecast
    String url = "http://api.open-meteo.com/v1/forecast?";
    url += "latitude=" + String(g_lat, 4);
    url += "&longitude=" + String(g_lon, 4);
    url += "&current=temperature_2m,weather_code";
    url += "&hourly=temperature_2m,weather_code";
    url += "&forecast_hours=12";
    url += "&timezone=auto";

    Serial.print("Weather fetch: ");
    Serial.println(url);

    http.begin(url);
    http.setTimeout(10000);
    int code = http.GET();

    if (code == 200)
    {
        String payload = http.getString();
        
        // Simple JSON parsing (avoid heavy ArduinoJson dependency)
        // Look for "current":{"temperature_2m": and "weather_code":
        int idx = payload.indexOf("\"current\"");
        if (idx >= 0)
        {
            int tempIdx = payload.indexOf("\"temperature_2m\":", idx);
            if (tempIdx >= 0)
            {
                int start = tempIdx + 17;
                int end = payload.indexOf(",", start);
                if (end < 0) end = payload.indexOf("}", start);
                String tempStr = payload.substring(start, end);
                g_current.temp = (int8_t)tempStr.toFloat();
            }

            int codeIdx = payload.indexOf("\"weather_code\":", idx);
            if (codeIdx >= 0)
            {
                int start = codeIdx + 15;
                int end = payload.indexOf(",", start);
                if (end < 0) end = payload.indexOf("}", start);
                String codeStr = payload.substring(start, end);
                g_current.type = codeToType(codeStr.toInt());
            }
            
            g_current.valid = true;
            Serial.print("Weather: ");
            Serial.print(g_current.temp);
            Serial.print("C, type=");
            Serial.println(g_current.type);
        }

        // Parse hourly forecast
        int hourlyIdx = payload.indexOf("\"hourly\"");
        if (hourlyIdx >= 0)
        {
            // Find temperature_2m array
            int tempArrIdx = payload.indexOf("\"temperature_2m\":[", hourlyIdx);
            if (tempArrIdx >= 0)
            {
                int arrStart = tempArrIdx + 18;
                int arrEnd = payload.indexOf("]", arrStart);
                String tempArr = payload.substring(arrStart, arrEnd);
                
                int pos = 0;
                for (int i = 0; i < 12 && pos >= 0; ++i)
                {
                    int comma = tempArr.indexOf(",", pos);
                    String val = (comma >= 0) ? tempArr.substring(pos, comma) : tempArr.substring(pos);
                    g_forecast[i].temp = (int8_t)val.toFloat();
                    pos = (comma >= 0) ? comma + 1 : -1;
                }
            }

            // Find weather_code array
            int codeArrIdx = payload.indexOf("\"weather_code\":[", hourlyIdx);
            if (codeArrIdx >= 0)
            {
                int arrStart = codeArrIdx + 16;
                int arrEnd = payload.indexOf("]", arrStart);
                String codeArr = payload.substring(arrStart, arrEnd);
                
                int pos = 0;
                for (int i = 0; i < 12 && pos >= 0; ++i)
                {
                    int comma = codeArr.indexOf(",", pos);
                    String val = (comma >= 0) ? codeArr.substring(pos, comma) : codeArr.substring(pos);
                    g_forecast[i].type = codeToType(val.toInt());
                    pos = (comma >= 0) ? comma + 1 : -1;
                }
            }
        }
    }
    else
    {
        Serial.print("Weather fetch failed: ");
        Serial.println(code);
    }

    http.end();
}

WeatherData weather_get_current()
{
    return g_current;
}

ForecastSlot weather_get_forecast(uint8_t slot)
{
    if (slot >= 12)
    {
        slot = 11;
    }
    return g_forecast[slot];
}

void weather_set_location(float lat, float lon)
{
    g_lat = lat;
    g_lon = lon;
    g_simulating = false;  // Stop simulation when location changes
    g_current.valid = false; // Force refresh
    g_lastFetch = 0;
    saveLocation();
}

void weather_get_location(float* lat, float* lon)
{
    if (lat) *lat = g_lat;
    if (lon) *lon = g_lon;
}

void weather_simulate(uint8_t type, int8_t temp)
{
    g_simulating = true;  // Lock out weather fetching
    g_simStartMs = millis();  // Track when simulation started
    g_current.type = (WeatherType)type;
    g_current.temp = temp;
    g_current.valid = true;
    
    // Also update forecast for demo
    for (int i = 0; i < 12; ++i)
    {
        g_forecast[i].type = (WeatherType)((type + i / 3) % 6);
        g_forecast[i].temp = temp + (i - 6);
    }
    
    Serial.print("Weather simulated: type=");
    Serial.print(type);
    Serial.print(" temp=");
    Serial.println(temp);
}

void weather_stop_simulation()
{
    g_simulating = false;
    g_lastFetch = 0;  // Force refresh on next update
    Serial.println("Weather simulation stopped");
}

bool weather_set_city(const char* city)
{
    if (WiFi.status() != WL_CONNECTED || !city || strlen(city) < 2)
    {
        return false;
    }
    
    HTTPClient http;
    
    // Use Open-Meteo geocoding API
    String url = "http://geocoding-api.open-meteo.com/v1/search?name=";
    url += city;
    url += "&count=1&language=en&format=json";
    
    Serial.print("Geocoding: ");
    Serial.println(url);
    
    http.begin(url);
    http.setTimeout(10000);
    int code = http.GET();
    
    bool success = false;
    
    if (code == 200)
    {
        String payload = http.getString();
        
        // Parse: {"results":[{"latitude":51.5074,"longitude":-0.1278,...}]}
        int latIdx = payload.indexOf("\"latitude\":");
        int lonIdx = payload.indexOf("\"longitude\":");
        
        if (latIdx >= 0 && lonIdx >= 0)
        {
            int latStart = latIdx + 11;
            int latEnd = payload.indexOf(",", latStart);
            float lat = payload.substring(latStart, latEnd).toFloat();
            
            int lonStart = lonIdx + 12;
            int lonEnd = payload.indexOf(",", lonStart);
            if (lonEnd < 0) lonEnd = payload.indexOf("}", lonStart);
            float lon = payload.substring(lonStart, lonEnd).toFloat();
            
            if (lat != 0 || lon != 0)
            {
                weather_set_location(lat, lon);
                Serial.print("City found: ");
                Serial.print(lat, 4);
                Serial.print(", ");
                Serial.println(lon, 4);
                success = true;
            }
        }
    }
    
    http.end();
    return success;
}
