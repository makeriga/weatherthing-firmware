#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include "weather.h"
#include "settings.h"
#include "http_worker.h"

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static WeatherType codeToType(int code);
static String url_encode(const String& in);

static float g_lat = 56.9496f;  // Default: Riga, Latvia
static float g_lon = 24.1052f;
static WeatherData g_current = {0, WEATHER_SUNNY, false};
static ForecastSlot g_forecast[12];
static uint32_t g_lastAttempt = 0;
static uint32_t g_lastSuccess = 0;
static const uint32_t FETCH_INTERVAL = 600000; // 10 minutes
static const uint32_t RETRY_INTERVAL = 30000; // 30 seconds
static const uint32_t STALE_THRESHOLD = 1800000; // 30 minutes - data considered stale
static bool g_simulating = false;  // True when simulating weather (don't fetch)
static uint32_t g_simStartMs = 0;  // When simulation started
static bool g_isFetching = false;  // True while actively fetching data
static volatile bool g_weatherFetchQueued = false;

static Preferences g_weatherPrefs;

static portMUX_TYPE g_weatherMux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE g_mapMux = portMUX_INITIALIZER_UNLOCKED;

static WeatherType symbolToType(const String& symbol)
{
    String s = symbol;
    s.toLowerCase();

    if (s.indexOf("thunder") >= 0) return WEATHER_STORM;
    if (s.indexOf("snow") >= 0) return WEATHER_SNOW;
    if (s.indexOf("sleet") >= 0 || s.indexOf("freezingrain") >= 0) return WEATHER_SLEET;
    if (s.indexOf("heavyrain") >= 0) return WEATHER_HEAVY_RAIN;
    if (s.indexOf("rain") >= 0 || s.indexOf("drizzle") >= 0) return WEATHER_RAIN;
    if (s.indexOf("fog") >= 0) return WEATHER_FOG;
    if (s.indexOf("partlycloudy") >= 0) return WEATHER_PARTLY_CLOUDY;
    if (s.indexOf("cloudy") >= 0) return WEATHER_CLOUDY;
    if (s.indexOf("clearsky") >= 0 || s.indexOf("fair") >= 0) {
        if (s.indexOf("night") >= 0) return WEATHER_CLEAR_NIGHT;
        return WEATHER_SUNNY;
    }

    return WEATHER_CLOUDY;
}

// Minimal URL encoder for query parameters
static String url_encode(const String& in)
{
    String out;
    const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < in.length(); ++i)
    {
        unsigned char c = (unsigned char)in[i];
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
        {
            out += (char)c;
        }
        else if (c == ' ')
        {
            out += "%20";
        }
        else
        {
            out += '%';
            out += hex[(c >> 4) & 0xF];
            out += hex[c & 0xF];
        }
    }
    return out;
}

