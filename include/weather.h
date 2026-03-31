#ifndef WEATHER_H
#define WEATHER_H

#include <stdint.h>

// Expanded weather condition types
enum WeatherType : uint8_t
{
    WEATHER_SUNNY = 0,       // Clear sky
    WEATHER_PARTLY_CLOUDY = 1, // Partly cloudy
    WEATHER_CLOUDY = 2,      // Overcast
    WEATHER_FOG = 3,         // Fog/mist
    WEATHER_DRIZZLE = 4,     // Light drizzle
    WEATHER_RAIN = 5,        // Rain
    WEATHER_HEAVY_RAIN = 6,  // Heavy rain/showers
    WEATHER_STORM = 7,       // Thunderstorm
    WEATHER_SNOW = 8,        // Snow
    WEATHER_SLEET = 9,       // Sleet/freezing rain
    WEATHER_WIND = 10,       // Windy (used for high wind conditions)
    WEATHER_CLEAR_NIGHT = 11,// Clear night sky
    WEATHER_TYPE_COUNT = 12
};

struct WeatherData
{
    int8_t temp;        // Current temperature in C
    WeatherType type;   // Current condition
    bool valid;         // Whether data is valid
};

struct ForecastSlot
{
    int8_t temp;
    WeatherType type;
};

// Initialize weather module
void weather_begin();

// Call periodically to fetch/update weather
void weather_update();

// Get current weather
WeatherData weather_get_current();

// Get forecast slot (0-11 for 12-hour forecast)
ForecastSlot weather_get_forecast(uint8_t slot);

// Set location (latitude, longitude)
void weather_set_location(float lat, float lon);

// Get current location
void weather_get_location(float* lat, float* lon);

// Set location by city name (uses geocoding API)
bool weather_set_city(const char* city);

// Resolve/apply timezone for current location (DST-aware when possible)
bool weather_sync_timezone_from_location();

// Simulate weather for testing (stays until location change or stop)
void weather_simulate(uint8_t type, int8_t temp);

// Stop simulation mode and resume fetching
void weather_stop_simulation();

// Check if weather data is currently being fetched
bool weather_is_fetching();

// Check if weather data is stale (hasn't been updated in a while)
bool weather_is_stale();

// Get age of weather data in seconds (0 if never fetched)
uint32_t weather_data_age_secs();

// Get weather type name
const char* weather_type_name(WeatherType type);

// ============== MAP PRESETS DATA ==============

// RainViewer radar data (20x7 precipitation intensity grid)
struct RadarMapData {
    uint8_t intensity[20][7];  // 0-255 precipitation intensity
    uint32_t timestamp;        // When data was fetched
    bool valid;
};

// Open-Meteo grid data (20x7 cloud/precip grid)
struct GridMapData {
    uint8_t cloud[20][7];      // 0-100 cloud cover %
    uint8_t precip[20][7];     // 0-255 precipitation intensity (scaled)
    uint32_t timestamp;
    bool valid;
};

// Fetch RainViewer radar tile and downsample to 20x7
bool weather_fetch_radar_map();

// Fetch Open-Meteo grid data for 140 points around location
bool weather_fetch_grid_map();

// Non-blocking map fetch requests (schedule if needed)
bool weather_request_radar_map();
bool weather_request_grid_map();

// Copy latest map data into caller-provided struct
void weather_get_radar_map_copy(RadarMapData* out);
void weather_get_grid_map_copy(GridMapData* out);

// Get current radar map data
const RadarMapData& weather_get_radar_map();

// Get current grid map data
const GridMapData& weather_get_grid_map();

// Force refresh of map data
void weather_refresh_maps();

#endif
