#include "settings.h"
#include "weatherthing_hw.h"
#include <Preferences.h>

static Settings g_settings;
static Preferences g_prefs;

// WLED-inspired color palettes
// Each palette is 16 color stops, interpolated for smooth gradients

// Palette color definitions (RGB)
static const uint8_t PALETTE_DATA[][16][3] = {
    // PALETTE_CLASSIC: Green -> Yellow -> Red
    {{0,255,0}, {0,255,0}, {50,255,0}, {100,255,0}, {150,255,0}, {200,255,0}, {255,255,0}, {255,220,0},
     {255,180,0}, {255,140,0}, {255,100,0}, {255,60,0}, {255,30,0}, {255,0,0}, {255,0,0}, {255,0,0}},
    
    // PALETTE_OCEAN: Deep blue -> Cyan -> White
    {{0,0,80}, {0,20,120}, {0,40,160}, {0,80,200}, {0,120,220}, {0,160,240}, {0,200,255}, {50,220,255},
     {100,230,255}, {150,240,255}, {180,245,255}, {200,250,255}, {220,252,255}, {240,254,255}, {250,255,255}, {255,255,255}},
    
    // PALETTE_LAVA: Black -> Red -> Orange -> Yellow
    {{20,0,0}, {60,0,0}, {100,0,0}, {140,10,0}, {180,30,0}, {200,60,0}, {220,90,0}, {240,120,0},
     {255,150,0}, {255,170,20}, {255,190,50}, {255,210,80}, {255,220,120}, {255,235,160}, {255,245,200}, {255,255,220}},
    
    // PALETTE_FOREST: Dark green -> Green -> Teal -> Cyan
    {{0,40,0}, {0,60,10}, {0,80,20}, {0,100,30}, {0,130,50}, {0,160,70}, {0,180,100}, {0,200,130},
     {20,210,150}, {50,220,170}, {80,230,190}, {100,240,200}, {120,245,210}, {150,250,220}, {180,252,235}, {200,255,250}},
    
    // PALETTE_RAINBOW: Full spectrum
    {{255,0,0}, {255,60,0}, {255,120,0}, {255,180,0}, {255,255,0}, {180,255,0}, {0,255,0}, {0,255,120},
     {0,255,255}, {0,120,255}, {0,0,255}, {120,0,255}, {255,0,255}, {255,0,180}, {255,0,120}, {255,0,60}},
    
    // PALETTE_PARTY: Pink -> Purple -> Blue -> Cyan
    {{255,0,80}, {255,0,120}, {255,0,180}, {200,0,220}, {150,0,255}, {100,50,255}, {80,100,255}, {60,150,255},
     {50,180,255}, {80,200,255}, {120,220,255}, {150,230,255}, {180,240,255}, {200,250,255}, {220,255,255}, {240,255,255}},
    
    // PALETTE_SUNSET: Yellow -> Orange -> Red -> Purple -> Blue
    {{255,255,50}, {255,220,30}, {255,180,20}, {255,140,10}, {255,100,20}, {255,60,40}, {255,30,80}, {220,20,120},
     {180,20,160}, {140,30,200}, {100,50,220}, {80,80,240}, {60,100,255}, {50,120,255}, {40,140,255}, {30,160,255}},
    
    // PALETTE_ICE: White -> Cyan -> Blue
    {{255,255,255}, {240,255,255}, {220,250,255}, {200,245,255}, {180,240,255}, {150,230,255}, {120,220,255}, {100,200,255},
     {80,180,255}, {60,150,255}, {50,120,255}, {40,100,255}, {30,80,220}, {20,60,180}, {10,40,140}, {0,20,100}}
};

static const char* PALETTE_NAMES[] = {
    "Classic", "Ocean", "Lava", "Forest", "Rainbow", "Party", "Sunset", "Ice"
};

