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
    uint8_t vuNoiseGate;     // 0-255 (noise floor level)
    uint8_t micGain;         // 1-10 (1=low, 5=normal, 10=high)
    uint8_t micBoost;        // 0-10 (extra amplification: 0=off, 10=max ~50x extra)
    bool vuInvert;           // Invert VU meter response
    bool agcEnabled;         // Enable automatic gain control
    uint16_t vuSilenceMs;    // Silence detection threshold in ms (0=disabled, 100-2000)
    uint16_t agcMin;
    uint16_t agcMax;
    uint8_t agcAttack;
    uint8_t agcDecay;
    uint8_t envAttack;
    uint8_t envDecay;
    uint8_t beatThreshold;
    uint8_t beatHold;
    
    // Weather display
    uint8_t weatherPreset;   // 0=classic, 1=fullscreen
    uint8_t weatherProvider; // 0=auto, 1=open-meteo, 2=met.no
    uint8_t tempPalette;     // 0=default (blue-cyan-green-yellow-red), 1=cool (purple-blue-cyan), 2=warm (yellow-orange-red)
    
    // Weather audio reactivity
    bool wxAudioHue;         // Shift hue/brightness on beat
    bool wxAudioSpeed;       // Speed up animations on beat
    
    // Map presets config
    uint8_t mapZoom;         // Map zoom level 2-6 (lower = wider area)
    uint8_t mapStyle;        // 0=precip colors, 1=cloud emphasis, 2=temperature
    
    // Ticker
    char stockSymbol[12];    // Stock ticker symbol (e.g., "AAPL")
    bool stockEnabled;       // Show stock instead of BTC
    char cryptoSymbol[12];   // Crypto ticker symbol (e.g., "ETH", "DOGE") - empty = BTC
    
    // Clock
    int8_t tzOffset;         // Timezone offset in hours (-12 to +14)
    
    // Financial card update intervals (in minutes)
    uint8_t btcUpdateMins;   // BTC update interval (1-60 min, default 5)
    uint8_t stockUpdateMins; // Stock update interval (1-60 min, default 5)
    
    // Brightness control
    uint8_t brightMin;       // Minimum brightness (dark room) 5-40, default 8
    uint8_t brightMax;       // Maximum brightness (light room) 20-127, default 80
    uint8_t brightMode;      // 0=auto, 1=manual fixed
    uint8_t brightManual;    // Manual brightness level 1-127 (or 255 if highPower)
    bool brightBlanking;     // Use blanking frame for light measurement
    uint8_t brightBlankSecs; // Blanking interval in seconds (10-120)
    bool highPowerMode;      // Allow brightness >127 (WARNING: heat risk, bare PCB only)
    
    // Timeline settings
    uint8_t forecastHours;   // 12, 24, or 48 hours forecast
    uint32_t wxTimelineSunny;
    uint32_t wxTimelineCloudy;
    uint32_t wxTimelineRain;
    uint32_t wxTimelineStorm;
    uint32_t wxTimelineSnow;
    uint32_t wxTimelineWind;
    
    // Weather simulation timeout
    uint16_t simTimeoutSecs; // 0=indefinite, else seconds before returning to real weather
    
    // MQTT settings
    char mqttServer[48];     // MQTT broker hostname/IP
    uint16_t mqttPort;       // MQTT port (default 1883)
    char mqttUser[32];       // MQTT username (optional)
    char mqttPass[32];       // MQTT password (optional)
    char mqttTopic[64];      // Custom subscribe topic (optional)
    bool mqttEnabled;        // MQTT enabled flag
    
    // RSS Settings
    char rssUrl[128];        // RSS Feed URL
    uint8_t rssPalette;      // Color palette for RSS
    uint8_t rssSpeed;        // Scroll speed (1-10)
    uint8_t rssUpdateMins;   // Update interval
    uint8_t rssItemCount;    // Number of items to fetch (1-10)
    uint8_t rssFormat;       // 0=Title only, 1=Title + Description
    
    // Card Cycle Settings
    bool cardEnabled[16];     // Enabled/Disabled state for each card (expanded for social)
    uint8_t cardOrder[16];    // Display order (indices)
    uint16_t cycleDuration;   // Seconds per card (0 = manual only)
    bool cycleEnabled;        // Enable auto cycling
    
    // Per-preset rotation settings (bitmask per card type, bit=1 means included in rotation)
    uint32_t presetEnabled[16]; // Up to 32 presets per card type (expanded for social cards)
    
    // Social Media Settings
    char ytChannelId[32];        // YouTube Channel ID
    char ytApiKey[48];           // YouTube API Key
    char twitchUser[32];         // Twitch username
    char twitchClientId[48];     // Twitch Client ID
    char twitterUser[32];        // Twitter/X username  
    char instaUser[32];          // Instagram username
    char tiktokUser[32];         // TikTok username
    uint8_t socialUpdateMins;    // Update interval for social cards (1-60 min)
    
    // Transition Settings
    bool showTransitionTitle;    // Show card title during transitions (default true)
    bool showTransitionAnim;     // Show transition animations (default true)
    
    // Demo Mode Settings
    bool demoMode;               // Enable demo mode for video capture
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