static bool fetch_open_meteo(WeatherData* outCurrent, ForecastSlot outForecast[12])
{
    if (!outCurrent || !outForecast) return false;

    HTTPClient http;

    String url = "http://api.open-meteo.com/v1/forecast?";
    url += "latitude=" + String(g_lat, 4);
    url += "&longitude=" + String(g_lon, 4);
    url += "&current=temperature_2m,weather_code";
    url += "&hourly=temperature_2m,weather_code";
    url += "&forecast_hours=12";
    url += "&timezone=auto";

    Serial.print("Weather fetch (Open-Meteo): ");
    Serial.println(url);

    http.begin(url);
    http.setConnectTimeout(3000);
    http.setTimeout(5000);
    int code = http.GET();

    if (code != 200)
    {
        Serial.print("Open-Meteo fetch failed: ");
        Serial.println(code);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    WeatherData current = {0, WEATHER_CLOUDY, false};
    ForecastSlot forecast[12];
    for (int i = 0; i < 12; ++i) {
        forecast[i].temp = 0;
        forecast[i].type = WEATHER_CLOUDY;
    }

    bool gotTemp = false;
    bool gotCode = false;
    int idx = payload.indexOf("\"current\"");
    if (idx >= 0)
    {
        int tempIdx = payload.indexOf("\"temperature_2m\":", idx);
        if (tempIdx >= 0)
        {
            int start = tempIdx + 17;
            int end = payload.indexOf(",", start);
            if (end < 0) end = payload.indexOf("}", start);
            if (end > start) {
                String tempStr = payload.substring(start, end);
                current.temp = (int8_t)tempStr.toFloat();
                gotTemp = true;
            }
        }

        int codeIdx = payload.indexOf("\"weather_code\":", idx);
        if (codeIdx >= 0)
        {
            int start = codeIdx + 15;
            int end = payload.indexOf(",", start);
            if (end < 0) end = payload.indexOf("}", start);
            if (end > start) {
                String codeStr = payload.substring(start, end);
                current.type = codeToType(codeStr.toInt());
                gotCode = true;
            }
        }
    }

    int hourlyIdx = payload.indexOf("\"hourly\"");
    bool gotForecastTemps = false;
    bool gotForecastCodes = false;
    if (hourlyIdx >= 0)
    {
        int tempArrIdx = payload.indexOf("\"temperature_2m\":[", hourlyIdx);
        if (tempArrIdx >= 0)
        {
            int arrStart = tempArrIdx + 18;
            int arrEnd = payload.indexOf("]", arrStart);
            if (arrEnd > arrStart) {
                String tempArr = payload.substring(arrStart, arrEnd);
                int pos = 0;
                int count = 0;
                for (int i = 0; i < 12 && pos >= 0; ++i)
                {
                    int comma = tempArr.indexOf(",", pos);
                    String val = (comma >= 0) ? tempArr.substring(pos, comma) : tempArr.substring(pos);
                    forecast[i].temp = (int8_t)val.toFloat();
                    pos = (comma >= 0) ? comma + 1 : -1;
                    count++;
                }
                gotForecastTemps = (count == 12);
            }
        }

        int codeArrIdx = payload.indexOf("\"weather_code\":[", hourlyIdx);
        if (codeArrIdx >= 0)
        {
            int arrStart = codeArrIdx + 16;
            int arrEnd = payload.indexOf("]", arrStart);
            if (arrEnd > arrStart) {
                String codeArr = payload.substring(arrStart, arrEnd);
                int pos = 0;
                int count = 0;
                for (int i = 0; i < 12 && pos >= 0; ++i)
                {
                    int comma = codeArr.indexOf(",", pos);
                    String val = (comma >= 0) ? codeArr.substring(pos, comma) : codeArr.substring(pos);
                    forecast[i].type = codeToType(val.toInt());
                    pos = (comma >= 0) ? comma + 1 : -1;
                    count++;
                }
                gotForecastCodes = (count == 12);
            }
        }
    }

    if (!gotTemp || !gotCode || !gotForecastTemps || !gotForecastCodes)
    {
        Serial.println("Open-Meteo parse failed (missing fields)");
        return false;
    }

    current.valid = true;
    *outCurrent = current;
    for (int i = 0; i < 12; ++i) outForecast[i] = forecast[i];
    return true;
}

static bool fetch_met_no(WeatherData* outCurrent, ForecastSlot outForecast[12])
{
    if (!outCurrent || !outForecast) return false;

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    String url = "https://api.met.no/weatherapi/locationforecast/2.0/compact?lat=";
    url += String(g_lat, 4);
    url += "&lon=";
    url += String(g_lon, 4);

    Serial.print("Weather fetch (MET Norway): ");
    Serial.println(url);

    if (!http.begin(client, url)) {
        Serial.println("MET Norway begin() failed");
        return false;
    }
    http.setTimeout(15000);
    http.addHeader("User-Agent", "WeatherThing/1.0 (+https://github.com/makeriga/weatherthing-firmware)");
    http.addHeader("Accept", "application/json");

    int code = http.GET();
    if (code != 200)
    {
        Serial.print("MET Norway fetch failed: ");
        Serial.println(code);
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    if (!stream) {
        http.end();
        Serial.println("MET Norway stream unavailable");
        return false;
    }

    ForecastSlot forecast[12];
    WeatherData current = {0, WEATHER_CLOUDY, false};
    for (int i = 0; i < 12; ++i) {
        forecast[i].temp = 0;
        forecast[i].type = WEATHER_CLOUDY;
    }

    String buf;
    buf.reserve(1024);
    uint32_t startMs = millis();

    auto pump = [&]() -> bool {
        char tmp[257];
        int avail = stream->available();
        if (avail <= 0) {
            return false;
        }
        int toRead = min(avail, 256);
        int n = (int)stream->readBytes(tmp, (size_t)toRead);
        if (n > 0) {
            tmp[n] = '\0';
            buf += tmp;
            if (buf.length() > 2048) {
                buf.remove(0, buf.length() - 2048);
            }
            return true;
        }
        return false;
    };

    bool haveTimeseries = false;
    while (!haveTimeseries && (millis() - startMs) < 15000) {
        int idx = buf.indexOf("\"timeseries\"");
        if (idx >= 0) {
            buf.remove(0, idx);
            haveTimeseries = true;
            break;
        }
        if (!pump()) {
            if (!http.connected()) break;
            delay(1);
        }
    }

    if (!haveTimeseries) {
        http.end();
        Serial.println("MET Norway parse failed (no timeseries)");
        return false;
    }

    bool ok = true;
    for (int i = 0; i < 12; ++i)
    {
        int tIdx = -1;
        int tStart = -1;
        int tEnd = -1;
        while ((millis() - startMs) < 15000) {
            tIdx = buf.indexOf("\"air_temperature\":");
            if (tIdx >= 0) {
                tStart = tIdx + 18;
                tEnd = buf.indexOf(",", tStart);
                if (tEnd < 0) tEnd = buf.indexOf("}", tStart);
                if (tEnd > tStart) break;
            }
            if (!pump()) {
                if (!http.connected()) break;
                delay(1);
            }
        }
        if (tIdx < 0 || tEnd <= tStart) { ok = false; break; }

        float tVal = buf.substring(tStart, tEnd).toFloat();

        int sIdx = -1;
        int sStart = -1;
        int sEnd = -1;
        while ((millis() - startMs) < 15000) {
            sIdx = buf.indexOf("\"symbol_code\":\"", tEnd);
            if (sIdx >= 0) {
                sStart = sIdx + 15;
                sEnd = buf.indexOf("\"", sStart);
                if (sEnd > sStart) break;
            }
            if (!pump()) {
                if (!http.connected()) break;
                delay(1);
            }
        }
        if (sIdx < 0 || sEnd <= sStart) { ok = false; break; }

        String sym = buf.substring(sStart, sEnd);

        forecast[i].temp = (int8_t)tVal;
        forecast[i].type = symbolToType(sym);

        if (i == 0) {
            current.temp = forecast[i].temp;
            current.type = forecast[i].type;
        }

        buf.remove(0, sEnd);
    }

    http.end();

    if (!ok) {
        Serial.println("MET Norway parse failed (missing fields)");
        return false;
    }

    current.valid = true;
    *outCurrent = current;
    for (int i = 0; i < 12; ++i) outForecast[i] = forecast[i];
    return true;
}

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
                g_lastAttempt = 0;
                g_lastSuccess = 0;  // Force refresh
                Serial.println("Simulation timeout - returning to real weather");
            }
        }
        if (g_simulating) {
            return;  // Still simulating
        }
    }
    
    uint32_t now = millis();
    bool shouldFetch = false;

    portENTER_CRITICAL(&g_weatherMux);
    if (!g_weatherFetchQueued) {
        if (!(g_current.valid && (now - g_lastSuccess) < FETCH_INTERVAL)) {
            if (!((now - g_lastAttempt) < RETRY_INTERVAL)) {
                g_lastAttempt = now;
                g_isFetching = true;
                g_weatherFetchQueued = true;
                shouldFetch = true;
            }
        }
    }
    portEXIT_CRITICAL(&g_weatherMux);

    if (!shouldFetch) {
        return;
    }

    auto job = [](void*) {
        WeatherData nextCurrent;
        ForecastSlot nextForecast[12];

        Settings& cfg = settings_get();
        uint8_t provider = cfg.weatherProvider;
        if (provider > 2) provider = 0;

        bool usedFallback = false;
        bool ok = false;
        if (provider == 1) {
            ok = fetch_open_meteo(&nextCurrent, nextForecast);
        } else if (provider == 2) {
            ok = fetch_met_no(&nextCurrent, nextForecast);
        } else {
            ok = fetch_open_meteo(&nextCurrent, nextForecast);
            if (!ok) {
                usedFallback = true;
                ok = fetch_met_no(&nextCurrent, nextForecast);
            }
        }

        uint32_t now = millis();

        portENTER_CRITICAL(&g_weatherMux);
        g_isFetching = false;
        g_weatherFetchQueued = false;

        if (ok)
        {
            g_current = nextCurrent;
            for (int i = 0; i < 12; ++i) g_forecast[i] = nextForecast[i];
            g_lastSuccess = now;
        }
        portEXIT_CRITICAL(&g_weatherMux);

        if (ok)
        {
            if (provider == 2) {
                Serial.println("Weather provider: MET Norway");
            } else if (provider == 1) {
                Serial.println("Weather provider: Open-Meteo");
            } else if (usedFallback) {
                Serial.println("Weather provider: MET Norway (fallback)");
            } else {
                Serial.println("Weather provider: Open-Meteo");
            }
            Serial.print("Weather updated: ");
            Serial.print(nextCurrent.temp);
            Serial.print("C, type=");
            Serial.println(nextCurrent.type);
        }
    };

    if (!http_worker_enqueue(job, nullptr)) {
        portENTER_CRITICAL(&g_weatherMux);
        g_isFetching = false;
        g_weatherFetchQueued = false;
        portEXIT_CRITICAL(&g_weatherMux);
    }
}