void settings_begin()
{
    // Set defaults
    g_settings.animSpeed = 5;
    g_settings.vuPalette = PALETTE_CLASSIC;
    g_settings.vuSensitivity = 5;
    g_settings.vuNoiseGate = 50;
    g_settings.micGain = 5;  // Normal gain
    g_settings.micBoost = 5; // Medium extra boost
    g_settings.vuInvert = false; // Normal direction by default
    g_settings.agcEnabled = true; // AGC enabled by default
    g_settings.vuSilenceMs = 500; // 500ms silence detection threshold
    g_settings.weatherPreset = 0;
    g_settings.tempPalette = 0;  // Default temperature colors
    g_settings.stockSymbol[0] = '\0';
    g_settings.stockEnabled = false;
    g_settings.cryptoSymbol[0] = '\0'; // Empty = BTC default
    g_settings.tzOffset = 0;  // UTC by default
    g_settings.btcUpdateMins = 5;    // 5 minute default
    g_settings.stockUpdateMins = 5;  // 5 minute default
    g_settings.brightMin = 5;        // Dark room brightness
    g_settings.brightMax = 150;      // Light room brightness (max 255)
    g_settings.brightMode = 0;       // Auto by default
    g_settings.brightManual = 50;    // Manual brightness level
    g_settings.brightBlanking = true; // Use blanking for cleaner readings
    g_settings.brightBlankSecs = 30;  // 30 second blanking interval
    g_settings.forecastHours = 12;   // 12 hour forecast default
    g_settings.simTimeoutSecs = 30;  // 30 second simulation timeout
    
    // MQTT defaults
    g_settings.mqttServer[0] = '\0';
    g_settings.mqttPort = 1883;
    g_settings.mqttUser[0] = '\0';
    g_settings.mqttPass[0] = '\0';
    g_settings.mqttTopic[0] = '\0';
    g_settings.mqttEnabled = false;
    
    // RSS defaults
    g_settings.rssUrl[0] = '\0';
    g_settings.rssPalette = 0;
    g_settings.rssSpeed = 5;
    g_settings.rssUpdateMins = 15;
    g_settings.rssItemCount = 3;     // Default 3 items
    g_settings.rssFormat = 0;        // Default title only

    // Card Cycle defaults
    for(int i=0; i<16; ++i) {
        // Enable cards 0-7 by default, disable Games(8), MQTT(9), RSS(10), and social(11-15)
        g_settings.cardEnabled[i] = (i < 8);
        g_settings.cardOrder[i] = i;
        g_settings.presetEnabled[i] = 0xFFFFFFFF; // All presets enabled by default
    }
    
    // Social media defaults (disabled by default)
    g_settings.ytChannelId[0] = '\0';
    g_settings.ytApiKey[0] = '\0';
    g_settings.twitchUser[0] = '\0';
    g_settings.twitchClientId[0] = '\0';
    g_settings.twitterUser[0] = '\0';
    g_settings.instaUser[0] = '\0';
    g_settings.tiktokUser[0] = '\0';
    g_settings.socialUpdateMins = 15; // 15 minute default
    g_settings.cycleDuration = 10;
    g_settings.cycleEnabled = false; // Disabled by default
    
    // Transition defaults
    g_settings.showTransitionTitle = true;  // Show titles by default
    g_settings.showTransitionAnim = true;   // Show animations by default
    
    // Demo mode defaults
    g_settings.demoMode = false;            // Disabled by default
    g_settings.demoDurationSecs = 5;        // 5 seconds per preset
    
    // Load from flash
    if (g_prefs.begin("wtsettings", true))
    {
        g_settings.animSpeed = g_prefs.getUChar("animSpeed", 5);
        g_settings.vuPalette = g_prefs.getUChar("vuPalette", 0);
        g_settings.vuSensitivity = g_prefs.getUChar("vuSens", 5);
        g_settings.vuNoiseGate = g_prefs.getUChar("vuNoise", 50);
        g_settings.micGain = g_prefs.getUChar("micGain", 5);
        g_settings.micBoost = g_prefs.getUChar("micBoost", 5);
        g_settings.vuInvert = g_prefs.getBool("vuInv", false);
        g_settings.agcEnabled = g_prefs.getBool("agcOn", true);
        g_settings.vuSilenceMs = g_prefs.getUShort("vuSilMs", 500);
        g_settings.weatherPreset = g_prefs.getUChar("wxPreset", 0);
        g_settings.tempPalette = g_prefs.getUChar("tempPal", 0);
        g_settings.stockEnabled = g_prefs.getBool("stockOn", false);
        g_settings.tzOffset = g_prefs.getChar("tzOffset", 0);
        g_settings.btcUpdateMins = g_prefs.getUChar("btcMins", 5);
        g_settings.stockUpdateMins = g_prefs.getUChar("stockMins", 5);
        
        String sym = g_prefs.getString("stockSym", "");
        strncpy(g_settings.stockSymbol, sym.c_str(), sizeof(g_settings.stockSymbol) - 1);
        g_settings.stockSymbol[sizeof(g_settings.stockSymbol) - 1] = '\0';
        
        String crypto = g_prefs.getString("cryptoSym", "");
        strncpy(g_settings.cryptoSymbol, crypto.c_str(), sizeof(g_settings.cryptoSymbol) - 1);
        g_settings.cryptoSymbol[sizeof(g_settings.cryptoSymbol) - 1] = '\0';
        
        // Brightness settings
        g_settings.brightMin = g_prefs.getUChar("brightMin", 5);
        g_settings.brightMax = g_prefs.getUChar("brightMax", 80);
        g_settings.brightMode = g_prefs.getUChar("brightMode", 0);
        g_settings.brightManual = g_prefs.getUChar("brightMan", 50);
        g_settings.brightBlanking = g_prefs.getBool("brightBlk", true);
        g_settings.brightBlankSecs = g_prefs.getUChar("blankSec", 30);
        g_settings.forecastHours = g_prefs.getUChar("fcstHours", 12);
        g_settings.simTimeoutSecs = g_prefs.getUShort("simTimeout", 30);
        
        // MQTT settings
        String mqttSrv = g_prefs.getString("mqttSrv", "");
        strncpy(g_settings.mqttServer, mqttSrv.c_str(), sizeof(g_settings.mqttServer) - 1);
        g_settings.mqttServer[sizeof(g_settings.mqttServer) - 1] = '\0';
        
        g_settings.mqttPort = g_prefs.getUShort("mqttPort", 1883);
        
        String mqttUsr = g_prefs.getString("mqttUsr", "");
        strncpy(g_settings.mqttUser, mqttUsr.c_str(), sizeof(g_settings.mqttUser) - 1);
        g_settings.mqttUser[sizeof(g_settings.mqttUser) - 1] = '\0';
        
        String mqttPwd = g_prefs.getString("mqttPwd", "");
        strncpy(g_settings.mqttPass, mqttPwd.c_str(), sizeof(g_settings.mqttPass) - 1);
        g_settings.mqttPass[sizeof(g_settings.mqttPass) - 1] = '\0';
        
        String mqttTop = g_prefs.getString("mqttTop", "");
        strncpy(g_settings.mqttTopic, mqttTop.c_str(), sizeof(g_settings.mqttTopic) - 1);
        g_settings.mqttTopic[sizeof(g_settings.mqttTopic) - 1] = '\0';
        
        g_settings.mqttEnabled = g_prefs.getBool("mqttOn", false);
        
        // RSS settings
        String rssU = g_prefs.getString("rssUrl", "");
        strncpy(g_settings.rssUrl, rssU.c_str(), sizeof(g_settings.rssUrl) - 1);
        g_settings.rssUrl[sizeof(g_settings.rssUrl) - 1] = '\0';
        
        g_settings.rssPalette = g_prefs.getUChar("rssPal", 0);
        g_settings.rssSpeed = g_prefs.getUChar("rssSpd", 5);
        g_settings.rssUpdateMins = g_prefs.getUChar("rssMins", 15);
        g_settings.rssItemCount = g_prefs.getUChar("rssCnt", 3);
        g_settings.rssFormat = g_prefs.getUChar("rssFmt", 0);
        
        // Card Cycle settings
        if (g_prefs.isKey("cardEn16")) g_prefs.getBytes("cardEn16", g_settings.cardEnabled, 16);
        else if (g_prefs.isKey("cardEn")) g_prefs.getBytes("cardEn", g_settings.cardEnabled, 12);
        if (g_prefs.isKey("cardOrd16")) g_prefs.getBytes("cardOrd16", g_settings.cardOrder, 16);
        else if (g_prefs.isKey("cardOrd")) g_prefs.getBytes("cardOrd", g_settings.cardOrder, 12);
        if (g_prefs.isKey("presetEn16")) g_prefs.getBytes("presetEn16", g_settings.presetEnabled, 64); // 16 * 4 bytes
        else if (g_prefs.isKey("presetEn")) g_prefs.getBytes("presetEn", g_settings.presetEnabled, 48);
        
        // Social media settings
        String ytCh = g_prefs.getString("ytChan", "");
        strncpy(g_settings.ytChannelId, ytCh.c_str(), sizeof(g_settings.ytChannelId) - 1);
        g_settings.ytChannelId[sizeof(g_settings.ytChannelId) - 1] = '\0';
        
        String ytKey = g_prefs.getString("ytKey", "");
        strncpy(g_settings.ytApiKey, ytKey.c_str(), sizeof(g_settings.ytApiKey) - 1);
        g_settings.ytApiKey[sizeof(g_settings.ytApiKey) - 1] = '\0';
        
        String twUsr = g_prefs.getString("twUser", "");
        strncpy(g_settings.twitchUser, twUsr.c_str(), sizeof(g_settings.twitchUser) - 1);
        g_settings.twitchUser[sizeof(g_settings.twitchUser) - 1] = '\0';
        
        String twCid = g_prefs.getString("twCid", "");
        strncpy(g_settings.twitchClientId, twCid.c_str(), sizeof(g_settings.twitchClientId) - 1);
        g_settings.twitchClientId[sizeof(g_settings.twitchClientId) - 1] = '\0';
        
        String xUsr = g_prefs.getString("xUser", "");
        strncpy(g_settings.twitterUser, xUsr.c_str(), sizeof(g_settings.twitterUser) - 1);
        g_settings.twitterUser[sizeof(g_settings.twitterUser) - 1] = '\0';
        
        String igUsr = g_prefs.getString("igUser", "");
        strncpy(g_settings.instaUser, igUsr.c_str(), sizeof(g_settings.instaUser) - 1);
        g_settings.instaUser[sizeof(g_settings.instaUser) - 1] = '\0';
        
        String ttUsr = g_prefs.getString("ttUser", "");
        strncpy(g_settings.tiktokUser, ttUsr.c_str(), sizeof(g_settings.tiktokUser) - 1);
        g_settings.tiktokUser[sizeof(g_settings.tiktokUser) - 1] = '\0';
        
        g_settings.socialUpdateMins = g_prefs.getUChar("socMins", 15);
        g_settings.cycleDuration = g_prefs.getUShort("cycleDur", 10);
        g_settings.cycleEnabled = g_prefs.getBool("cycleOn", false);
        
        // Transition settings
        g_settings.showTransitionTitle = g_prefs.getBool("trTitle", true);
        g_settings.showTransitionAnim = g_prefs.getBool("trAnim", true);
        
        // Demo mode settings
        g_settings.demoMode = g_prefs.getBool("demoOn", false);
        g_settings.demoDurationSecs = g_prefs.getUChar("demoSecs", 5);
        
        g_prefs.end();
    }
    
    // Clamp values
    if (g_settings.animSpeed < 1) g_settings.animSpeed = 1;
    if (g_settings.animSpeed > 10) g_settings.animSpeed = 10;
    if (g_settings.rssSpeed < 1) g_settings.rssSpeed = 1;
    if (g_settings.rssSpeed > 10) g_settings.rssSpeed = 10;
    if (g_settings.vuPalette >= PALETTE_COUNT) g_settings.vuPalette = 0;
    if (g_settings.vuSensitivity < 1) g_settings.vuSensitivity = 1;
    if (g_settings.vuSensitivity > 10) g_settings.vuSensitivity = 10;
    if (g_settings.micGain < 1) g_settings.micGain = 1;
    if (g_settings.micGain > 10) g_settings.micGain = 10;
    if (g_settings.tzOffset < -12) g_settings.tzOffset = -12;
    if (g_settings.tzOffset > 14) g_settings.tzOffset = 14;
    if (g_settings.btcUpdateMins < 1) g_settings.btcUpdateMins = 1;
    if (g_settings.btcUpdateMins > 60) g_settings.btcUpdateMins = 60;
    if (g_settings.stockUpdateMins < 1) g_settings.stockUpdateMins = 1;
    if (g_settings.stockUpdateMins > 60) g_settings.stockUpdateMins = 60;
    if (g_settings.brightMin < 5) g_settings.brightMin = 5;
    if (g_settings.brightMin > 40) g_settings.brightMin = 40;
    if (g_settings.brightMax < 20) g_settings.brightMax = 20;
    if (g_settings.brightMax > 255) g_settings.brightMax = 255;
    if (g_settings.brightManual < 5) g_settings.brightManual = 5;
    if (g_settings.brightManual > 255) g_settings.brightManual = 255;
    if (g_settings.forecastHours != 12 && g_settings.forecastHours != 24 && g_settings.forecastHours != 48) {
        g_settings.forecastHours = 12;
    }
}

