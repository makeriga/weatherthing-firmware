#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

// Audio/VU color palettes (WLED-inspired)
enum ColorPalette : uint8_t
{
    PALETTE_CLASSIC = 0,     // Green-Yellow-Red spectrum
    PALETTE_OCEAN = 1,       // Blue-Cyan-White
    PALETTE_LAVA = 2,        // Red-Orange-Yellow fire
    PALETTE_FOREST = 3,      // Green-Teal-Cyan nature
    PALETTE_RAINBOW = 4,     // Full rainbow cycle
    PALETTE_PARTY = 5,       // Pink-Purple-Blue party
    PALETTE_SUNSET = 6,      // Orange-Pink-Purple
    PALETTE_ICE = 7,         // White-Cyan-Blue cold
    PALETTE_COUNT = 8
};

// Global settings structure
struct Settings
{
    // Animation
    uint8_t animSpeed;       // 1-10 (1=slow, 5=normal, 10=fast)
    
    // Audio/VU
    uint8_t vuPalette;       // ColorPalette enum
    uint8_t vuSensitivity;   // 1-10
    uint8_t vuNoiseGate;     // 0-255
    uint8_t micGain;         // 1-10 (1=low, 5=normal, 10=high)
    bool vuInvert;           // Invert VU meter response
    
    // Weather display
    uint8_t weatherPreset;   // 0=classic, 1=fullscreen
    uint8_t tempPalette;     // 0=default (blue-cyan-green-yellow-red), 1=cool (purple-blue-cyan), 2=warm (yellow-orange-red)
    
    // Ticker
    char stockSymbol[12];    // Stock ticker symbol (e.g., "AAPL")
    bool stockEnabled;       // Show stock instead of BTC
    
    // Clock
    int8_t tzOffset;         // Timezone offset in hours (-12 to +14)
    
    // Financial card update intervals (in minutes)
    uint8_t btcUpdateMins;   // BTC update interval (1-60 min, default 5)
    uint8_t stockUpdateMins; // Stock update interval (1-60 min, default 5)
    
    // Brightness control
    uint8_t brightMin;       // Minimum brightness (dark room) 5-40, default 8
    uint8_t brightMax;       // Maximum brightness (light room) 20-80, default 50
    uint8_t brightMode;      // 0=auto, 1=manual fixed
    uint8_t brightManual;    // Manual brightness level 5-80
    bool brightBlanking;     // Use blanking frame for light measurement
    
    // Timeline settings
    uint8_t forecastHours;   // 12, 24, or 48 hours forecast
    
    // Weather simulation timeout
    uint16_t simTimeoutSecs; // 0=indefinite, else seconds before returning to real weather
    
    // MQTT settings
    char mqttServer[48];     // MQTT broker hostname/IP
    uint16_t mqttPort;       // MQTT port (default 1883)
    char mqttUser[32];       // MQTT username (optional)
    char mqttPass[32];       // MQTT password (optional)
    char mqttTopic[64];      // Custom subscribe topic (optional)
    bool mqttEnabled;        // MQTT enabled flag
    
    // Card Cycle Settings
    bool cardEnabled[10];     // Enabled/Disabled state for each card
    uint8_t cardOrder[10];    // Display order (indices 0-9)
    uint16_t cycleDuration;   // Seconds per card (0 = manual only)
    bool cycleEnabled;        // Enable auto cycling
};

// Initialize settings (load from flash)
void settings_begin();

// Get current settings
Settings& settings_get();

// Save settings to flash
void settings_save();

// Get palette name
const char* settings_palette_name(uint8_t palette);

// Get color from palette at position (0-255)
uint32_t settings_palette_color(uint8_t palette, uint8_t pos, uint8_t brightness = 255);

#endif