WeatherData weather_get_current()
{
    WeatherData out;
    portENTER_CRITICAL(&g_weatherMux);
    out = g_current;
    portEXIT_CRITICAL(&g_weatherMux);
    return out;
}

ForecastSlot weather_get_forecast(uint8_t slot)
{
    if (slot >= 12)
    {
        slot = 11;
    }
    ForecastSlot out;
    portENTER_CRITICAL(&g_weatherMux);
    out = g_forecast[slot];
    portEXIT_CRITICAL(&g_weatherMux);
    return out;
}

void weather_set_location(float lat, float lon)
{
    g_lat = lat;
    g_lon = lon;
    g_simulating = false;  // Stop simulation when location changes
    g_current.valid = false; // Force refresh
    g_lastAttempt = 0;
    g_lastSuccess = 0;
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
    g_lastAttempt = 0;
    g_lastSuccess = 0;  // Force refresh on next update
    Serial.println("Weather simulation stopped");
}

bool weather_is_fetching()
{
    bool out;
    portENTER_CRITICAL(&g_weatherMux);
    out = g_isFetching;
    portEXIT_CRITICAL(&g_weatherMux);
    return out;
}

bool weather_is_stale()
{
    if (!g_current.valid) return true;
    if (g_lastSuccess == 0) return true;
    return (millis() - g_lastSuccess) > STALE_THRESHOLD;
}