Settings& settings_get()
{
    return g_settings;
}

void settings_save()
{
    if (g_prefs.begin("wtsettings", false))
    {
        g_prefs.putUChar("animSpeed", g_settings.animSpeed);
        g_prefs.putUChar("vuPalette", g_settings.vuPalette);
        g_prefs.putUChar("vuSens", g_settings.vuSensitivity);
        g_prefs.putUChar("vuNoise", g_settings.vuNoiseGate);
        g_prefs.putUChar("micGain", g_settings.micGain);
        g_prefs.putBool("vuInv", g_settings.vuInvert);
        g_prefs.putUChar("micBoost", g_settings.micBoost);
        g_prefs.putBool("agcOn", g_settings.agcEnabled);
        g_prefs.putUShort("vuSilMs", g_settings.vuSilenceMs);
        g_prefs.putUChar("wxPreset", g_settings.weatherPreset);
        g_prefs.putUChar("tempPal", g_settings.tempPalette);
        g_prefs.putBool("stockOn", g_settings.stockEnabled);
        g_prefs.putString("stockSym", g_settings.stockSymbol);
        g_prefs.putString("cryptoSym", g_settings.cryptoSymbol);
        g_prefs.putChar("tzOffset", g_settings.tzOffset);
        g_prefs.putUChar("btcMins", g_settings.btcUpdateMins);
        g_prefs.putUChar("stockMins", g_settings.stockUpdateMins);
        g_prefs.putUChar("brightMin", g_settings.brightMin);
        g_prefs.putUChar("brightMax", g_settings.brightMax);
        g_prefs.putUChar("brightMode", g_settings.brightMode);
        g_prefs.putUChar("brightMan", g_settings.brightManual);
        g_prefs.putBool("brightBlk", g_settings.brightBlanking);
        g_prefs.putUChar("blankSec", g_settings.brightBlankSecs);
        g_prefs.putUChar("fcstHours", g_settings.forecastHours);
        g_prefs.putUShort("simTimeout", g_settings.simTimeoutSecs);
        
        // MQTT settings
        g_prefs.putString("mqttSrv", g_settings.mqttServer);
        g_prefs.putUShort("mqttPort", g_settings.mqttPort);
        g_prefs.putString("mqttUsr", g_settings.mqttUser);
        g_prefs.putString("mqttPwd", g_settings.mqttPass);
        g_prefs.putString("mqttTop", g_settings.mqttTopic);
        g_prefs.putBool("mqttOn", g_settings.mqttEnabled);
        
        // RSS settings
        g_prefs.putString("rssUrl", g_settings.rssUrl);
        g_prefs.putUChar("rssPal", g_settings.rssPalette);
        g_prefs.putUChar("rssSpd", g_settings.rssSpeed);
        g_prefs.putUChar("rssMins", g_settings.rssUpdateMins);
        g_prefs.putUChar("rssCnt", g_settings.rssItemCount);
        g_prefs.putUChar("rssFmt", g_settings.rssFormat);
        
        // Card Cycle settings
        g_prefs.putBytes("cardEn16", g_settings.cardEnabled, 16);
        g_prefs.putBytes("cardOrd16", g_settings.cardOrder, 16);
        g_prefs.putBytes("presetEn16", g_settings.presetEnabled, 64); // 16 * 4 bytes
        
        // Social media settings
        g_prefs.putString("ytChan", g_settings.ytChannelId);
        g_prefs.putString("ytKey", g_settings.ytApiKey);
        g_prefs.putString("twUser", g_settings.twitchUser);
        g_prefs.putString("twCid", g_settings.twitchClientId);
        g_prefs.putString("xUser", g_settings.twitterUser);
        g_prefs.putString("igUser", g_settings.instaUser);
        g_prefs.putString("ttUser", g_settings.tiktokUser);
        g_prefs.putUChar("socMins", g_settings.socialUpdateMins);
        g_prefs.putUShort("cycleDur", g_settings.cycleDuration);
        g_prefs.putBool("cycleOn", g_settings.cycleEnabled);
        
        // Transition settings
        g_prefs.putBool("trTitle", g_settings.showTransitionTitle);
        g_prefs.putBool("trAnim", g_settings.showTransitionAnim);
        
        // Demo mode settings
        g_prefs.putBool("demoOn", g_settings.demoMode);
        g_prefs.putUChar("demoSecs", g_settings.demoDurationSecs);
        
        g_prefs.end();
    }
}

