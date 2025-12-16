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
    g_settings.vuPalette = PALETTE_PARTY;
    g_settings.vuNoiseGate = 50;
    g_settings.micGain = 3;  // More responsive at moderate volume
    g_settings.micBoost = 0; // Off by default
    g_settings.vuInvert = false; // Normal direction by default
    g_settings.agcEnabled = true; // AGC enabled by default
    g_settings.vuSilenceMs = 500; // 500ms silence detection threshold
    g_settings.agcMin = 152;
    g_settings.agcMax = 11926;
    g_settings.agcAttack = 2;
    g_settings.agcDecay = 32;
    g_settings.envAttack = 2;
    g_settings.envDecay = 16;
    g_settings.beatThreshold = 77;
    g_settings.beatHold = 15;
    g_settings.weatherPreset = 0;
    g_settings.weatherProvider = 0;
    g_settings.tempPalette = 0;  // Default temperature colors
    g_settings.wxAudioHue = false;   // Weather audio hue shift off by default
    g_settings.wxAudioSpeed = false; // Weather audio speed off by default
    g_settings.mapZoom = 4;      // Medium zoom level
    g_settings.mapStyle = 0;     // Precipitation colors
    g_settings.stockSymbol[0] = '\0';
    g_settings.stockEnabled = false;
    g_settings.cryptoSymbol[0] = '\0'; // Empty = BTC default
    g_settings.tzOffset = 2;  // GMT+2 (Riga, Latvia) by default
    g_settings.btcUpdateMins = 5;    // 5 minute default
    g_settings.stockUpdateMins = 5;  // 5 minute default
    g_settings.brightMin = 2;         // Minimum brightness (absolute min)
    g_settings.brightMax = 80;        // Light room brightness (max 127 safe, 255 with highPower)
    g_settings.brightMode = 0;        // Auto by default
    g_settings.brightManual = 50;     // Manual brightness level
    g_settings.brightBlanking = false; // Blanking disabled by default
    g_settings.brightBlankSecs = 30;  // 30 second blanking interval
    g_settings.highPowerMode = false; // Safe mode by default
    g_settings.forecastHours = 12;   // 12 hour forecast default
    g_settings.wxTimelineSunny = wt_color(255, 180, 0);    // Warm yellow-orange
    g_settings.wxTimelineCloudy = wt_color(140, 140, 160);  // Visible gray-blue tint
    g_settings.wxTimelineRain = wt_color(30, 80, 180);      // Darker visible blue
    g_settings.wxTimelineStorm = wt_color(120, 0, 200);     // Purple
    g_settings.wxTimelineSnow = wt_color(200, 220, 255);    // Icy white-blue
    g_settings.wxTimelineWind = wt_color(0, 200, 180);      // Teal
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
    // Enable: Weather(0), Clock(1), Network(4), Audio(5), Sparkle(6), Aurora(7), Countdown(12), Pomodoro(13), Sun(14), Stopwatch(15)
    // Disable: BTC(2), Stocks(3), Games(8), MQTT(9), RSS(10), YouTube(11)
    for(int i=0; i<16; ++i) {
        bool enabled = (i == 0 || i == 1 || i == 4 || i == 5 || i == 6 || i == 7 || i >= 12);
        g_settings.cardEnabled[i] = enabled;
        g_settings.cardOrder[i] = i;
        g_settings.presetEnabled[i] = 0xFFFFFFFFFFFFFFFFULL; // All presets enabled by default
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
    
    // Load from flash
    if (g_prefs.begin("wtsettings", true))
    {
        g_settings.animSpeed = g_prefs.getUChar("animSpeed", 5);
        g_settings.vuPalette = g_prefs.getUChar("vuPalette", PALETTE_PARTY);
        g_settings.vuNoiseGate = g_prefs.getUChar("vuNoise", 50);
        g_settings.micGain = g_prefs.getUChar("micGain", 3);
        g_settings.micBoost = g_prefs.getUChar("micBoost", 0);
        g_settings.vuInvert = g_prefs.getBool("vuInv", false);
        g_settings.agcEnabled = g_prefs.getBool("agcOn", true);
        g_settings.vuSilenceMs = g_prefs.getUShort("vuSilMs", 500);
        g_settings.agcMin = g_prefs.getUShort("agcMin", 152);
        g_settings.agcMax = g_prefs.getUShort("agcMax", 11926);
        g_settings.agcAttack = g_prefs.getUChar("agcAtk", 2);
        g_settings.agcDecay = g_prefs.getUChar("agcDcy", 32);
        g_settings.envAttack = g_prefs.getUChar("envAtk", 2);
        g_settings.envDecay = g_prefs.getUChar("envDcy", 16);
        g_settings.beatThreshold = g_prefs.getUChar("beatThr", 77);
        g_settings.beatHold = g_prefs.getUChar("beatHld", 15);
        g_settings.weatherPreset = g_prefs.getUChar("wxPreset", 0);
        g_settings.weatherProvider = g_prefs.getUChar("wxProv", 0);
        g_settings.tempPalette = g_prefs.getUChar("tempPal", 0);
        g_settings.wxAudioHue = g_prefs.getBool("wxAudHue", false);
        g_settings.wxAudioSpeed = g_prefs.getBool("wxAudSpd", false);
        g_settings.mapZoom = g_prefs.getUChar("mapZoom", 4);
        g_settings.mapStyle = g_prefs.getUChar("mapStyle", 0);
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
        g_settings.brightMin = g_prefs.getUChar("brightMin", 2);
        g_settings.brightMax = g_prefs.getUChar("brightMax", 80);
        g_settings.brightMode = g_prefs.getUChar("brightMode", 0);
        g_settings.brightManual = g_prefs.getUChar("brightMan", 50);
        g_settings.brightBlanking = g_prefs.getBool("brightBlk", false);
        g_settings.brightBlankSecs = g_prefs.getUChar("blankSec", 30);
        g_settings.highPowerMode = g_prefs.getBool("hiPower", false);
        g_settings.forecastHours = g_prefs.getUChar("fcstHours", 12);
        g_settings.wxTimelineSunny = g_prefs.getUInt("wxTlSun", wt_color(255, 180, 0));
        g_settings.wxTimelineCloudy = g_prefs.getUInt("wxTlCld", wt_color(100, 100, 120));
        g_settings.wxTimelineRain = g_prefs.getUInt("wxTlRai", wt_color(0, 0, 255));
        g_settings.wxTimelineStorm = g_prefs.getUInt("wxTlSto", wt_color(100, 0, 200));
        g_settings.wxTimelineSnow = g_prefs.getUInt("wxTlSno", wt_color(255, 255, 255));
        g_settings.wxTimelineWind = g_prefs.getUInt("wxTlWin", wt_color(0, 255, 200));
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
        // presetEnabled migration:
        // - Old firmwares stored 16x uint32 (64 bytes) under presetEn16 (or 12x uint32 under presetEn)
        // - New firmwares store 16x uint64 (128 bytes) under presetEn16
        if (g_prefs.isKey("presetEn16")) {
            uint8_t buf[128];
            size_t n = g_prefs.getBytes("presetEn16", buf, sizeof(buf));
            if (n == 64) {
                uint32_t tmp32[16];
                memcpy(tmp32, buf, 64);
                for (int i = 0; i < 16; ++i) {
                    // Preserve old selection for presets 0-31, enable new presets 32-63 by default.
                    g_settings.presetEnabled[i] = (0xFFFFFFFF00000000ULL) | (uint64_t)tmp32[i];
                }
            } else if (n == 128) {
                uint64_t tmp64[16];
                memcpy(tmp64, buf, 128);
                for (int i = 0; i < 16; ++i) {
                    g_settings.presetEnabled[i] = tmp64[i];
                }
            }
        }
        else if (g_prefs.isKey("presetEn")) {
            uint8_t buf[48];
            size_t n = g_prefs.getBytes("presetEn", buf, sizeof(buf));
            if (n == 48) {
                uint32_t tmp32[12];
                memcpy(tmp32, buf, 48);
                for (int i = 0; i < 12; ++i) {
                    g_settings.presetEnabled[i] = (0xFFFFFFFF00000000ULL) | (uint64_t)tmp32[i];
                }
            }
        }
        
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
        
        g_prefs.end();
    }
    
    // Clamp values
    if (g_settings.animSpeed < 1) g_settings.animSpeed = 1;
    if (g_settings.animSpeed > 10) g_settings.animSpeed = 10;
    if (g_settings.rssSpeed < 1) g_settings.rssSpeed = 1;
    if (g_settings.rssSpeed > 10) g_settings.rssSpeed = 10;
    if (g_settings.vuPalette >= PALETTE_COUNT) g_settings.vuPalette = 0;
    if (g_settings.micGain < 1) g_settings.micGain = 1;
    if (g_settings.micGain > 10) g_settings.micGain = 10;
    if (g_settings.micBoost > 10) g_settings.micBoost = 10;
    if (g_settings.vuSilenceMs > 2000) g_settings.vuSilenceMs = 2000;
    if (g_settings.agcMin < 20) g_settings.agcMin = 20;
    if (g_settings.agcMin > 2000) g_settings.agcMin = 2000;
    if (g_settings.agcMax < g_settings.agcMin) g_settings.agcMax = g_settings.agcMin;
    if (g_settings.agcMax > 65000) g_settings.agcMax = 65000;
    if (g_settings.agcAttack < 1) g_settings.agcAttack = 1;
    if (g_settings.agcAttack > 64) g_settings.agcAttack = 64;
    if (g_settings.agcDecay < 2) g_settings.agcDecay = 2;
    if (g_settings.agcDecay > 128) g_settings.agcDecay = 128;
    if (g_settings.envAttack < 1) g_settings.envAttack = 1;
    if (g_settings.envAttack > 32) g_settings.envAttack = 32;
    if (g_settings.envDecay < 2) g_settings.envDecay = 2;
    if (g_settings.envDecay > 128) g_settings.envDecay = 128;
    if (g_settings.beatThreshold < 10) g_settings.beatThreshold = 10;
    if (g_settings.beatThreshold > 250) g_settings.beatThreshold = 250;
    if (g_settings.beatHold < 1) g_settings.beatHold = 1;
    if (g_settings.beatHold > 60) g_settings.beatHold = 60;
    if (g_settings.tzOffset < -12) g_settings.tzOffset = -12;
    if (g_settings.tzOffset > 14) g_settings.tzOffset = 14;
    if (g_settings.btcUpdateMins < 1) g_settings.btcUpdateMins = 1;
    if (g_settings.btcUpdateMins > 60) g_settings.btcUpdateMins = 60;
    if (g_settings.stockUpdateMins < 1) g_settings.stockUpdateMins = 1;
    if (g_settings.stockUpdateMins > 60) g_settings.stockUpdateMins = 60;
    if (g_settings.brightMin < 5) g_settings.brightMin = 5;
    if (g_settings.brightMin > 40) g_settings.brightMin = 40;
    // Brightness limits depend on high power mode
    uint8_t maxAllowed = g_settings.highPowerMode ? 255 : 127;
    if (g_settings.brightMax < 20) g_settings.brightMax = 20;
    if (g_settings.brightMax > maxAllowed) g_settings.brightMax = maxAllowed;
    if (g_settings.brightManual < 5) g_settings.brightManual = 5;
    if (g_settings.brightManual > maxAllowed) g_settings.brightManual = maxAllowed;
    if (g_settings.weatherProvider > 2) g_settings.weatherProvider = 0;
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
        g_prefs.putUChar("vuNoise", g_settings.vuNoiseGate);
        g_prefs.putUChar("micGain", g_settings.micGain);
        g_prefs.putBool("vuInv", g_settings.vuInvert);
        g_prefs.putUChar("micBoost", g_settings.micBoost);
        g_prefs.putBool("agcOn", g_settings.agcEnabled);
        g_prefs.putUShort("vuSilMs", g_settings.vuSilenceMs);
        g_prefs.putUShort("agcMin", g_settings.agcMin);
        g_prefs.putUShort("agcMax", g_settings.agcMax);
        g_prefs.putUChar("agcAtk", g_settings.agcAttack);
        g_prefs.putUChar("agcDcy", g_settings.agcDecay);
        g_prefs.putUChar("envAtk", g_settings.envAttack);
        g_prefs.putUChar("envDcy", g_settings.envDecay);
        g_prefs.putUChar("beatThr", g_settings.beatThreshold);
        g_prefs.putUChar("beatHld", g_settings.beatHold);
        g_prefs.putUChar("wxPreset", g_settings.weatherPreset);
        g_prefs.putUChar("wxProv", g_settings.weatherProvider);
        g_prefs.putUChar("tempPal", g_settings.tempPalette);
        g_prefs.putBool("wxAudHue", g_settings.wxAudioHue);
        g_prefs.putBool("wxAudSpd", g_settings.wxAudioSpeed);
        g_prefs.putUChar("mapZoom", g_settings.mapZoom);
        g_prefs.putUChar("mapStyle", g_settings.mapStyle);
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
        g_prefs.putBool("hiPower", g_settings.highPowerMode);
        g_prefs.putUChar("fcstHours", g_settings.forecastHours);
        g_prefs.putUInt("wxTlSun", g_settings.wxTimelineSunny);
        g_prefs.putUInt("wxTlCld", g_settings.wxTimelineCloudy);
        g_prefs.putUInt("wxTlRai", g_settings.wxTimelineRain);
        g_prefs.putUInt("wxTlSto", g_settings.wxTimelineStorm);
        g_prefs.putUInt("wxTlSno", g_settings.wxTimelineSnow);
        g_prefs.putUInt("wxTlWin", g_settings.wxTimelineWind);
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
        g_prefs.putBytes("presetEn16", g_settings.presetEnabled, 128); // 16 * 8 bytes
        
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