uint32_t weather_data_age_secs()
{
    if (g_lastSuccess == 0) return 0;
    return (millis() - g_lastSuccess) / 1000;
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
    url += url_encode(String(city));
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
        int resultsIdx = payload.indexOf("\"results\"");
        int latIdx = resultsIdx >= 0 ? payload.indexOf("\"latitude\":", resultsIdx) : -1;
        int lonIdx = resultsIdx >= 0 ? payload.indexOf("\"longitude\":", resultsIdx) : -1;
        
        if (latIdx >= 0 && lonIdx >= 0)
        {
            int latStart = latIdx + 11;
            int latEnd = payload.indexOf(",", latStart);
            if (latEnd < 0) latEnd = payload.indexOf("}", latStart);
            float lat = (latEnd > latStart) ? payload.substring(latStart, latEnd).toFloat() : 0.0f;
            
            int lonStart = lonIdx + 12;
            int lonEnd = payload.indexOf(",", lonStart);
            if (lonEnd < 0) lonEnd = payload.indexOf("}", lonStart);
            float lon = (lonEnd > lonStart) ? payload.substring(lonStart, lonEnd).toFloat() : 0.0f;
            
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

// ============== MAP PRESETS IMPLEMENTATION ==============

static RadarMapData g_radarMap = {{{0}}, 0, false};
static GridMapData g_gridMap = {{{0}}, {{0}}, 0, false};
static uint32_t g_lastRadarFetch = 0;
static uint32_t g_lastGridFetch = 0;
static const uint32_t MAP_FETCH_INTERVAL = 300000; // 5 minutes
static const uint32_t MAP_RETRY_INTERVAL = 60000;  // 60 seconds
static uint32_t g_lastRadarAttempt = 0;
static uint32_t g_lastGridAttempt = 0;
static volatile bool g_radarFetchQueued = false;
static volatile bool g_gridFetchQueued = false;

// RainViewer radar host and path (updated from API)
static char g_rainviewerHost[64] = "";
static char g_rainviewerPath[64] = "";

bool weather_fetch_radar_map()
{
    if (WiFi.status() != WL_CONNECTED) return false;
    
    uint32_t now = millis();
    if (g_radarMap.valid && (now - g_lastRadarFetch) < MAP_FETCH_INTERVAL) {
        return true; // Still fresh
    }
    
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    
    // Step 1: Get RainViewer index to find latest radar timestamp
    http.begin(client, "https://api.rainviewer.com/public/weather-maps.json");
    http.setTimeout(10000);
    int code = http.GET();
    
    if (code != 200) {
        Serial.print("RainViewer index failed: ");
        Serial.println(code);
        http.end();
        return false;
    }
    
    String indexJson = http.getString();
    http.end();
    
    // Parse host and latest radar path
    // Format: {"host":"https://tilecache.rainviewer.com","radar":{"past":[...],"nowcast":[...]},"satellite":{...}}
    int hostIdx = indexJson.indexOf("\"host\":\"");
    if (hostIdx < 0) {
        Serial.println("RainViewer: no host");
        return false;
    }
    int hostStart = hostIdx + 8;
    int hostEnd = indexJson.indexOf("\"", hostStart);
    String host = indexJson.substring(hostStart, hostEnd);
    strncpy(g_rainviewerHost, host.c_str(), sizeof(g_rainviewerHost) - 1);
    
    // Find "radar" section first (not satellite!)
    int radarIdx = indexJson.indexOf("\"radar\":");
    if (radarIdx < 0) {
        Serial.println("RainViewer: no radar section");
        return false;
    }
    
    // Find "past" array within radar section
    int pastIdx = indexJson.indexOf("\"past\":[", radarIdx);
    if (pastIdx < 0) {
        Serial.println("RainViewer: no radar past data");
        return false;
    }
    
    // Find the end of the past array to limit our search
    int pastEnd = indexJson.indexOf("]", pastIdx);
    if (pastEnd < 0) pastEnd = indexJson.length();
    
    // Find the last "path" WITHIN the radar past array only
    String pastSection = indexJson.substring(pastIdx, pastEnd);
    int lastPathIdx = pastSection.lastIndexOf("\"path\":\"");
    if (lastPathIdx < 0) {
        Serial.println("RainViewer: no radar path");
        return false;
    }
    int pathStart = lastPathIdx + 8;
    int pathEnd = pastSection.indexOf("\"", pathStart);
    String path = pastSection.substring(pathStart, pathEnd);
    strncpy(g_rainviewerPath, path.c_str(), sizeof(g_rainviewerPath) - 1);
    
    Serial.print("RainViewer radar path: ");
    Serial.println(path);
    
    // Step 2: Fetch radar tile centered on user location
    // RainViewer tile URL format: {host}{path}/256/{z}/{x}/{y}/{color}/{options}.png
    // z = zoom level, x/y = tile coordinates (not lat/lon!)
    // Use zoom from settings (2-6, lower = wider area)
    Settings& cfg = settings_get();
    int zoom = cfg.mapZoom;
    if (zoom < 2) zoom = 2;
    if (zoom > 6) zoom = 6;
    float latRad = g_lat * PI / 180.0f;
    int tileX = (int)((g_lon + 180.0f) / 360.0f * (1 << zoom));
    int tileY = (int)((1.0f - logf(tanf(latRad) + 1.0f / cosf(latRad)) / PI) / 2.0f * (1 << zoom));
    
    // Color scheme: 2 = original colors, 6 = NOAA, 7 = infrared
    // Options: 1_1 = smooth + snow
    String tileUrl = host + path + "/256/" + String(zoom) + "/" + String(tileX) + "/" + String(tileY) + "/2/1_1.png";
    
    Serial.print("Fetching radar tile: ");
    Serial.println(tileUrl);
    
    http.begin(client, tileUrl);
    http.setTimeout(15000);
    code = http.GET();
    
    if (code != 200) {
        Serial.print("Radar tile failed: ");
        Serial.println(code);
        http.end();
        return false;
    }
    
    // Get PNG data
    int len = http.getSize();
    if (len < 100 || len > 100000) {
        Serial.print("Invalid tile size: ");
        Serial.println(len);
        http.end();
        return false;
    }
    
    // Read PNG into buffer
    uint8_t* pngData = (uint8_t*)malloc(len);
    if (!pngData) {
        Serial.println("Radar: malloc failed");
        http.end();
        return false;
    }
    
    WiFiClient* stream = http.getStreamPtr();
    int bytesRead = 0;
    while (bytesRead < len && stream->connected()) {
        int avail = stream->available();
        if (avail > 0) {
            int toRead = min(avail, len - bytesRead);
            stream->readBytes(pngData + bytesRead, toRead);
            bytesRead += toRead;
        }
        yield();
    }
    http.end();
    
    if (bytesRead < len) {
        Serial.println("Radar: incomplete read");
        free(pngData);
        return false;
    }
    
    // Simple PNG parsing - find IDAT chunk and extract raw pixel intensities
    // RainViewer tiles are 256x256 indexed PNG with precipitation values
    // We'll sample 20x7 points from the center region
    
    // For simplicity, we'll scan for non-zero bytes in the IDAT region
    // and map them to our 20x7 grid. This is a simplified approach.
    
    uint8_t nextIntensity[20][7];
    memset(nextIntensity, 0, sizeof(nextIntensity));
    
    // Find IDAT chunk (contains compressed image data)
    int idatPos = -1;
    for (int i = 0; i < len - 8; i++) {
        if (pngData[i] == 'I' && pngData[i+1] == 'D' && pngData[i+2] == 'A' && pngData[i+3] == 'T') {
            idatPos = i + 4;
            break;
        }
    }
    
    if (idatPos > 0) {
        // Sample the compressed data to get a rough intensity map
        // This is a hack - proper PNG decoding would be better but memory-intensive
        int dataLen = len - idatPos - 12; // Approximate IDAT data length
        if (dataLen > 0) {
            // Sample 140 points (20x7) from the data
            int stride = dataLen / 140;
            if (stride < 1) stride = 1;
            
            for (int x = 0; x < 20; x++) {
                for (int y = 0; y < 7; y++) {
                    int idx = idatPos + ((y * 20 + x) * stride) % dataLen;
                    if (idx < len) {
                        // Use raw byte value as intensity hint
                        uint8_t val = pngData[idx];
                        // RainViewer uses color palette where higher values = more rain
                        nextIntensity[x][y] = val;
                    }
                }
            }
        }
    }
    
    free(pngData);
    
    portENTER_CRITICAL(&g_mapMux);
    memcpy(g_radarMap.intensity, nextIntensity, sizeof(nextIntensity));
    g_radarMap.timestamp = now;
    g_radarMap.valid = true;
    g_lastRadarFetch = now;
    portEXIT_CRITICAL(&g_mapMux);
    
    Serial.println("Radar map updated");
    return true;
}

bool weather_fetch_grid_map()
{
    if (WiFi.status() != WL_CONNECTED) return false;
    
    uint32_t now = millis();
    if (g_gridMap.valid && (now - g_lastGridFetch) < MAP_FETCH_INTERVAL) {
        return true; // Still fresh
    }
    
    // Generate bounding box around user location
    // ±0.3° lat (~33km), ±0.6° lon (varies by latitude, ~40km at mid-latitudes)
    float latMin = g_lat - 0.3f;
    float latMax = g_lat + 0.3f;
    float lonMin = g_lon - 0.6f;
    float lonMax = g_lon + 0.6f;
    
    // Build comma-separated coordinate lists for 20x7 = 140 points
    String latList = "";
    String lonList = "";
    latList.reserve(1800);
    lonList.reserve(1800);
    
    for (int y = 0; y < 7; y++) {
        float lat = latMax - (y * (latMax - latMin) / 6.0f);
        for (int x = 0; x < 20; x++) {
            float lon = lonMin + (x * (lonMax - lonMin) / 19.0f);
            if (latList.length() > 0) {
                latList += ",";
                lonList += ",";
            }
            latList += String(lat, 3);
            lonList += String(lon, 3);
        }
    }
    
    // Fetch current weather for all 140 points
    // Open-Meteo allows up to 1000 coordinates per request
    HTTPClient http;
    String url = "http://api.open-meteo.com/v1/forecast?";
    url += "latitude=" + latList;
    url += "&longitude=" + lonList;
    url += "&current=cloud_cover,precipitation";
    
    Serial.println("Fetching grid map (140 points)...");
    
    http.begin(url);
    http.setTimeout(30000); // Longer timeout for large request
    int code = http.GET();
    
    if (code != 200) {
        Serial.print("Grid map failed: ");
        Serial.println(code);
        http.end();
        return false;
    }
    
    String payload = http.getString();
    http.end();
    
    // Parse response - format is array of current values
    // Look for "cloud_cover" and "precipitation" arrays
    
    uint8_t nextCloud[20][7];
    uint8_t nextPrecip[20][7];
    memset(nextCloud, 0, sizeof(nextCloud));
    memset(nextPrecip, 0, sizeof(nextPrecip));
    
    // Find cloud_cover values
    int cloudIdx = payload.indexOf("\"cloud_cover\":");
    int precipIdx = payload.indexOf("\"precipitation\":");
    
    // Parse as individual current objects (one per coordinate)
    // Format: [...{"current":{"cloud_cover":50,"precipitation":0.1}}...]
    
    int searchPos = 0;
    int pointIdx = 0;
    
    while (pointIdx < 140 && searchPos < (int)payload.length()) {
        int currentIdx = payload.indexOf("\"current\":", searchPos);
        if (currentIdx < 0) break;
        
        // Find cloud_cover value
        int ccIdx = payload.indexOf("\"cloud_cover\":", currentIdx);
        if (ccIdx > 0 && ccIdx < currentIdx + 200) {
            int valStart = ccIdx + 14;
            int valEnd = payload.indexOf(",", valStart);
            if (valEnd < 0) valEnd = payload.indexOf("}", valStart);
            if (valEnd > valStart) {
                int cc = payload.substring(valStart, valEnd).toInt();
                int x = pointIdx % 20;
                int y = pointIdx / 20;
                nextCloud[x][y] = (uint8_t)constrain(cc, 0, 100);
            }
        }
        
        // Find precipitation value
        int prIdx = payload.indexOf("\"precipitation\":", currentIdx);
        if (prIdx > 0 && prIdx < currentIdx + 200) {
            int valStart = prIdx + 16;
            int valEnd = payload.indexOf(",", valStart);
            if (valEnd < 0) valEnd = payload.indexOf("}", valStart);
            if (valEnd > valStart) {
                float pr = payload.substring(valStart, valEnd).toFloat();
                int x = pointIdx % 20;
                int y = pointIdx / 20;
                // Scale precipitation (0-10mm/h) to 0-255
                nextPrecip[x][y] = (uint8_t)constrain((int)(pr * 25.5f), 0, 255);
            }
        }
        
        searchPos = currentIdx + 50;
        pointIdx++;
    }
    
    portENTER_CRITICAL(&g_mapMux);
    memcpy(g_gridMap.cloud, nextCloud, sizeof(nextCloud));
    memcpy(g_gridMap.precip, nextPrecip, sizeof(nextPrecip));
    g_gridMap.timestamp = now;
    g_gridMap.valid = (pointIdx > 0);
    g_lastGridFetch = now;
    portEXIT_CRITICAL(&g_mapMux);
    
    Serial.print("Grid map updated: ");
    Serial.print(pointIdx);
    Serial.println(" points");
    
    return g_gridMap.valid;
}

const RadarMapData& weather_get_radar_map()
{
    return g_radarMap;
}

const GridMapData& weather_get_grid_map()
{
    return g_gridMap;
}

void weather_get_radar_map_copy(RadarMapData* out)
{
    if (!out) return;
    portENTER_CRITICAL(&g_mapMux);
    *out = g_radarMap;
    portEXIT_CRITICAL(&g_mapMux);
}

void weather_get_grid_map_copy(GridMapData* out)
{
    if (!out) return;
    portENTER_CRITICAL(&g_mapMux);
    *out = g_gridMap;
    portEXIT_CRITICAL(&g_mapMux);
}

bool weather_request_radar_map()
{
    if (WiFi.status() != WL_CONNECTED) return false;
    uint32_t now = millis();

    portENTER_CRITICAL(&g_mapMux);
    bool fresh = g_radarMap.valid && (now - g_lastRadarFetch) < MAP_FETCH_INTERVAL;
    bool queued = g_radarFetchQueued;
    bool retryOk = (now - g_lastRadarAttempt) >= MAP_RETRY_INTERVAL || g_lastRadarAttempt == 0;
    if (!fresh && !queued && retryOk) {
        g_lastRadarAttempt = now;
        g_radarFetchQueued = true;
    }
    bool willQueue = (!fresh && !queued && retryOk);
    portEXIT_CRITICAL(&g_mapMux);

    if (fresh || queued) return true;
    if (!willQueue) return false;

    auto job = [](void*) {
        weather_fetch_radar_map();
        portENTER_CRITICAL(&g_mapMux);
        g_radarFetchQueued = false;
        portEXIT_CRITICAL(&g_mapMux);
    };

    if (!http_worker_enqueue(job, nullptr)) {
        portENTER_CRITICAL(&g_mapMux);
        g_radarFetchQueued = false;
        portEXIT_CRITICAL(&g_mapMux);
        return false;
    }
    return true;
}

bool weather_request_grid_map()
{
    if (WiFi.status() != WL_CONNECTED) return false;
    uint32_t now = millis();

    portENTER_CRITICAL(&g_mapMux);
    bool fresh = g_gridMap.valid && (now - g_lastGridFetch) < MAP_FETCH_INTERVAL;
    bool queued = g_gridFetchQueued;
    bool retryOk = (now - g_lastGridAttempt) >= MAP_RETRY_INTERVAL || g_lastGridAttempt == 0;
    if (!fresh && !queued && retryOk) {
        g_lastGridAttempt = now;
        g_gridFetchQueued = true;
    }
    bool willQueue = (!fresh && !queued && retryOk);
    portEXIT_CRITICAL(&g_mapMux);

    if (fresh || queued) return true;
    if (!willQueue) return false;

    auto job = [](void*) {
        weather_fetch_grid_map();
        portENTER_CRITICAL(&g_mapMux);
        g_gridFetchQueued = false;
        portEXIT_CRITICAL(&g_mapMux);
    };

    if (!http_worker_enqueue(job, nullptr)) {
        portENTER_CRITICAL(&g_mapMux);
        g_gridFetchQueued = false;
        portEXIT_CRITICAL(&g_mapMux);
        return false;
    }
    return true;
}

void weather_refresh_maps()
{
    g_lastRadarFetch = 0;
    g_lastGridFetch = 0;
    g_radarMap.valid = false;
    g_gridMap.valid = false;
    g_lastRadarAttempt = 0;
    g_lastGridAttempt = 0;
    g_radarFetchQueued = false;
    g_gridFetchQueued = false;
}