const char* settings_palette_name(uint8_t palette)
{
    if (palette < PALETTE_COUNT) return PALETTE_NAMES[palette];
    return "Unknown";
}

// Interpolate between two colors
static uint32_t lerpColor(uint8_t r1, uint8_t g1, uint8_t b1, 
                          uint8_t r2, uint8_t g2, uint8_t b2, 
                          uint8_t t, uint8_t brightness)
{
    uint8_t r = r1 + ((int16_t)(r2 - r1) * t / 255);
    uint8_t g = g1 + ((int16_t)(g2 - g1) * t / 255);
    uint8_t b = b1 + ((int16_t)(b2 - b1) * t / 255);
    
    if (brightness < 255) {
        r = (uint16_t)r * brightness / 255;
        g = (uint16_t)g * brightness / 255;
        b = (uint16_t)b * brightness / 255;
    }
    
    return wt_color(r, g, b);
}

uint32_t settings_palette_color(uint8_t palette, uint8_t pos, uint8_t brightness)
{
    if (palette >= PALETTE_COUNT) palette = 0;
    
    // Map 0-255 to palette index (16 stops)
    uint8_t idx1 = pos >> 4;  // 0-15
    uint8_t idx2 = (idx1 + 1) & 0x0F;  // Wrap around
    uint8_t frac = (pos & 0x0F) << 4;  // 0-255 fractional
    
    const uint8_t* c1 = PALETTE_DATA[palette][idx1];
    const uint8_t* c2 = PALETTE_DATA[palette][idx2];
    
    return lerpColor(c1[0], c1[1], c1[2], c2[0], c2[1], c2[2], frac, brightness);
}
