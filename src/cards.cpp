#include <Arduino.h>
#include <time.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <math.h>
#include <limits.h>
#include "weatherthing_hw.h"
#include "net.h"
#include "cards.h"
#include "weather.h"
#include "sprites.h"
#include "settings.h"
#include "mqtt.h"

struct Card
{
    void (*setup)();
    void (*update)(uint32_t now, uint32_t dt);
    void (*render)();
};

static void diag_setup();
static void diag_update(uint32_t now, uint32_t dt);
static void diag_render();

static void clock_setup();
static void clock_update(uint32_t now, uint32_t dt);
static void clock_render();

static void vu_setup();
static void vu_update(uint32_t now, uint32_t dt);
static void vu_render();

static void test_setup();
static void test_update(uint32_t now, uint32_t dt);
static void test_render();

static void weather_setup();
static void weather_update(uint32_t now, uint32_t dt);
static void weather_render();

static void ticker_setup();
static void ticker_update(uint32_t now, uint32_t dt);
static void ticker_render();

static void stock_setup();
static void stock_update(uint32_t now, uint32_t dt);
static void stock_render();

static void games_setup();
static void games_update(uint32_t now, uint32_t dt);
static void games_render();

static void sparkle_setup();
static void sparkle_update(uint32_t now, uint32_t dt);
static void sparkle_render();

static void aurora_setup();
static void aurora_update(uint32_t now, uint32_t dt);
static void aurora_render();

static void netcard_setup();
static void netcard_update(uint32_t now, uint32_t dt);
static void netcard_render();

static void mqttcard_setup();
static void mqttcard_update(uint32_t now, uint32_t dt);
static void mqttcard_render();

static void rss_setup();
static void rss_update(uint32_t now, uint32_t dt);
static void rss_render();

// Social media cards
static void youtube_setup();
static void youtube_update(uint32_t now, uint32_t dt);
static void youtube_render();

static void twitch_setup();
static void twitch_update(uint32_t now, uint32_t dt);
static void twitch_render();

static void twitter_setup();
static void twitter_update(uint32_t now, uint32_t dt);
static void twitter_render();

static void insta_setup();
static void insta_update(uint32_t now, uint32_t dt);
static void insta_render();

static void tiktok_setup();
static void tiktok_update(uint32_t now, uint32_t dt);
static void tiktok_render();

static void drawDigit(uint8_t x, uint8_t y, uint8_t d, uint32_t color);
static uint32_t weatherColor(uint8_t type);

// Boot animation state
static uint8_t g_bootPhase = 0;       // 0=draw comet, 1=hold, 2=fade, 3=done
static uint32_t g_bootStart = 0;
static bool g_bootDone = false;
static const uint8_t BOOT_TRAIL_LEN = 32;
static int8_t g_bootTrailX[BOOT_TRAIL_LEN];
static int8_t g_bootTrailY[BOOT_TRAIL_LEN];
static uint8_t g_bootTrailCount = 0;

// WiFi notification
static bool g_wifiNotifyPending = false;
static uint32_t g_wifiNotifyStart = 0;
static char g_wifiIP[20] = "";
static int16_t g_scrollX = 20;

// Weather/VU/Clock presets
static uint8_t g_weatherPreset = 0;  // 0=classic, 1=fullscreen
static uint8_t g_vuPreset = 0;       // 0=mirror, 1=bars, 2=dots
static uint8_t g_clockPreset = 0;    // 0=digital, 1=binary, 2=minimal, 3=bars

// Card order: Weather, Clock, BTC, Stock, Network, Audio, Sparkle, Aurora, Games, MQTT, RSS, Social
static Card g_cards[] = {
    {weather_setup, weather_update, weather_render},   // 0
    {clock_setup, clock_update, clock_render},         // 1
    {ticker_setup, ticker_update, ticker_render},      // 2 (BTC)
    {stock_setup, stock_update, stock_render},         // 3
    {netcard_setup, netcard_update, netcard_render},   // 4 (Network)
    {vu_setup, vu_update, vu_render},                  // 5 (Audio VU)
    {sparkle_setup, sparkle_update, sparkle_render},   // 6
    {aurora_setup, aurora_update, aurora_render},      // 7
    {games_setup, games_update, games_render},         // 8 (Games)
    {mqttcard_setup, mqttcard_update, mqttcard_render}, // 9 (MQTT/Home Assistant)
    {rss_setup, rss_update, rss_render},               // 10 (RSS)
    {youtube_setup, youtube_update, youtube_render},   // 11 (YouTube)
    {twitch_setup, twitch_update, twitch_render},      // 12 (Twitch)
    {twitter_setup, twitter_update, twitter_render},   // 13 (Twitter/X)
    {insta_setup, insta_update, insta_render},         // 14 (Instagram)
    {tiktok_setup, tiktok_update, tiktok_render}       // 15 (TikTok)
};

// Card indices for reference
static const uint8_t CARD_WEATHER = 0;
static const uint8_t CARD_CLOCK = 1;
static const uint8_t CARD_BTC = 2;
static const uint8_t CARD_STOCK = 3;
static const uint8_t CARD_NETWORK = 4;
static const uint8_t CARD_VU = 5;
static const uint8_t CARD_SPARKLE = 6;
static const uint8_t CARD_AURORA = 7;
static const uint8_t CARD_GAMES = 8;
static const uint8_t CARD_MQTT = 9;
static const uint8_t CARD_RSS = 10;
static const uint8_t CARD_YOUTUBE = 11;
static const uint8_t CARD_TWITCH = 12;
static const uint8_t CARD_TWITTER = 13;
static const uint8_t CARD_INSTA = 14;
static const uint8_t CARD_TIKTOK = 15;

static const uint8_t g_cardCount = sizeof(g_cards) / sizeof(g_cards[0]);
static uint8_t g_currentCard = 0;
static uint32_t g_lastTick = 0;
static bool g_lastBtn1 = false;
static bool g_lastBtn2 = false;

// Card names for title animation (short single words)
static const char* g_cardNames[] = {
    "Weather",   // 0
    "Clock",     // 1
    "Bitcoin",   // 2
    "Stocks",    // 3
    "Network",   // 4
    "MIC",       // 5
    "Sparkle",   // 6
    "Aurora",    // 7
    "Games",     // 8
    "MQTT",      // 9
    "RSS",       // 10
    "YouTube",   // 11
    "Twitch",    // 12
    "Twitter",   // 13
    "Insta",     // 14
    "TikTok"     // 15
};

// Which cards are "musical" (show note icon) - VU (5), Sparkle (6), Aurora (7)
static const bool g_cardMusical[] = {
    false, false, false, false, false, true, true, true, false, false, false,
    false, false, false, false, false  // Social cards not musical
};

// Title animation state
static bool g_showingTitle = false;
static uint32_t g_titleStartTime = 0;
static const uint32_t TITLE_DURATION_MS = 2000;  // Show title for 2 seconds
static int16_t g_titleScrollX = 0;
static uint8_t g_transitionEffect = 0;  // Cycles through effects

// Transition particle system
struct TransParticle {
    float x, y;
    float vx, vy;
    uint8_t life;
    uint32_t color;
};
static const uint8_t TRANS_PARTICLES = 20;
static TransParticle g_transParticles[TRANS_PARTICLES];

static uint16_t g_diagLedStep = 0;
static uint32_t g_diagLastPrint = 0;
static uint32_t g_diagLastStep = 0;

static uint32_t g_clockLastUpdate = 0;
static bool g_clockTimeValid = false;
static tm g_clockTime;

// VU meter state
static const uint8_t VU_SAMPLES = 64;
static int16_t g_vuSamples[VU_SAMPLES];
static uint8_t g_vuBars[WT_MATRIX_WIDTH];
static uint8_t g_vuPeaks[WT_MATRIX_WIDTH];
static uint8_t g_vuPeakHold[WT_MATRIX_WIDTH];
static uint16_t g_vuNoiseFloor = 250;  // High noise gate for modified circuit
static uint16_t g_vuPeakLevel = 0;     // AGC peak tracker
static uint8_t g_vuSilenceFrames = 0;

// Ticker card state (BTC and Stock)
static uint32_t g_tickerLastFetch = 0;
static float g_tickerPrice = 0;
static float g_tickerPrevPrice = 0;  // For price change tracking
static bool g_tickerValid = false;
static int8_t g_tickerChange = 0;    // -1=down, 0=same, 1=up

// Stock ticker state
static uint32_t g_stockLastFetch = 0;
static float g_stockPrice = 0;
static float g_stockPrevPrice = 0;
static bool g_stockValid = false;
static int8_t g_stockChange = 0;

// Weather animation state
static uint32_t g_weatherAnimFrame = 0;
static uint32_t g_weatherLastAnim = 0;

// Global sound-reactive state
static bool g_soundReactive = false;
static uint8_t g_audioLevel = 0;      // 0-255 envelope
static uint32_t g_lastAudioSample = 0;
static uint32_t g_soundToggleFlashUntil = 0;
static uint32_t g_btn2HoldStart = 0;

// Sparkle card state
static const uint8_t SPARKLE_MAX = 24;
struct Sparkle {
    uint8_t x, y;
    uint8_t life;
    uint8_t hue;
};
static Sparkle g_sparkles[SPARKLE_MAX];
static uint8_t g_sparkleMode = 0;  // 0=random, 1=wave, 2=rain

// Aurora card state
static uint32_t g_plasmaOffset = 0;

static void test_setup();
static void test_update(uint32_t now, uint32_t dt);
static void test_render();

static uint32_t colorWheel(uint8_t pos)
{
    pos = 255 - pos;
    if (pos < 85)
    {
        return wt_color(255 - pos * 3, 0, pos * 3);
    }
    if (pos < 170)
    {
        pos -= 85;
        return wt_color(0, pos * 3, 255 - pos * 3);
    }
    pos -= 170;
    return wt_color(pos * 3, 255 - pos * 3, 0);
}

static const uint8_t DIGIT_W = 3;
static const uint8_t DIGIT_H = 7;

// 3x7 digit glyphs - clean, full-height numerals
static const uint8_t DIGITS[10][DIGIT_H] = {
    {0b111, 0b101, 0b101, 0b101, 0b101, 0b101, 0b111}, // 0
    {0b010, 0b110, 0b010, 0b010, 0b010, 0b010, 0b111}, // 1
    {0b111, 0b001, 0b001, 0b111, 0b100, 0b100, 0b111}, // 2
    {0b111, 0b001, 0b001, 0b111, 0b001, 0b001, 0b111}, // 3
    {0b101, 0b101, 0b101, 0b111, 0b001, 0b001, 0b001}, // 4
    {0b111, 0b100, 0b100, 0b111, 0b001, 0b001, 0b111}, // 5
    {0b111, 0b100, 0b100, 0b111, 0b101, 0b101, 0b111}, // 6
    {0b111, 0b001, 0b001, 0b010, 0b010, 0b100, 0b100}, // 7
    {0b111, 0b101, 0b101, 0b111, 0b101, 0b101, 0b111}, // 8
    {0b111, 0b101, 0b101, 0b111, 0b001, 0b001, 0b111}  // 9
};

// 5x7 Bold Digits for minimal display
static const uint8_t BIG_DIGITS[10][7] = {
    {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}, // 0
    {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}, // 1
    {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111}, // 2
    {0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110}, // 3
    {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010}, // 4
    {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110}, // 5
    {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110}, // 6
    {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000}, // 7
    {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110}, // 8
    {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100}  // 9
};

// Draw Big Digit (5x7)
static void drawBigDigit(uint8_t x, uint8_t y, uint8_t d, uint32_t color) {
    if (d > 9) return;
    for (uint8_t row = 0; row < 7; ++row) {
        uint8_t bits = BIG_DIGITS[d][row];
        for (uint8_t col = 0; col < 5; ++col) {
            if (bits & (1 << (4 - col))) {
                 wt_display_set_pixel_xy(x + col, y + (6 - row), color);
            }
        }
    }
}

// Bitcoin ₿ icon - clear B with vertical stripes through top/bottom
static void drawBitcoinIcon(uint8_t x, uint8_t y, uint32_t color)
{
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    uint32_t bright = wt_color(r, g, b);
    uint32_t dim = wt_color(r * 2/3, g * 2/3, b * 2/3);
    
    // The Bitcoin B design (6 wide x 7 tall):
    // Row 0 (top):     _||___ (vertical stripes extending up)
    // Row 1:           |###_  
    // Row 2:           |__#|
    // Row 3 (middle):  |###_
    // Row 4:           |__#|
    // Row 5:           |###_
    // Row 6 (bottom):  _||___ (vertical stripes extending down)
    
    // Vertical stripes through top (extend beyond B)
    wt_display_set_pixel_xy(x+1, y+6, dim);
    wt_display_set_pixel_xy(x+2, y+6, dim);
    
    // Top of B
    wt_display_set_pixel_xy(x+0, y+5, bright);
    wt_display_set_pixel_xy(x+1, y+5, bright);
    wt_display_set_pixel_xy(x+2, y+5, bright);
    wt_display_set_pixel_xy(x+3, y+5, bright);
    
    // Upper curve
    wt_display_set_pixel_xy(x+0, y+4, bright);
    wt_display_set_pixel_xy(x+4, y+4, dim);
    
    // Middle bar
    wt_display_set_pixel_xy(x+0, y+3, bright);
    wt_display_set_pixel_xy(x+1, y+3, bright);
    wt_display_set_pixel_xy(x+2, y+3, bright);
    wt_display_set_pixel_xy(x+3, y+3, bright);
    
    // Lower curve
    wt_display_set_pixel_xy(x+0, y+2, bright);
    wt_display_set_pixel_xy(x+4, y+2, dim);
    
    // Bottom of B
    wt_display_set_pixel_xy(x+0, y+1, bright);
    wt_display_set_pixel_xy(x+1, y+1, bright);
    wt_display_set_pixel_xy(x+2, y+1, bright);
    wt_display_set_pixel_xy(x+3, y+1, bright);
    
    // Vertical stripes through bottom (extend beyond B)
    wt_display_set_pixel_xy(x+1, y+0, dim);
    wt_display_set_pixel_xy(x+2, y+0, dim);
}

// Compact weather icon drawing - shared cloud bitmap
static const uint8_t CLOUD_BITS[] PROGMEM = {
    0b00111110,  // y=4: columns 1-5
    0b01111111,  // y=5: columns 0-6
    0b00011100   // y=6: columns 2-4
};

static void drawCloudShape(uint8_t yBase, uint32_t col)
{
    for (uint8_t row = 0; row < 3; ++row)
    {
        uint8_t bits = pgm_read_byte(&CLOUD_BITS[row]);
        for (uint8_t x = 0; x < 7; ++x)
        {
            if (bits & (1 << (6 - x))) wt_display_set_pixel_xy(x, yBase + row, col);
        }
    }
}

// Aurora / plasma sound-reactive card
static void aurora_setup()
{
    g_plasmaOffset = 0;
}

static void aurora_update(uint32_t now, uint32_t dt)
{
    // Speed scales with audio level (more reactive)
    uint32_t speed = 40 + g_audioLevel / 2;
    g_plasmaOffset += dt * speed;
}

static void aurora_render()
{
    wt_display_clear();
    wt_timeline_clear();

    uint8_t palette = settings_get().vuPalette;
    float t = (float)(g_plasmaOffset % 65536u) / 4000.0f;
    float audioBoost = 0.4f + (float)g_audioLevel / 255.0f;

    for (uint8_t y = 0; y < WT_MATRIX_HEIGHT; ++y)
    {
        for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x)
        {
            float nx = (float)x - (WT_MATRIX_WIDTH - 1) * 0.5f;
            float ny = (float)y - (WT_MATRIX_HEIGHT - 1) * 0.5f;
            float wave = sinf((nx * 0.45f) + t) + cosf((ny * 0.55f) - t * 1.3f);
            wave += sinf((nx + ny) * 0.25f + t * 0.7f);
            wave *= audioBoost;
            float norm = (wave + 3.0f) * (1.0f / 6.0f);
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;
            uint8_t palPos = (uint8_t)(norm * 255.0f);
            // Use palette colors for aurora
            uint32_t col = settings_palette_color(palette, palPos, 255);
            wt_display_set_pixel_xy(x, y, col);
        }
    }

    // Timeline displays audio-driven gradient using palette
    for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i)
    {
        float pos = (float)i / (WT_TIMELINE_PIXELS - 1);
        uint8_t palPos = (uint8_t)(pos * 255);
        wt_timeline_set_pixel(i, settings_palette_color(palette, palPos, 180));
    }
}

static uint32_t g_netcardLastScroll = 0;

static void netcard_setup()
{
    g_netcardLastScroll = millis();
    g_scrollX = WT_MATRIX_WIDTH;
}

static void netcard_update(uint32_t now, uint32_t dt)
{
    (void)dt;
    if (g_wifiIP[0] == '\0')
    {
        return;
    }

    if (now - g_netcardLastScroll > 180)
    {
        g_netcardLastScroll = now;
        int16_t totalWidth = (int16_t)strlen(g_wifiIP) * 4;
        g_scrollX--;
        if (g_scrollX < -totalWidth)
        {
            g_scrollX = WT_MATRIX_WIDTH;
        }
    }
}

static void netcard_render()
{
    wt_display_clear();
    wt_timeline_clear();

    bool hasIp = (g_wifiIP[0] != '\0');
    uint32_t now = millis();

    // Animated timeline - gradient pulse
    for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i)
    {
        uint8_t phase = (now / 50 + i * 20) % 256;
        uint8_t bright = 80 + (phase < 128 ? phase : 255 - phase) / 2;
        
        if (hasIp) {
            wt_timeline_set_pixel(i, wt_color(0, bright, bright / 2));
        } else {
            wt_timeline_set_pixel(i, wt_color(bright, bright / 4, 0));
        }
    }

    // Draw WiFi icon (left side) - clean curved arcs
    uint8_t wx = 1, wy = 0;
    uint32_t wifiMain = hasIp ? wt_color(0, 220, 180) : wt_color(255, 100, 50);
    uint32_t wifiDim = hasIp ? wt_color(0, 80, 60) : wt_color(100, 40, 0);
    
    // Animated arcs (bottom to top)
    uint8_t arcPhase = (now / 350) % 4;
    
    // Bottom dot (antenna base - always on)
    wt_display_set_pixel_xy(wx + 2, wy + 0, wifiMain);
    wt_display_set_pixel_xy(wx + 2, wy + 1, wifiMain);
    
    // Arc 1 (smallest)
    uint32_t arc1 = (arcPhase >= 1) ? wifiMain : wifiDim;
    wt_display_set_pixel_xy(wx + 1, wy + 2, arc1);
    wt_display_set_pixel_xy(wx + 3, wy + 2, arc1);
    
    // Arc 2 (medium)
    uint32_t arc2 = (arcPhase >= 2) ? wifiMain : wifiDim;
    wt_display_set_pixel_xy(wx + 0, wy + 3, arc2);
    wt_display_set_pixel_xy(wx + 1, wy + 4, arc2);
    wt_display_set_pixel_xy(wx + 3, wy + 4, arc2);
    wt_display_set_pixel_xy(wx + 4, wy + 3, arc2);
    
    // Arc 3 (largest)
    uint32_t arc3 = (arcPhase >= 3) ? wifiMain : wifiDim;
    wt_display_set_pixel_xy(wx + 0, wy + 5, arc3);
    wt_display_set_pixel_xy(wx + 1, wy + 6, arc3);
    wt_display_set_pixel_xy(wx + 3, wy + 6, arc3);
    wt_display_set_pixel_xy(wx + 4, wy + 5, arc3);

    if (!hasIp)
    {
        // Animated sad face on right side
        uint8_t fx = 12, fy = 0;
        uint8_t blinkPhase = (now / 100) % 30;
        bool blink = blinkPhase < 2;
        
        // Face outline (yellow-orange)
        uint32_t faceCol = wt_color(255, 180, 50);
        uint32_t faceDim = wt_color(180, 100, 20);
        
        // Top of head
        wt_display_set_pixel_xy(fx + 2, fy + 6, faceCol);
        wt_display_set_pixel_xy(fx + 3, fy + 6, faceCol);
        wt_display_set_pixel_xy(fx + 4, fy + 6, faceCol);
        // Sides
        wt_display_set_pixel_xy(fx + 1, fy + 5, faceCol);
        wt_display_set_pixel_xy(fx + 5, fy + 5, faceCol);
        wt_display_set_pixel_xy(fx + 1, fy + 4, faceCol);
        wt_display_set_pixel_xy(fx + 5, fy + 4, faceCol);
        wt_display_set_pixel_xy(fx + 1, fy + 3, faceCol);
        wt_display_set_pixel_xy(fx + 5, fy + 3, faceCol);
        wt_display_set_pixel_xy(fx + 1, fy + 2, faceCol);
        wt_display_set_pixel_xy(fx + 5, fy + 2, faceCol);
        // Bottom
        wt_display_set_pixel_xy(fx + 2, fy + 1, faceCol);
        wt_display_set_pixel_xy(fx + 3, fy + 1, faceCol);
        wt_display_set_pixel_xy(fx + 4, fy + 1, faceCol);
        
        // Eyes (blink occasionally)
        if (!blink) {
            wt_display_set_pixel_xy(fx + 2, fy + 4, wt_color(255, 255, 255));
            wt_display_set_pixel_xy(fx + 4, fy + 4, wt_color(255, 255, 255));
        }
        
        // Frown
        wt_display_set_pixel_xy(fx + 2, fy + 2, faceDim);
        wt_display_set_pixel_xy(fx + 3, fy + 3, faceDim);
        wt_display_set_pixel_xy(fx + 4, fy + 2, faceDim);
        
        return;
    }

    // Scrolling IP address with nice gradient
    uint32_t textBright = wt_color(255, 255, 255);
    uint32_t textDim = wt_color(150, 200, 255);
    int16_t textX = g_scrollX;
    uint8_t textY = 0;

    for (uint8_t i = 0; g_wifiIP[i] != '\0' && i < 16; ++i)
    {
        char c = g_wifiIP[i];
        int16_t cx = textX + (int16_t)i * 4;

        if (cx < -3 || cx >= (int16_t)WT_MATRIX_WIDTH) continue;

        // Fade at edges
        uint32_t textCol = textBright;
        if (cx < 8) textCol = textDim;
        else if (cx > WT_MATRIX_WIDTH - 5) {
            uint8_t fade = (WT_MATRIX_WIDTH - cx) * 50;
            // Clamp to 255 to avoid overflow wrapping
            uint8_t r = (uint8_t)min(255, (int)fade + 100);
            uint8_t g = (uint8_t)min(255, (int)fade + 150);
            uint8_t b = (uint8_t)min(255, (int)fade + 200);
            textCol = wt_color(r, g, b);
        }

        if (c == '.')
        {
            if (cx >= 7 && cx < (int16_t)WT_MATRIX_WIDTH)
                wt_display_set_pixel_xy((uint8_t)cx + 1, textY, textCol);
            continue;
        }

        if (c >= '0' && c <= '9' && cx >= 7)
        {
            drawDigit((uint8_t)cx, textY, (uint8_t)(c - '0'), textCol);
        }
    }
}

// Aurora / plasma card (sound reactive)

// Beautiful animated weather icons with polished effects
// NOTE: Y=0 is BOTTOM of display, Y increases upward
static void drawWeatherIcon(int8_t x, int8_t y, uint8_t type, uint8_t frame)
{
    uint32_t now = millis();
    
    // Helper: draw animated cloud with morphing bumps - classic fluffy cloud shape
    auto drawAnimCloud = [&](uint8_t baseY, uint32_t col, uint32_t darkCol) {
        float t = now * 0.001f;
        float morph1 = sinf(t);
        float morph2 = sinf(t * 1.3f + 2.0f);
        float morph3 = sinf(t * 0.7f + 4.0f);
        
        // Cloud base (flat bottom) - row baseY
        for (int i = 1; i < 7; ++i) {
            wt_display_set_pixel_xy(x + i, y + baseY, col);
        }
        // Middle body - row baseY+1
        for (int i = 0; i < 8; ++i) {
            wt_display_set_pixel_xy(x + i, y + baseY + 1, col);
        }
        // Upper body with bumps - row baseY+2
        int bump1x = 1 + (int)(morph1 * 0.4f);
        int bump2x = 4 + (int)(morph2 * 0.4f);
        int bump3x = 6 + (int)(morph3 * 0.3f);
        wt_display_set_pixel_xy(x + bump1x, y + baseY + 2, col);
        wt_display_set_pixel_xy(x + bump1x + 1, y + baseY + 2, col);
        wt_display_set_pixel_xy(x + bump2x, y + baseY + 2, col);
        wt_display_set_pixel_xy(x + bump2x + 1, y + baseY + 2, col);
        wt_display_set_pixel_xy(x + bump3x, y + baseY + 2, col);
        // Top puff - row baseY+3 (morphing)
        int topX = 2 + (int)(morph2 * 0.5f);
        wt_display_set_pixel_xy(x + topX, y + baseY + 3, col);
        wt_display_set_pixel_xy(x + topX + 1, y + baseY + 3, col);
        wt_display_set_pixel_xy(x + topX + 2, y + baseY + 3, col);
        // Shading
        wt_display_set_pixel_xy(x + 0, y + baseY + 1, darkCol);
        wt_display_set_pixel_xy(x + 7, y + baseY + 1, darkCol);
    };
    
    switch(type) {
        case WEATHER_SUNNY: {
            // Sun with smooth pulsing halo - centered
            float cx = x + 3.0f, cy = y + 3.0f;
            float pulse = 0.9f + 0.1f * sinf(now * 0.002f);
            
            // Ultra-low brightness halo (smooth glow around sun)
            uint32_t haloCol = wt_color(30, 20, 0);
            for (int8_t dy = -3; dy <= 3; ++dy) {
                for (int8_t dx = -3; dx <= 3; ++dx) {
                    float dist = sqrtf(dx*dx + dy*dy);
                    if (dist > 1.8f && dist < 3.5f) {
                        int8_t px = (int8_t)cx + dx;
                        int8_t py = (int8_t)cy + dy;
                        if (px >= 0 && px < WT_MATRIX_WIDTH && py >= 0 && py < WT_MATRIX_HEIGHT) {
                            float fade = 1.0f - (dist - 1.8f) / 1.7f;
                            uint8_t hb = (uint8_t)(25 * fade * pulse);
                            wt_display_set_pixel_xy(px, py, wt_color(hb, (uint8_t)(hb * 0.7f), 0));
                        }
                    }
                }
            }
            
            // Sun core (2x2 bright)
            uint32_t sunCore = wt_color((uint8_t)(255 * pulse), (uint8_t)(200 * pulse), 0);
            wt_display_set_pixel_xy(x + 2, y + 3, sunCore);
            wt_display_set_pixel_xy(x + 3, y + 3, sunCore);
            wt_display_set_pixel_xy(x + 2, y + 4, sunCore);
            wt_display_set_pixel_xy(x + 3, y + 4, sunCore);
            
            // Inner glow ring
            uint32_t innerGlow = wt_color((uint8_t)(200 * pulse), (uint8_t)(150 * pulse), 0);
            wt_display_set_pixel_xy(x + 1, y + 3, innerGlow);
            wt_display_set_pixel_xy(x + 4, y + 3, innerGlow);
            wt_display_set_pixel_xy(x + 1, y + 4, innerGlow);
            wt_display_set_pixel_xy(x + 4, y + 4, innerGlow);
            wt_display_set_pixel_xy(x + 2, y + 2, innerGlow);
            wt_display_set_pixel_xy(x + 3, y + 2, innerGlow);
            wt_display_set_pixel_xy(x + 2, y + 5, innerGlow);
            wt_display_set_pixel_xy(x + 3, y + 5, innerGlow);
            break;
        }
        
        case WEATHER_CLEAR_NIGHT: {
            // Crescent moon - proper C shape facing right
            float pulse = 0.92f + 0.08f * sinf(now * 0.0015f);
            uint32_t moonBright = wt_color((uint8_t)(255 * pulse), (uint8_t)(250 * pulse), (uint8_t)(200 * pulse));
            uint32_t moonMid = wt_color((uint8_t)(180 * pulse), (uint8_t)(175 * pulse), (uint8_t)(140 * pulse));
            uint32_t moonDim = wt_color((uint8_t)(100 * pulse), (uint8_t)(95 * pulse), (uint8_t)(70 * pulse));
            uint32_t moonGlow = wt_color(20, 20, 15);
            
            // Outer glow ring
            wt_display_set_pixel_xy(x + 1, y + 6, moonGlow);
            wt_display_set_pixel_xy(x + 0, y + 4, moonGlow);
            wt_display_set_pixel_xy(x + 0, y + 3, moonGlow);
            wt_display_set_pixel_xy(x + 1, y + 1, moonGlow);
            
            // Crescent moon shape (C facing right, hollow in center-right)
            // Top arc
            wt_display_set_pixel_xy(x + 2, y + 6, moonMid);
            wt_display_set_pixel_xy(x + 3, y + 6, moonBright);
            wt_display_set_pixel_xy(x + 4, y + 5, moonDim);
            // Left edge (bright part)
            wt_display_set_pixel_xy(x + 1, y + 5, moonBright);
            wt_display_set_pixel_xy(x + 1, y + 4, moonBright);
            wt_display_set_pixel_xy(x + 1, y + 3, moonBright);
            wt_display_set_pixel_xy(x + 1, y + 2, moonBright);
            // Bottom arc
            wt_display_set_pixel_xy(x + 2, y + 1, moonMid);
            wt_display_set_pixel_xy(x + 3, y + 1, moonBright);
            wt_display_set_pixel_xy(x + 4, y + 2, moonDim);
            
            // Twinkling stars
            uint8_t tw1 = 30 + (uint8_t)(40 * sinf(now * 0.003f));
            uint8_t tw2 = 25 + (uint8_t)(35 * sinf(now * 0.0035f + 1.5f));
            uint8_t tw3 = 20 + (uint8_t)(30 * sinf(now * 0.004f + 3.0f));
            wt_display_set_pixel_xy(x + 7, y + 5, wt_color(tw1, tw1, tw1));
            wt_display_set_pixel_xy(x + 6, y + 2, wt_color(tw2, tw2, tw2));
            wt_display_set_pixel_xy(x + 8, y + 3, wt_color(tw3, tw3, tw3));
            break;
        }
        
        case WEATHER_PARTLY_CLOUDY: {
            // Sun in top-right with animated shine rays, cloud in foreground
            float pulse = 0.85f + 0.15f * sinf(now * 0.002f);
            float rayPulse = sinf(now * 0.003f);
            
            // Animated shine rays (very low brightness, expanding outward)
            uint8_t rayB = 12 + (uint8_t)(8 * rayPulse);
            uint32_t rayCol = wt_color(rayB, (uint8_t)(rayB * 0.7f), 0);
            // Diagonal rays that pulse
            wt_display_set_pixel_xy(x + 4, y + 6, rayCol);
            wt_display_set_pixel_xy(x + 7, y + 6, rayCol);
            wt_display_set_pixel_xy(x + 8, y + 5, rayCol);
            wt_display_set_pixel_xy(x + 8, y + 4, rayCol);
            wt_display_set_pixel_xy(x + 7, y + 3, rayCol);
            // Second pulse layer
            uint8_t ray2B = 8 + (uint8_t)(6 * sinf(now * 0.0025f + 1.0f));
            uint32_t ray2Col = wt_color(ray2B, (uint8_t)(ray2B * 0.6f), 0);
            wt_display_set_pixel_xy(x + 3, y + 6, ray2Col);
            wt_display_set_pixel_xy(x + 8, y + 6, ray2Col);
            
            // Sun core (2x2 bright)
            uint32_t sunCore = wt_color((uint8_t)(255 * pulse), (uint8_t)(190 * pulse), 0);
            uint32_t sunGlow = wt_color((uint8_t)(200 * pulse), (uint8_t)(140 * pulse), 0);
            wt_display_set_pixel_xy(x + 5, y + 5, sunCore);
            wt_display_set_pixel_xy(x + 6, y + 5, sunCore);
            wt_display_set_pixel_xy(x + 5, y + 4, sunCore);
            wt_display_set_pixel_xy(x + 6, y + 4, sunCore);
            // Inner glow around sun
            wt_display_set_pixel_xy(x + 4, y + 5, sunGlow);
            wt_display_set_pixel_xy(x + 7, y + 5, sunGlow);
            wt_display_set_pixel_xy(x + 4, y + 4, sunGlow);
            wt_display_set_pixel_xy(x + 7, y + 4, sunGlow);
            wt_display_set_pixel_xy(x + 5, y + 6, sunGlow);
            wt_display_set_pixel_xy(x + 6, y + 6, sunGlow);
            
            // Cloud (foreground, lower) - drawn last to overlap sun
            uint32_t cloudCol = wt_color(190, 190, 210);
            drawAnimCloud(0, cloudCol, wt_color(150, 150, 170));
            break;
        }
        
        case WEATHER_CLOUDY: {
            // Dark rain cloud - bluish-grey for threatening overcast weather
            float pulse = 0.9f + 0.1f * sinf(now * 0.0015f);
            uint8_t br = (uint8_t)(95 * pulse);  // Darker base for rain clouds
            // Blue-grey tint: less red, more blue for that ominous look
            uint32_t cloudCol = wt_color((uint8_t)(br * 0.7f), (uint8_t)(br * 0.75f), (uint8_t)(br + 35));
            uint32_t darkCol = wt_color((uint8_t)(br * 0.5f), (uint8_t)(br * 0.55f), (uint8_t)(br + 15));
            drawAnimCloud(3, cloudCol, darkCol);
            break;
        }
        
        case WEATHER_FOG: {
            // Gradient fog: dense at bottom, fading up
            for (int row = 0; row < 7; ++row) {
                // Denser at bottom (low Y), fading toward top
                float density = 1.0f - (row / 7.0f);
                float drift = sinf(now * 0.0008f + row * 0.3f) * 1.5f;
                
                for (int col = 0; col < 8; ++col) {
                    float colDrift = sinf(now * 0.001f + col * 0.5f + row);
                    uint8_t br = (uint8_t)(120 * density * (0.7f + 0.3f * colDrift));
                    if (br > 10) {
                        int8_t px = x + col + (int8_t)drift;
                        if (px >= 0 && px < WT_MATRIX_WIDTH) {
                            wt_display_set_pixel_xy(px, y + row, wt_color(br, br, (uint8_t)(br + 15)));
                        }
                    }
                }
            }
            break;
        }
        
        case WEATHER_DRIZZLE:
        case WEATHER_RAIN:
        case WEATHER_HEAVY_RAIN: {
            // Bluish animated cloud at top
            uint8_t blueShift = (type == WEATHER_HEAVY_RAIN) ? 40 : 20;
            uint32_t cloudCol = wt_color(90, 90, 120 + blueShift);
            uint32_t darkCol = wt_color(60, 60, 90 + blueShift);
            drawAnimCloud(4, cloudCol, darkCol);
            
            // Rain drops with trails - falling DOWN (decreasing Y)
            uint8_t dropCount = (type == WEATHER_DRIZZLE) ? 3 : (type == WEATHER_RAIN) ? 5 : 7;
            
            for (uint8_t d = 0; d < dropCount; ++d) {
                int8_t dropX = x + (d * 11 + 3) % 7;
                // Drops fall from top (y+3) to bottom (y+0)
                int8_t dropY = y + 3 - ((now / (40 + d * 10) + d * 17) % 5);
                
                // Bright drop head
                uint32_t headCol = wt_color(120, 180, 255);
                uint32_t trailCol = wt_color(60, 100, 200);
                uint32_t trailDim = wt_color(30, 50, 120);
                
                if (dropY >= y && dropY <= y + 3) {
                    wt_display_set_pixel_xy(dropX, dropY, headCol);
                    // Trail above the drop (where it came from)
                    if (dropY + 1 <= y + 3) {
                        wt_display_set_pixel_xy(dropX, dropY + 1, trailCol);
                    }
                    if (type != WEATHER_DRIZZLE && dropY + 2 <= y + 3) {
                        wt_display_set_pixel_xy(dropX, dropY + 2, trailDim);
                    }
                }
                // Splash at bottom
                if (dropY == y && type == WEATHER_HEAVY_RAIN) {
                    wt_display_set_pixel_xy(dropX - 1, y, wt_color(40, 60, 100));
                    wt_display_set_pixel_xy(dropX + 1, y, wt_color(40, 60, 100));
                }
            }
            break;
        }
        
        case WEATHER_STORM: {
            // Very dark blue/purple cloud
            uint32_t darkCloud = wt_color(40, 35, 80);
            uint32_t darkerCloud = wt_color(25, 20, 60);
            drawAnimCloud(4, darkCloud, darkerCloud);
            
            // Lightning bolt
            bool flash = (frame % 40) < 3;
            if (flash) {
                uint32_t boltCol = wt_color(255, 255, 200);
                wt_display_set_pixel_xy(x + 3, y + 3, boltCol);
                wt_display_set_pixel_xy(x + 4, y + 3, boltCol);
                wt_display_set_pixel_xy(x + 3, y + 2, boltCol);
                wt_display_set_pixel_xy(x + 2, y + 1, boltCol);
                wt_display_set_pixel_xy(x + 3, y + 1, boltCol);
                wt_display_set_pixel_xy(x + 2, y + 0, boltCol);
            } else {
                // Rain
                uint32_t rainCol = wt_color(70, 70, 180);
                for (uint8_t d = 0; d < 3; ++d) {
                    int8_t dropX = x + 1 + d * 2;
                    int8_t dropY = y + 3 - ((frame / 3 + d * 3) % 4);
                    if (dropY >= y && dropY <= y + 3) {
                        wt_display_set_pixel_xy(dropX, dropY, rainCol);
                    }
                }
            }
            break;
        }
        
        case WEATHER_SNOW: {
            // Light grey cloud
            uint32_t cloudCol = wt_color(150, 150, 165);
            drawAnimCloud(4, cloudCol, wt_color(120, 120, 135));
            
            // Drifting snowflakes with gentle sway - falling DOWN
            for (uint8_t f = 0; f < 6; ++f) {
                float drift = sinf(now * 0.002f + f * 1.5f) * 1.5f;
                int8_t flakeX = x + (f * 13 + 2) % 7 + (int8_t)drift;
                // Snowflakes fall slowly from top to bottom
                int8_t flakeY = y + 3 - ((now / (120 + f * 20) + f * 11) % 5);
                
                if (flakeX >= 0 && flakeX < WT_MATRIX_WIDTH && flakeY >= y && flakeY <= y + 3) {
                    // Bright snowflake
                    wt_display_set_pixel_xy(flakeX, flakeY, wt_color(255, 255, 255));
                    // Subtle sparkle trail
                    if (flakeY + 1 <= y + 3) {
                        uint8_t sparkle = 60 + (uint8_t)(40 * sinf(now * 0.01f + f));
                        wt_display_set_pixel_xy(flakeX, flakeY + 1, wt_color(sparkle, sparkle, sparkle + 20));
                    }
                }
            }
            break;
        }
        
        case WEATHER_SLEET: {
            // Blue-grey cloud for sleet
            uint32_t cloudCol = wt_color(120, 130, 170);
            drawAnimCloud(4, cloudCol, wt_color(90, 100, 140));
            
            // Mix of blue drops and white flakes
            for (uint8_t f = 0; f < 4; ++f) {
                float drift = sinf(now * 0.002f + f) * 1.0f;
                int8_t px = x + 1 + (f * 3 / 2) % 5 + (int8_t)drift;
                int8_t py = y + 3 - ((frame / 5 + f * 3) % 4);
                
                if (px >= 0 && px < WT_MATRIX_WIDTH && py >= y && py <= y + 3) {
                    uint32_t col = (f % 2) ? wt_color(255, 255, 255) : wt_color(100, 150, 255);
                    wt_display_set_pixel_xy(px, py, col);
                }
            }
            break;
        }
        
        case WEATHER_WIND: {
            // Curved wind lines flowing right, clear representation
            uint32_t windMain = wt_color(150, 200, 220);
            uint32_t windMid = wt_color(80, 130, 150);
            uint32_t windDim = wt_color(30, 60, 80);
            
            // Three horizontal wind streams at different speeds
            int speeds[] = {3, 2, 4}; // Different speeds per row
            int rows[] = {2, 4, 5};
            
            for (int s = 0; s < 3; ++s) {
                int row = rows[s];
                int offset = (frame / speeds[s]) % 10;
                
                for (int col = 0; col < 8; ++col) {
                    int8_t px = x + col;
                    int pos = (col + offset) % 8;
                    
                    if (pos < 4) {
                        uint32_t c = (pos == 0) ? windMain : (pos == 1) ? windMid : windDim;
                        wt_display_set_pixel_xy(px, y + row, c);
                    }
                }
            }
            
            // Swirl hint
            float swirl = sinf(now * 0.003f);
            int8_t swirlY = y + 3 + (int8_t)(swirl * 1.5f);
            int8_t swirlX = x + 2 + ((frame / 4) % 6);
            if (swirlX < WT_MATRIX_WIDTH && swirlY >= 0 && swirlY < WT_MATRIX_HEIGHT) {
                wt_display_set_pixel_xy(swirlX, swirlY, windMid);
            }
            break;
        }
        
        default: {
            // Fallback: simple cloud
            uint32_t cloudCol = wt_color(150, 150, 160);
            drawAnimCloud(3, cloudCol, wt_color(120, 120, 130));
            break;
        }
    }
}

static void drawDigit(uint8_t x, uint8_t y, uint8_t d, uint32_t color)
{
    if (d > 9)
    {
        return;
    }

    for (uint8_t row = 0; row < DIGIT_H; ++row)
    {
        uint8_t bits = sprites_get_digit_row(d, row);
        for (uint8_t col = 0; col < DIGIT_W; ++col)
        {
            bool on = bits & (1 << (DIGIT_W - 1 - col));
            if (on)
            {
                uint8_t yy = y + (DIGIT_H - 1 - row);
                wt_display_set_pixel_xy(x + col, yy, color);
            }
        }
    }
}

// Full height 4x7 font - wider letters for better readability
static const uint8_t FONT_4X7[][7] = {
    {0b0110, 0b1001, 0b1001, 0b1111, 0b1001, 0b1001, 0b1001}, // A
    {0b1110, 0b1001, 0b1001, 0b1110, 0b1001, 0b1001, 0b1110}, // B
    {0b0111, 0b1000, 0b1000, 0b1000, 0b1000, 0b1000, 0b0111}, // C
    {0b1110, 0b1001, 0b1001, 0b1001, 0b1001, 0b1001, 0b1110}, // D
    {0b1111, 0b1000, 0b1000, 0b1110, 0b1000, 0b1000, 0b1111}, // E
    {0b1111, 0b1000, 0b1000, 0b1110, 0b1000, 0b1000, 0b1000}, // F
    {0b0111, 0b1000, 0b1000, 0b1011, 0b1001, 0b1001, 0b0111}, // G
    {0b1001, 0b1001, 0b1001, 0b1111, 0b1001, 0b1001, 0b1001}, // H
    {0b1110, 0b0100, 0b0100, 0b0100, 0b0100, 0b0100, 0b1110}, // I
    {0b0111, 0b0010, 0b0010, 0b0010, 0b0010, 0b1010, 0b0100}, // J
    {0b1001, 0b1010, 0b1100, 0b1000, 0b1100, 0b1010, 0b1001}, // K
    {0b1000, 0b1000, 0b1000, 0b1000, 0b1000, 0b1000, 0b1111}, // L
    {0b1001, 0b1111, 0b1111, 0b1001, 0b1001, 0b1001, 0b1001}, // M
    {0b1001, 0b1101, 0b1101, 0b1011, 0b1011, 0b1001, 0b1001}, // N - diagonal
    {0b0110, 0b1001, 0b1001, 0b1001, 0b1001, 0b1001, 0b0110}, // O
    {0b1110, 0b1001, 0b1001, 0b1110, 0b1000, 0b1000, 0b1000}, // P
    {0b0110, 0b1001, 0b1001, 0b1001, 0b1011, 0b0110, 0b0001}, // Q
    {0b1110, 0b1001, 0b1001, 0b1110, 0b1010, 0b1001, 0b1001}, // R
    {0b0111, 0b1000, 0b1000, 0b0110, 0b0001, 0b0001, 0b1110}, // S
    {0b1111, 0b0100, 0b0100, 0b0100, 0b0100, 0b0100, 0b0100}, // T
    {0b1001, 0b1001, 0b1001, 0b1001, 0b1001, 0b1001, 0b0110}, // U
    {0b1001, 0b1001, 0b1001, 0b1001, 0b1001, 0b0110, 0b0110}, // V
    {0b1001, 0b1001, 0b1001, 0b1001, 0b1111, 0b1111, 0b1001}, // W
    {0b1001, 0b1001, 0b0110, 0b0110, 0b0110, 0b1001, 0b1001}, // X
    {0b1001, 0b1001, 0b1001, 0b0110, 0b0100, 0b0100, 0b0100}, // Y
    {0b1111, 0b0001, 0b0010, 0b0100, 0b1000, 0b1000, 0b1111}, // Z
};

// Draw a single character using full 7-pixel height, 4 pixels wide
static void drawChar3x5(int16_t x, uint8_t y, char c, uint32_t color) {
    int idx = -1;
    if (c >= 'A' && c <= 'Z') idx = c - 'A';
    else if (c >= 'a' && c <= 'z') idx = c - 'a';
    
    if (idx < 0 || idx > 25) return;
    
    for (uint8_t row = 0; row < 7; ++row) {
        uint8_t bits = FONT_4X7[idx][row];
        for (uint8_t col = 0; col < 4; ++col) {
            if (bits & (1 << (3 - col))) {
                int16_t px = x + col;
                if (px >= 0 && px < WT_MATRIX_WIDTH) {
                    wt_display_set_pixel_xy(px, 6 - row, color);
                }
            }
        }
    }
}

// Draw music note icon (for audio cards)
static void drawNoteIcon(int16_t x, uint8_t y, uint32_t color) {
    if (x < 0 || x + 3 >= WT_MATRIX_WIDTH) return;
    // Stem
    wt_display_set_pixel_xy(x + 2, y, color);
    wt_display_set_pixel_xy(x + 2, y + 1, color);
    wt_display_set_pixel_xy(x + 2, y + 2, color);
    wt_display_set_pixel_xy(x + 2, y + 3, color);
    // Note head
    wt_display_set_pixel_xy(x, y + 4, color);
    wt_display_set_pixel_xy(x + 1, y + 4, color);
    // Flag
    wt_display_set_pixel_xy(x + 3, y, color);
    wt_display_set_pixel_xy(x + 3, y + 1, color);
}

// Card icons - 7x7 bitmaps for epic transitions
static const uint8_t ICON_SUN[7] = {0x10, 0x54, 0x38, 0xFE, 0x38, 0x54, 0x10};      // ☀
static const uint8_t ICON_CLOCK[7] = {0x38, 0x44, 0x4C, 0x54, 0x44, 0x44, 0x38};    // ⏰
static const uint8_t ICON_BTC[7] = {0x7C, 0x52, 0x72, 0x52, 0x72, 0x52, 0x7C};      // ₿
static const uint8_t ICON_CHART[7] = {0x01, 0x03, 0x07, 0x0E, 0x1C, 0x38, 0x7F};    // 📈
static const uint8_t ICON_WIFI[7] = {0x00, 0x7E, 0x00, 0x3C, 0x00, 0x18, 0x18};     // 📶
static const uint8_t ICON_MIC[7] = {0x18, 0x3C, 0x3C, 0x3C, 0x18, 0x18, 0x7E};      // 🎤
static const uint8_t ICON_STAR[7] = {0x10, 0x10, 0x54, 0x38, 0x54, 0x10, 0x10};     // ✨
static const uint8_t ICON_WAVE[7] = {0x60, 0x90, 0x60, 0x06, 0x09, 0x06, 0x00};     // 🌈
static const uint8_t ICON_GAME[7] = {0x7F, 0x41, 0x5D, 0x55, 0x5D, 0x41, 0x7F};     // 🎮
static const uint8_t ICON_HOME[7] = {0x10, 0x38, 0x7C, 0x54, 0x54, 0x54, 0x7C};     // 🏠
static const uint8_t ICON_NEWS[7] = {0x7F, 0x41, 0x7F, 0x41, 0x5F, 0x41, 0x7F};     // 📰
static const uint8_t ICON_PLAY[7] = {0x00, 0x7C, 0x38, 0x10, 0x00, 0x00, 0x00};     // ▶

// Get icon for card
static const uint8_t* getCardIcon(uint8_t card) {
    switch (card) {
        case CARD_WEATHER: return ICON_SUN;
        case CARD_CLOCK:   return ICON_CLOCK;
        case CARD_BTC:     return ICON_BTC;
        case CARD_STOCK:   return ICON_CHART;
        case CARD_NETWORK: return ICON_WIFI;
        case CARD_VU:      return ICON_MIC;
        case CARD_SPARKLE: return ICON_STAR;
        case CARD_AURORA:  return ICON_WAVE;
        case CARD_GAMES:   return ICON_GAME;
        case CARD_MQTT:    return ICON_HOME;
        case CARD_RSS:     return ICON_NEWS;
        default:           return ICON_PLAY;
    }
}

// Get card color
static uint32_t getCardColor(uint8_t card) {
    switch (card) {
        case CARD_WEATHER: return wt_color(100, 180, 255);  // Sky blue
        case CARD_CLOCK:   return wt_color(255, 220, 100);  // Golden
        case CARD_BTC:     return wt_color(255, 160, 30);   // Bitcoin orange
        case CARD_STOCK:   return wt_color(50, 255, 100);   // Money green
        case CARD_NETWORK: return wt_color(0, 200, 255);    // Cyan
        case CARD_VU:      return wt_color(255, 50, 200);   // Hot pink
        case CARD_SPARKLE: return wt_color(255, 255, 255);  // White
        case CARD_AURORA:  return wt_color(50, 255, 150);   // Aurora green
        case CARD_GAMES:   return wt_color(255, 80, 255);   // Magenta
        case CARD_MQTT:    return wt_color(65, 180, 255);   // Home Assistant blue
        case CARD_RSS:     return wt_color(255, 150, 50);   // RSS orange
        case CARD_YOUTUBE: return wt_color(255, 0, 0);      // YouTube red
        case CARD_TWITCH:  return wt_color(145, 70, 255);   // Twitch purple
        case CARD_TWITTER: return wt_color(29, 161, 242);   // Twitter blue
        case CARD_INSTA:   return wt_color(255, 100, 150);  // Insta pink
        case CARD_TIKTOK:  return wt_color(0, 255, 200);    // TikTok teal
        default:           return wt_color(200, 200, 200);
    }
}

// Draw 7x7 icon at position
static void drawIcon7x7(int8_t x, int8_t y, const uint8_t* icon, uint32_t color) {
    for (int row = 0; row < 7; row++) {
        uint8_t bits = icon[row];
        for (int col = 0; col < 7; col++) {
            if (bits & (0x40 >> col)) {
                int px = x + col;
                int py = y + row;
                if (px >= 0 && px < WT_MATRIX_WIDTH && py >= 0 && py < WT_MATRIX_HEIGHT) {
                    wt_display_set_pixel_xy(px, py, color);
                }
            }
        }
    }
}

// Initialize particles for transition
static void initTransitionParticles(uint32_t color) {
    for (int i = 0; i < TRANS_PARTICLES; i++) {
        g_transParticles[i].x = random(WT_MATRIX_WIDTH);
        g_transParticles[i].y = random(WT_MATRIX_HEIGHT);
        g_transParticles[i].vx = (random(100) - 50) / 25.0f;
        g_transParticles[i].vy = (random(100) - 50) / 25.0f;
        g_transParticles[i].life = random(30, 60);
        // Color variation
        uint8_t r = ((color >> 16) & 0xFF);
        uint8_t g = ((color >> 8) & 0xFF);
        uint8_t b = (color & 0xFF);
        int var = random(-30, 30);
        g_transParticles[i].color = wt_color(
            constrain(r + var, 0, 255),
            constrain(g + var, 0, 255),
            constrain(b + var, 0, 255)
        );
    }
}

// Update and draw particles
static void updateParticles() {
    for (int i = 0; i < TRANS_PARTICLES; i++) {
        if (g_transParticles[i].life > 0) {
            g_transParticles[i].x += g_transParticles[i].vx;
            g_transParticles[i].y += g_transParticles[i].vy;
            g_transParticles[i].life--;
            
            int px = (int)g_transParticles[i].x;
            int py = (int)g_transParticles[i].y;
            if (px >= 0 && px < WT_MATRIX_WIDTH && py >= 0 && py < WT_MATRIX_HEIGHT) {
                uint8_t fade = g_transParticles[i].life * 4;
                uint32_t c = g_transParticles[i].color;
                uint8_t r = ((c >> 16) & 0xFF) * fade / 255;
                uint8_t g = ((c >> 8) & 0xFF) * fade / 255;
                uint8_t b = (c & 0xFF) * fade / 255;
                wt_display_set_pixel_xy(px, py, wt_color(r, g, b));
            }
        }
    }
}

// Render the title animation - EPIC TRANSITIONS!
static void renderTitle(uint32_t now) {
    wt_display_clear();
    wt_timeline_clear();
    
    float progress = (float)(now - g_titleStartTime) / TITLE_DURATION_MS;
    if (progress > 1.0f) progress = 1.0f;
    
    uint32_t color = getCardColor(g_currentCard);
    const uint8_t* icon = getCardIcon(g_currentCard);
    const char* name = g_cardNames[g_currentCard];
    uint8_t len = strlen(name);
    
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    
    // Pick transition effect based on g_transitionEffect (0-5)
    switch (g_transitionEffect % 6) {
        case 0: { // WIPE IN from left with icon + text
            int wipeX = (int)(progress * (WT_MATRIX_WIDTH + 10)) - 10;
            // Draw wipe edge (vertical rainbow line)
            for (int y = 0; y < WT_MATRIX_HEIGHT; y++) {
                if (wipeX >= 0 && wipeX < WT_MATRIX_WIDTH) {
                    uint8_t hue = (y * 36 + (int)(now / 10)) % 256;
                    wt_display_set_pixel_xy(wipeX, y, wt_color_hsv(hue, 255, 255));
                }
            }
            // Draw icon centered, fading in after wipe passes
            int iconX = 5;
            if (wipeX > iconX) {
                uint8_t iconBr = min(255, (wipeX - iconX) * 30);
                uint32_t iconCol = wt_color(r * iconBr / 255, g * iconBr / 255, b * iconBr / 255);
                drawIcon7x7(iconX, 0, icon, iconCol);
            }
            // Draw text scrolling in from right
            int textX = WT_MATRIX_WIDTH - (int)(progress * (WT_MATRIX_WIDTH - 13));
            for (uint8_t i = 0; i < len && i < 3; i++) {
                drawChar3x5(textX + i * 4, 1, name[i], color);
            }
            break;
        }
        
        case 1: { // EXPLOSION - icon bursts into particles, reforms as text
            if (progress < 0.4f) {
                // Draw icon exploding outward
                float explode = progress / 0.4f;
                for (int row = 0; row < 7; row++) {
                    uint8_t bits = icon[row];
                    for (int col = 0; col < 7; col++) {
                        if (bits & (0x40 >> col)) {
                            float dx = (col - 3) * explode * 3;
                            float dy = (row - 3) * explode * 2;
                            int px = 6 + col + (int)dx;
                            int py = row + (int)dy;
                            if (px >= 0 && px < WT_MATRIX_WIDTH && py >= 0 && py < WT_MATRIX_HEIGHT) {
                                uint8_t br = (uint8_t)(255 * (1.0f - explode));
                                wt_display_set_pixel_xy(px, py, wt_color(r*br/255, g*br/255, b*br/255));
                            }
                        }
                    }
                }
            } else {
                // Text fades in
                float textProg = (progress - 0.4f) / 0.6f;
                uint8_t br = (uint8_t)(255 * textProg);
                uint32_t textCol = wt_color(r*br/255, g*br/255, b*br/255);
                int textX = (WT_MATRIX_WIDTH - len * 4) / 2;
                for (uint8_t i = 0; i < len; i++) {
                    drawChar3x5(textX + i * 4, 1, name[i], textCol);
                }
            }
            updateParticles();
            break;
        }
        
        case 2: { // RAIN DROP - pixels fall from top, revealing icon
            int revealLine = (int)(progress * (WT_MATRIX_HEIGHT + 3));
            // Draw rain drops
            for (int x = 0; x < WT_MATRIX_WIDTH; x++) {
                int dropY = (revealLine + (x * 3) % 5) % (WT_MATRIX_HEIGHT + 5);
                if (dropY >= 0 && dropY < WT_MATRIX_HEIGHT) {
                    uint8_t hue = (x * 15 + (int)(now / 20)) % 256;
                    wt_display_set_pixel_xy(x, dropY, wt_color_hsv(hue, 200, 255));
                }
                if (dropY > 0 && dropY - 1 < WT_MATRIX_HEIGHT) {
                    wt_display_set_pixel_xy(x, dropY - 1, wt_color(50, 50, 80));
                }
            }
            // Icon appears as rain clears
            if (progress > 0.3f) {
                float iconBr = (progress - 0.3f) / 0.7f;
                uint32_t iconCol = wt_color((uint8_t)(r*iconBr), (uint8_t)(g*iconBr), (uint8_t)(b*iconBr));
                drawIcon7x7(6, 0, icon, iconCol);
            }
            break;
        }
        
        case 3: { // SPIRAL IN - pixels spiral into center forming icon
            float angle = progress * 6.28f * 3;  // 3 rotations
            float radius = (1.0f - progress) * 12;
            // Spiral trail
            for (int i = 0; i < 8; i++) {
                float a = angle - i * 0.3f;
                float rad = radius + i * 0.5f;
                int px = 10 + (int)(cosf(a) * rad);
                int py = 3 + (int)(sinf(a) * rad * 0.5f);
                if (px >= 0 && px < WT_MATRIX_WIDTH && py >= 0 && py < WT_MATRIX_HEIGHT) {
                    uint8_t br = 255 - i * 30;
                    uint8_t hue = ((int)(a * 40) + (int)(now / 10)) % 256;
                    wt_display_set_pixel_xy(px, py, wt_color_hsv(hue, 255, br));
                }
            }
            // Icon fades in at center
            if (progress > 0.5f) {
                float iconBr = (progress - 0.5f) * 2;
                uint32_t iconCol = wt_color((uint8_t)(r*iconBr), (uint8_t)(g*iconBr), (uint8_t)(b*iconBr));
                drawIcon7x7(6, 0, icon, iconCol);
            }
            break;
        }
        
        case 4: { // MATRIX RAIN + icon
            // Green matrix rain effect
            for (int x = 0; x < WT_MATRIX_WIDTH; x++) {
                int speed = 2 + (x % 3);
                int dropY = ((int)(now / (30 + x * 2)) + x * 7) % (WT_MATRIX_HEIGHT + 8) - 4;
                for (int t = 0; t < 4; t++) {
                    int py = dropY - t;
                    if (py >= 0 && py < WT_MATRIX_HEIGHT) {
                        uint8_t br = 255 - t * 60;
                        wt_display_set_pixel_xy(x, py, wt_color(0, br, br/3));
                    }
                }
            }
            // Icon pulses in center
            float pulse = 0.6f + 0.4f * sinf(now * 0.01f);
            uint32_t iconCol = wt_color((uint8_t)(r*pulse), (uint8_t)(g*pulse), (uint8_t)(b*pulse));
            drawIcon7x7(6, 0, icon, iconCol);
            break;
        }
        
        case 5: { // WAVE WIPE with rainbow trail
            float wavePhase = progress * 3.14159f;
            for (int x = 0; x < WT_MATRIX_WIDTH; x++) {
                float wave = sinf(wavePhase + x * 0.3f);
                int waveY = 3 + (int)(wave * 3);
                // Rainbow wave
                uint8_t hue = (x * 12 + (int)(now / 5)) % 256;
                for (int y = 0; y < WT_MATRIX_HEIGHT; y++) {
                    if (y == waveY || y == waveY + 1) {
                        wt_display_set_pixel_xy(x, y, wt_color_hsv(hue, 255, 255));
                    }
                }
            }
            // Show icon and short text
            if (progress > 0.2f) {
                float br = min(1.0f, (progress - 0.2f) * 2);
                drawIcon7x7(2, 0, icon, wt_color((uint8_t)(r*br), (uint8_t)(g*br), (uint8_t)(b*br)));
                // Show first 3 chars
                for (uint8_t i = 0; i < 3 && i < len; i++) {
                    drawChar3x5(11 + i * 4, 1, name[i], wt_color((uint8_t)(r*br), (uint8_t)(g*br), (uint8_t)(b*br)));
                }
            }
            break;
        }
    }
    
    // Epic timeline animation - rainbow chase with sparkles
    float tlPhase = progress * WT_TIMELINE_PIXELS * 2;
    for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i) {
        float dist = fabsf(i - tlPhase);
        if (dist < 5) {
            uint8_t br = (uint8_t)(255 * (1.0f - dist / 5.0f));
            uint8_t hue = (i * 8 + (int)(now / 10)) % 256;
            wt_timeline_set_pixel(i, wt_color_hsv(hue, 255, br));
        } else {
            // Subtle card color glow
            wt_timeline_set_pixel(i, wt_color(r/8, g/8, b/8));
        }
    }
}

// Preset counts - IMPORTANT: Update these when adding new presets!
// Also update the presetBtn() calls in net.cpp for the UI buttons
static const uint8_t WEATHER_PRESET_COUNT = 28;  // Weather presets 0-27 (update in weather_render switch + net.cpp UI)
static const uint8_t CLOCK_PRESET_COUNT = 14;    // Clock presets 0-13 (update in clock_render switch + net.cpp UI)
static const uint8_t VU_PRESET_COUNT = 27;       // VU presets 0-26 (update in vu_render switch + net.cpp UI)

// Demo mode state
static bool g_demoActive = false;
static uint32_t g_demoLastSwitch = 0;
static uint8_t g_demoTransitionPhase = 0;  // 0=showing preset, 1=flashy transition

// Get random enabled preset for a card type
static uint8_t getRandomEnabledPreset(uint8_t card, uint8_t maxPresets) {
    Settings& cfg = settings_get();
    uint32_t enabled = cfg.presetEnabled[card];
    
    // Count enabled presets
    uint8_t enabledList[32];
    uint8_t count = 0;
    for (uint8_t i = 0; i < maxPresets && i < 32; ++i) {
        if (enabled & (1UL << i)) {
            enabledList[count++] = i;
        }
    }
    
    if (count == 0) return 0;  // Fallback to preset 0
    return enabledList[random(count)];
}

// Count total items (cards + presets)
static uint8_t getTotalItems()
{
    return g_cardCount + (WEATHER_PRESET_COUNT - 1) + (VU_PRESET_COUNT - 1);
}

// Game mode (0=flappy, 1=snake, 2=breakout, 3=pong)
static uint8_t g_gameMode = 0;
static const uint8_t GAME_MODE_COUNT = 4;

// Game names for menu (short, 6 chars max)
static const char* g_gameNames[] = {"FLAP", "SNEK", "BRICK", "PONG"};

// Game menu state - true = in menu, false = playing
static bool g_inGameMenu = true;

// Title animation variables
static uint32_t g_lastAutoCycle = 0;

// Forward declaration
static void startTitleAnimation(uint32_t now);

static void startTitleAnimation(uint32_t now) {
    Settings& cfg = settings_get();
    g_lastAutoCycle = now; // Reset auto cycle timer on manual switch
    
    // Check if transitions are enabled
    if (!cfg.showTransitionTitle && !cfg.showTransitionAnim) {
        g_showingTitle = false;
        return;  // Skip all transitions
    }
    
    g_showingTitle = cfg.showTransitionTitle;
    g_titleStartTime = now;
    
    if (cfg.showTransitionAnim) {
        g_transitionEffect++;  // Cycle to next effect
        initTransitionParticles(getCardColor(g_currentCard));
    }
}

static uint8_t getNextEnabledCard(uint8_t current) {
    Settings& cfg = settings_get();
    // Find current position in order
    int8_t pos = -1;
    for(int i=0; i<g_cardCount; ++i) {
        if (cfg.cardOrder[i] == current) {
            pos = i;
            break;
        }
    }
    // If current not found in order, start from 0
    if (pos == -1) pos = 0;

    // Search for next enabled
    for(int i=1; i<=g_cardCount; ++i) {
        int nextPos = (pos + i) % g_cardCount;
        uint8_t nextCard = cfg.cardOrder[nextPos];
        if (nextCard < g_cardCount && cfg.cardEnabled[nextCard]) {
            return nextCard;
        }
    }
    return current; // No other enabled cards
}

static uint8_t getPrevEnabledCard(uint8_t current) {
    Settings& cfg = settings_get();
    // Find current position in order
    int8_t pos = -1;
    for(int i=0; i<g_cardCount; ++i) {
        if (cfg.cardOrder[i] == current) {
            pos = i;
            break;
        }
    }
    if (pos == -1) pos = 0;

    // Search for prev enabled
    for(int i=1; i<=g_cardCount; ++i) {
        int prevPos = (pos - i + g_cardCount) % g_cardCount;
        uint8_t prevCard = cfg.cardOrder[prevPos];
        if (prevCard < g_cardCount && cfg.cardEnabled[prevCard]) {
            return prevCard;
        }
    }
    return current;
}

static void cycleNext()
{
    uint32_t now = millis();
    
    // Weather, Clock, VU have presets, Games cycle inline
    if (g_currentCard == CARD_WEATHER && g_weatherPreset < WEATHER_PRESET_COUNT - 1) {
        g_weatherPreset++;
    } else if (g_currentCard == CARD_CLOCK && g_clockPreset < CLOCK_PRESET_COUNT - 1) {
        g_clockPreset++;
    } else if (g_currentCard == CARD_VU && g_vuPreset < VU_PRESET_COUNT - 1) {
        g_vuPreset++;
    } else if (g_currentCard == CARD_GAMES && g_gameMode < GAME_MODE_COUNT - 1) {
        g_gameMode++;
        g_cards[g_currentCard].setup();
    } else {
        // Move to next card
        g_weatherPreset = 0;
        g_clockPreset = 0;
        g_vuPreset = 0;
        g_gameMode = 0;
        
        uint8_t next = getNextEnabledCard(g_currentCard);
        if (next != g_currentCard) {
            g_currentCard = next;
            g_cards[g_currentCard].setup();
            startTitleAnimation(now);
        }
    }
}

static void cyclePrev()
{
    uint32_t now = millis();
    
    // Weather, Clock, VU have presets, Games cycle inline
    if (g_currentCard == CARD_WEATHER && g_weatherPreset > 0) {
        g_weatherPreset--;
    } else if (g_currentCard == CARD_CLOCK && g_clockPreset > 0) {
        g_clockPreset--;
    } else if (g_currentCard == CARD_VU && g_vuPreset > 0) {
        g_vuPreset--;
    } else if (g_currentCard == CARD_GAMES && g_gameMode > 0) {
        g_gameMode--;
        g_cards[g_currentCard].setup();
    } else {
        // Move to previous card
        uint8_t prev = getPrevEnabledCard(g_currentCard);
        
        if (prev != g_currentCard) {
            g_currentCard = prev;
            
            // Set presets to max for the new card
            if (g_currentCard == CARD_WEATHER) {
                g_weatherPreset = WEATHER_PRESET_COUNT - 1;
            } else if (g_currentCard == CARD_CLOCK) {
                g_clockPreset = CLOCK_PRESET_COUNT - 1;
            } else if (g_currentCard == CARD_VU) {
                g_vuPreset = VU_PRESET_COUNT - 1;
            } else if (g_currentCard == CARD_GAMES) {
                g_gameMode = GAME_MODE_COUNT - 1;
            } else {
                g_weatherPreset = 0;
                g_clockPreset = 0;
                g_vuPreset = 0;
                g_gameMode = 0;
            }
            g_cards[g_currentCard].setup();
            startTitleAnimation(now);
        }
    }
}

// Global button edge flags - set once per frame in cards_loop()
static bool g_btn1Edge = false;
static bool g_btn2Edge = false;

static void handleButtons(uint32_t now)
{
    static uint32_t bothHeldStart = 0;
    bool b1 = wt_button1_is_down();
    bool b2 = wt_button2_is_down();

    // Reset auto cycle on interaction
    if (b1 || b2) {
        g_lastAutoCycle = now;
    }
    
    // Games card: buttons control the game, not navigation
    // Use BOTH buttons pressed together to exit game
    if (g_currentCard == CARD_GAMES) {
        if (b1 && b2) {
            if (bothHeldStart == 0) bothHeldStart = now;
            if (now - bothHeldStart > 1000) {
                // Exit games after holding both for 1s
                bothHeldStart = 0;
                g_gameMode = 0;
                
                uint8_t next = getNextEnabledCard(g_currentCard);
                g_currentCard = next;
                
                g_cards[g_currentCard].setup();
                startTitleAnimation(now);
                
                // Wait until buttons released to prevent accidental triggering
                while(wt_button1_is_down() || wt_button2_is_down()) {
                    delay(10);
                }
            }
        } else {
            bothHeldStart = 0;
        }
        
        // Don't process card navigation - games handle buttons internally
        return;
    }
    
    // Button 1 = forward, Button 2 = backward (non-game cards)
    if (g_btn1Edge) {
        cycleNext();
    }
    if (g_btn2Edge) {
        cyclePrev();
    }
}

// Adaptive gain / automatic gain control state
static uint16_t g_agcPeak = 500;          // Adaptive peak level
static uint32_t g_silenceStartMs = 0;     // When silence started

static void sampleAudio(uint32_t now)
{
    if (now - g_lastAudioSample < 20)
    {
        return;
    }
    g_lastAudioSample = now;

    Settings& cfg = settings_get();
    
    int16_t minVal = INT16_MAX;
    int16_t maxVal = INT16_MIN;
    for (uint8_t i = 0; i < 32; ++i)
    {
        int16_t raw = (int16_t)wt_mic_read_raw();
        if (raw < minVal) minVal = raw;
        if (raw > maxVal) maxVal = raw;
    }

    uint16_t amp = maxVal - minVal;
    
    // Apply mic gain (1-10) with EXTREME boost for high sensitivity
    // Range: ~1x (min) to ~100x (max) - exponential curve
    float gainVal = (float)cfg.micGain;
    float gainMult = powf(2.5f, gainVal * 0.5f);  // Exponential: 2.5^(gain/2)
    
    // Apply additional boost (0-10) for extra amplification
    // boost 0=1x, 5=~10x, 10=~50x additional
    if (cfg.micBoost > 0) {
        float boostMult = 1.0f + (float)cfg.micBoost * (float)cfg.micBoost * 0.5f;
        gainMult *= boostMult;
    }
    
    amp = (uint16_t)((float)amp * gainMult);
    if (amp > 65000) amp = 65000;  // Prevent overflow
    
    // Dynamic noise floor based on vuNoiseGate setting (0-255 -> 10-200)
    // Very low baseline for maximum sensitivity
    uint16_t noiseFloor = 10 + (cfg.vuNoiseGate * 3 / 4);
    
    // Adaptive gain control (AGC) - can be disabled via settings
    if (cfg.agcEnabled) {
        if (amp > g_agcPeak) {
            g_agcPeak = (g_agcPeak * 3 + amp) / 4;  // Very fast rise
        } else {
            g_agcPeak = (g_agcPeak * 63 + 150) / 64;  // Slow decay toward low baseline
        }
        if (g_agcPeak < 150) g_agcPeak = 150;   // Very low minimum for sensitivity
        if (g_agcPeak > 10000) g_agcPeak = 10000; // Higher max for loud audio
    } else {
        g_agcPeak = 1000;  // Fixed reference when AGC disabled
    }
    
    // Silence detection - if signal is low for a while, suppress output
    // cfg.vuSilenceMs = 0 disables silence detection
    if (cfg.vuSilenceMs > 0 && amp < noiseFloor) {
        if (g_silenceStartMs == 0) {
            g_silenceStartMs = now;
        }
        if (now - g_silenceStartMs > cfg.vuSilenceMs) {
            // Sustained silence - zero out
            amp = 0;
        }
    } else {
        g_silenceStartMs = 0;  // Reset silence timer
    }
    
    // Subtract noise floor
    if (amp > noiseFloor) {
        amp -= noiseFloor;
    } else {
        amp = 0;
    }
    
    // Normalize using AGC peak
    uint16_t normalized = (amp * 255) / (g_agcPeak - noiseFloor + 1);
    if (normalized > 255) normalized = 255;

    uint8_t target = (uint8_t)normalized;

    if (target > g_audioLevel)
    {
        g_audioLevel = (g_audioLevel * 3 + target) / 4 + 1; // quick attack
        if (g_audioLevel > 255) g_audioLevel = 255;
    }
    else
    {
        g_audioLevel = (g_audioLevel * 31) / 32; // smooth decay
    }
}

static void applySoundReactiveOverlay(uint32_t now)
{
    if (!g_soundReactive)
    {
        return;
    }

    if (now < g_soundToggleFlashUntil)
    {
        uint32_t flashCol = g_soundReactive ? wt_color(0, 200, 120) : wt_color(200, 60, 60);
        wt_timeline_set_pixel(0, flashCol);
        wt_timeline_set_pixel(WT_TIMELINE_PIXELS - 1, flashCol);
        wt_display_set_pixel_xy(0, 0, flashCol);
        wt_display_set_pixel_xy(WT_MATRIX_WIDTH - 1, 0, flashCol);
        return;
    }

    if (g_audioLevel < 6)
    {
        return;
    }

    uint8_t glow = g_audioLevel / 6;
    if (glow > 60) glow = 60;
    uint32_t pulse = wt_color(glow * 2, glow, glow * 3 / 2);

    wt_timeline_set_pixel(0, pulse);
    wt_timeline_set_pixel(WT_TIMELINE_PIXELS - 1, pulse);
    wt_display_set_pixel_xy(0, 0, pulse);
    wt_display_set_pixel_xy(WT_MATRIX_WIDTH - 1, 0, pulse);
}

// Boot animation: Beating red heart - friendly welcome
static void renderBootAnimation(uint32_t now)
{
    wt_display_clear();
    wt_timeline_clear();

    uint32_t elapsed = now - g_bootStart;

    // 3 heartbeats then fade
    const uint32_t beatDuration = 600;  // One beat cycle
    const uint32_t totalBeats = 3;
    const uint32_t beatTime = beatDuration * totalBeats;
    const uint32_t fadeTime = 400;

    // Phase transitions
    if (g_bootPhase == 0 && elapsed > beatTime) {
        g_bootPhase = 1;
        g_bootStart = now;
        elapsed = 0;
    } else if (g_bootPhase == 1 && elapsed > fadeTime) {
        g_bootPhase = 2;
        g_bootDone = true;
        return;
    }

    // Heart shape bitmap (centered, 9x7)
    // Using classic pixel heart shape
    static const uint16_t HEART[7] = {
        0b011011000,  // row 6 (top)     .##.##.
        0b111111100,  // row 5           #######
        0b111111100,  // row 4           #######
        0b011111000,  // row 3           .#####.
        0b001110000,  // row 2           ..###..
        0b000100000,  // row 1           ...#...
        0b000000000,  // row 0 (bottom)  .......
    };
    
    // Calculate beat phase (0-1 within each beat)
    float beatPhase = 0.0f;
    float scale = 1.0f;
    
    if (g_bootPhase == 0) {
        uint32_t beatPos = elapsed % beatDuration;
        beatPhase = (float)beatPos / (float)beatDuration;
        
        // Quick pump at start of beat, then relax
        if (beatPhase < 0.15f) {
            scale = 1.0f + 0.3f * (beatPhase / 0.15f);  // Grow
        } else if (beatPhase < 0.3f) {
            scale = 1.3f - 0.3f * ((beatPhase - 0.15f) / 0.15f);  // Shrink back
        } else {
            scale = 1.0f;  // Rest
        }
    }

    // Fade multiplier
    float fadeMul = 1.0f;
    if (g_bootPhase == 1) {
        fadeMul = 1.0f - (float)elapsed / (float)fadeTime;
        if (fadeMul < 0.0f) fadeMul = 0.0f;
    }

    // Heart color - warm red with slight glow variation
    float glow = 0.8f + 0.2f * sinf(now * 0.01f);
    uint8_t r = (uint8_t)(255 * glow * scale * fadeMul);
    uint8_t g = (uint8_t)(40 * glow * fadeMul);
    uint8_t b = (uint8_t)(60 * glow * fadeMul);
    uint32_t heartCol = wt_color(r, g, b);
    
    // Dimmer color for outline/glow
    uint32_t glowCol = wt_color(r/3, g/3, b/3);

    // Center the heart (9 wide, centered on 20-wide display = offset 5-6)
    int8_t offsetX = 6;
    int8_t offsetY = 0;

    // Draw heart
    for (int8_t row = 0; row < 7; ++row) {
        uint16_t bits = HEART[6 - row];  // Flip vertically
        for (int8_t col = 0; col < 9; ++col) {
            if (bits & (1 << (8 - col))) {
                int8_t px = offsetX + col;
                int8_t py = offsetY + row;
                if (px >= 0 && px < WT_MATRIX_WIDTH && py >= 0 && py < WT_MATRIX_HEIGHT) {
                    wt_display_set_pixel_xy(px, py, heartCol);
                }
            }
        }
    }

    // Timeline pulses with heartbeat
    uint8_t tlBright = (uint8_t)(30 * scale * fadeMul);
    for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i) {
        wt_timeline_set_pixel(i, wt_color(tlBright, tlBright/4, tlBright/3));
    }
}

// WiFi connected success animation - checkmark then show IP
static void renderWifiNotification(uint32_t now)
{
    wt_display_clear();
    wt_timeline_clear();
    
    uint32_t elapsed = now - g_wifiNotifyStart;
    
    // Phase 1: Show green checkmark (0-1500ms)
    // Phase 2: Scroll IP (after 1500ms)
    const uint32_t checkPhase = 1500;
    
    if (elapsed < checkPhase) {
        // Draw animated green checkmark
        float progress = (float)elapsed / (float)checkPhase;
        uint8_t bright = (uint8_t)(200 + 55 * sinf(now * 0.01f));
        uint32_t checkCol = wt_color(0, bright, bright/3);
        
        // Checkmark shape (appears progressively)
        // Check mark coordinates: short stroke down-left, then long stroke up-right
        int8_t cx = 7; // Center
        int8_t cy = 1;
        
        if (progress > 0.0f) wt_display_set_pixel_xy(cx, cy + 2, checkCol);
        if (progress > 0.15f) wt_display_set_pixel_xy(cx - 1, cy + 1, checkCol);
        if (progress > 0.3f) wt_display_set_pixel_xy(cx - 2, cy, checkCol);
        if (progress > 0.45f) wt_display_set_pixel_xy(cx + 1, cy + 3, checkCol);
        if (progress > 0.6f) wt_display_set_pixel_xy(cx + 2, cy + 4, checkCol);
        if (progress > 0.75f) wt_display_set_pixel_xy(cx + 3, cy + 5, checkCol);
        if (progress > 0.9f) wt_display_set_pixel_xy(cx + 4, cy + 6, checkCol);
        
        // Green pulsing timeline
        for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i) {
            uint8_t tlb = 30 + (uint8_t)(30 * sinf(now * 0.005f + i * 0.2f));
            wt_timeline_set_pixel(i, wt_color(0, tlb, tlb/3));
        }
    } else {
        // Scroll IP address
        uint32_t scrollElapsed = elapsed - checkPhase;
        int16_t scrollPos = WT_MATRIX_WIDTH - (int16_t)(scrollElapsed / 50);
        
        uint32_t textCol = wt_color(0, 200, 180);
        
        for (uint8_t i = 0; g_wifiIP[i] != '\0' && i < 16; ++i)
        {
            char c = g_wifiIP[i];
            int16_t cx = scrollPos + i * 4;
            
            if (cx < -3 || cx >= WT_MATRIX_WIDTH) continue;
            
            if (c == '.') {
                if (cx >= 0 && cx < WT_MATRIX_WIDTH)
                    wt_display_set_pixel_xy(cx + 1, 0, textCol);
                continue;
            }
            
            if (c >= '0' && c <= '9') {
                drawDigit(cx, 0, c - '0', textCol);
            }
        }
        
        // Calculate total width
        int16_t totalWidth = strlen(g_wifiIP) * 4;
        
        // Done scrolling?
        if (scrollPos < -totalWidth || elapsed > 10000) {
            g_wifiNotifyPending = false;
        }
        
        // Cyan timeline
        for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i)
            wt_timeline_set_pixel(i, wt_color(0, 100, 120));
    }
}

void cards_begin()
{
    g_currentCard = 0;
    g_lastTick = millis();
    g_lastBtn1 = wt_button1_pressed();
    g_lastBtn2 = wt_button2_pressed();
    
    // Start boot animation
    g_bootPhase = 0;
    g_bootStart = millis();
    g_bootDone = false;
    g_bootTrailCount = 0;

    if (g_cardCount > 0)
    {
        g_cards[g_currentCard].setup();
    }
}

// Called by net.cpp when WiFi connects
void cards_notify_wifi_connected(const char* ip)
{
    strncpy(g_wifiIP, ip, sizeof(g_wifiIP) - 1);
    g_wifiIP[sizeof(g_wifiIP) - 1] = '\0';
    // Show checkmark + IP scroll animation after connecting
    g_wifiNotifyPending = true;
    g_wifiNotifyStart = millis();
    g_scrollX = WT_MATRIX_WIDTH;
    
    Serial.print("WiFi connected: ");
    Serial.println(ip);
}

// Card switching API
void cards_switch_to(uint8_t cardIndex)
{
    if (cardIndex >= g_cardCount) return;
    g_currentCard = cardIndex;
    g_weatherPreset = 0;
    g_clockPreset = 0;
    g_vuPreset = 0;
    g_gameMode = 0;
    g_cards[g_currentCard].setup();
    startTitleAnimation(millis());
}

void cards_set_preset(uint8_t preset)
{
    if (g_currentCard == CARD_WEATHER) {
        g_weatherPreset = preset % WEATHER_PRESET_COUNT;
    } else if (g_currentCard == CARD_CLOCK) {
        g_clockPreset = preset % CLOCK_PRESET_COUNT;
    } else if (g_currentCard == CARD_VU) {
        g_vuPreset = preset % VU_PRESET_COUNT;
    } else if (g_currentCard == CARD_GAMES) {
        g_gameMode = preset % GAME_MODE_COUNT;
        g_cards[g_currentCard].setup();
    }
}

uint8_t cards_get_current() { return g_currentCard; }
uint8_t cards_get_preset() { 
    if (g_currentCard == CARD_WEATHER) return g_weatherPreset;
    if (g_currentCard == CARD_CLOCK) return g_clockPreset;
    if (g_currentCard == CARD_VU) return g_vuPreset;
    if (g_currentCard == CARD_GAMES) return g_gameMode;
    return 0;
}
uint8_t cards_get_count() { return g_cardCount; }

// Friendly AP mode welcome screen with cloud animation
static void renderAPWelcomeScreen(uint32_t now)
{
    wt_display_clear();
    wt_timeline_clear();
    
    // Welcoming rainbow timeline - gentle wave
    for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i) {
        uint8_t hue = (now / 30 + i * 15) % 256;
        uint8_t bright = 40 + (uint8_t)(20 * sinf(now * 0.003f + i * 0.3f));
        wt_timeline_set_pixel(i, colorWheel(hue));
    }
    
    // Cute cloud with pulsing heart inside (it's a cloud device!)
    // Cloud shape (11x5)
    static const uint16_t CLOUD[5] = {
        0b00111110000,  // row 4 (top)
        0b01111111100,  // row 3
        0b11111111110,  // row 2
        0b01111111100,  // row 1
        0b00011100000,  // row 0 (bottom)
    };
    
    // Draw cloud (offset to left)
    float cloudPulse = 0.8f + 0.2f * sinf(now * 0.005f);
    uint8_t cloudR = (uint8_t)(200 * cloudPulse);
    uint8_t cloudG = (uint8_t)(220 * cloudPulse);
    uint8_t cloudB = (uint8_t)(255 * cloudPulse);
    uint32_t cloudCol = wt_color(cloudR, cloudG, cloudB);
    
    int8_t cloudX = 0;
    int8_t cloudY = 1;
    for (int8_t row = 0; row < 5; ++row) {
        uint16_t bits = CLOUD[4 - row];
        for (int8_t col = 0; col < 11; ++col) {
            if (bits & (1 << (10 - col))) {
                int8_t px = cloudX + col;
                int8_t py = cloudY + row;
                if (px >= 0 && px < WT_MATRIX_WIDTH && py >= 0 && py < WT_MATRIX_HEIGHT) {
                    wt_display_set_pixel_xy(px, py, cloudCol);
                }
            }
        }
    }
    
    // Small beating heart inside cloud
    float heartbeat = sinf(now * 0.008f);
    uint8_t heartBright = (heartbeat > 0) ? (uint8_t)(180 + 75 * heartbeat) : 180;
    uint32_t heartCol = wt_color(heartBright, heartBright/5, heartBright/4);
    
    // Tiny 3x3 heart in cloud center
    wt_display_set_pixel_xy(4, 4, heartCol);
    wt_display_set_pixel_xy(6, 4, heartCol);
    wt_display_set_pixel_xy(3, 3, heartCol);
    wt_display_set_pixel_xy(4, 3, heartCol);
    wt_display_set_pixel_xy(5, 3, heartCol);
    wt_display_set_pixel_xy(6, 3, heartCol);
    wt_display_set_pixel_xy(7, 3, heartCol);
    wt_display_set_pixel_xy(4, 2, heartCol);
    wt_display_set_pixel_xy(5, 2, heartCol);
    wt_display_set_pixel_xy(6, 2, heartCol);
    wt_display_set_pixel_xy(5, 1, heartCol);
    
    // WiFi symbol on right side (animated signal)
    int8_t wifiX = 13;
    int8_t wifiY = 0;
    uint8_t arcPhase = (now / 400) % 4;
    
    uint32_t wifiOn = wt_color(0, 220, 180);
    uint32_t wifiDim = wt_color(0, 60, 50);
    
    // Center dot
    wt_display_set_pixel_xy(wifiX + 3, wifiY + 0, wifiOn);
    wt_display_set_pixel_xy(wifiX + 3, wifiY + 1, wifiOn);
    
    // Arc 1
    uint32_t a1 = (arcPhase >= 1) ? wifiOn : wifiDim;
    wt_display_set_pixel_xy(wifiX + 2, wifiY + 2, a1);
    wt_display_set_pixel_xy(wifiX + 4, wifiY + 2, a1);
    
    // Arc 2
    uint32_t a2 = (arcPhase >= 2) ? wifiOn : wifiDim;
    wt_display_set_pixel_xy(wifiX + 1, wifiY + 3, a2);
    wt_display_set_pixel_xy(wifiX + 5, wifiY + 3, a2);
    wt_display_set_pixel_xy(wifiX + 1, wifiY + 4, a2);
    wt_display_set_pixel_xy(wifiX + 5, wifiY + 4, a2);
    
    // Arc 3
    uint32_t a3 = (arcPhase >= 3) ? wifiOn : wifiDim;
    wt_display_set_pixel_xy(wifiX + 0, wifiY + 5, a3);
    wt_display_set_pixel_xy(wifiX + 6, wifiY + 5, a3);
    wt_display_set_pixel_xy(wifiX + 0, wifiY + 6, a3);
    wt_display_set_pixel_xy(wifiX + 6, wifiY + 6, a3);
}

void cards_loop()
{
    uint32_t now = millis();
    uint32_t dt = now - g_lastTick;
    g_lastTick = now;

    // Read button edges ONCE per frame - must be first!
    g_btn1Edge = wt_button1_pressed();
    g_btn2Edge = wt_button2_pressed();

    sampleAudio(now);
    
    // Update brightness with settings
    Settings& cfg = settings_get();
    wt_update_brightness_auto(cfg.brightMin, cfg.brightMax, cfg.brightMode, cfg.brightManual, cfg.brightBlanking, cfg.brightBlankSecs);

    // Boot animation takes priority
    if (!g_bootDone)
    {
        renderBootAnimation(now);
        wt_leds_show();
        return;
    }
    
    // Show friendly AP welcome screen if in AP mode
    if (net_is_ap_mode())
    {
        renderAPWelcomeScreen(now);
        handleButtons(now); // Still allow button presses
        wt_leds_show();
        return;
    }
    
    // WiFi notification takes priority after boot
    if (g_wifiNotifyPending)
    {
        renderWifiNotification(now);
        wt_leds_show();
        return;
    }

    handleButtons(now);

    if (g_cardCount == 0)
    {
        return;
    }
    
    // Demo mode - stunning preset showcase for video capture
    if (cfg.demoMode) {
        // Initialize demo mode on first run
        if (!g_demoActive) {
            g_demoActive = true;
            g_demoLastSwitch = now;
            g_demoTransitionPhase = 0;
        }
        
        uint32_t demoDuration = (uint32_t)cfg.demoDurationSecs * 1000;
        uint32_t elapsed = now - g_demoLastSwitch;
        
        // Flashy transition effect in last 500ms
        if (elapsed > demoDuration - 500 && elapsed < demoDuration) {
            // Stunning wipe/flash transition
            float progress = (float)(elapsed - (demoDuration - 500)) / 500.0f;
            wt_display_clear();
            
            // Rainbow sweep from left to right
            int sweepX = (int)(progress * (WT_MATRIX_WIDTH + 4)) - 2;
            for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
                int dist = abs(x - sweepX);
                if (dist < 3) {
                    uint8_t bright = (dist == 0) ? 255 : (dist == 1) ? 180 : 80;
                    for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
                        uint8_t hue = (x * 12 + y * 20 + (now / 5)) % 256;
                        wt_display_set_pixel_xy(x, y, wt_color_hsv(hue, 255, bright));
                    }
                }
            }
            
            // Sparkle particles
            for (int i = 0; i < 8; ++i) {
                int px = random(WT_MATRIX_WIDTH);
                int py = random(WT_MATRIX_HEIGHT);
                if (random(100) < 30) {
                    wt_display_set_pixel_xy(px, py, wt_color(255, 255, 255));
                }
            }
            
            wt_leds_show();
            return;
        }
        
        // Time to switch to next random preset
        if (elapsed >= demoDuration) {
            g_demoLastSwitch = now;
            
            // Pick a random enabled card that has presets
            uint8_t cardChoices[3] = {CARD_WEATHER, CARD_CLOCK, CARD_VU};
            uint8_t presetCounts[3] = {WEATHER_PRESET_COUNT, CLOCK_PRESET_COUNT, VU_PRESET_COUNT};
            
            // Filter to only enabled cards
            uint8_t validCards[3];
            uint8_t validCount = 0;
            for (int i = 0; i < 3; ++i) {
                if (cfg.cardEnabled[cardChoices[i]]) {
                    validCards[validCount++] = i;
                }
            }
            
            if (validCount > 0) {
                uint8_t pick = validCards[random(validCount)];
                uint8_t newCard = cardChoices[pick];
                uint8_t newPreset = getRandomEnabledPreset(newCard, presetCounts[pick]);
                
                g_currentCard = newCard;
                g_cards[g_currentCard].setup();
                
                // Set the preset
                if (newCard == CARD_WEATHER) g_weatherPreset = newPreset;
                else if (newCard == CARD_CLOCK) g_clockPreset = newPreset;
                else if (newCard == CARD_VU) g_vuPreset = newPreset;
                
                startTitleAnimation(now);
            }
        }
    } else {
        g_demoActive = false;  // Reset when demo mode disabled
    }
    
    // Auto cycle (skip if demo mode active)
    if (!cfg.demoMode && cfg.cycleEnabled && cfg.cycleDuration > 0 && g_currentCard != CARD_GAMES) {
        if (now - g_lastAutoCycle > (uint32_t)cfg.cycleDuration * 1000) {
            // Switch to next card
            uint8_t next = getNextEnabledCard(g_currentCard);
            if (next != g_currentCard) {
                // Reset presets for consistency
                g_weatherPreset = 0; 
                g_clockPreset = 0;
                g_vuPreset = 0;
                
                g_currentCard = next;
                g_cards[g_currentCard].setup();
                startTitleAnimation(now);
            }
            g_lastAutoCycle = now;
        }
    }

    // Check if title animation is playing
    if (g_showingTitle) {
        if (now - g_titleStartTime < TITLE_DURATION_MS) {
            renderTitle(now);
            wt_leds_show();
            return;
        } else {
            // Title animation finished
            g_showingTitle = false;
        }
    }

    // Background tasks (fetch tickers and social media)
    if (now % 1000 == 0) { // Check every second
        ticker_update(now, 0);
        stock_update(now, 0);
        // Social media updates (they manage their own timing)
        youtube_update(now, 0);
        twitch_update(now, 0);
        twitter_update(now, 0);
        insta_update(now, 0);
        tiktok_update(now, 0);
    }

    Card &card = g_cards[g_currentCard];
    card.update(now, dt);
    card.render();

    applySoundReactiveOverlay(now);

    wt_leds_show();
}

static void diag_setup()
{
    g_diagLedStep = 0;
    uint32_t now = millis();
    g_diagLastPrint = now;
    g_diagLastStep = now;
}

static void diag_update(uint32_t now, uint32_t dt)
{
    const uint32_t stepInterval = 80;

    if (now - g_diagLastStep >= stepInterval)
    {
        g_diagLastStep = now;

        wt_display_clear();
        wt_timeline_clear();

        uint16_t matrixIndex = g_diagLedStep % WT_MATRIX_PIXELS;
        uint8_t c = (g_diagLedStep * 2) & 0xFF;
        uint32_t col = colorWheel(c);
        wt_display_set_pixel_raw(matrixIndex, col);

        uint8_t tlIndex = g_diagLedStep % WT_TIMELINE_PIXELS;
        wt_timeline_set_pixel(tlIndex, col);

        g_diagLedStep++;
    }

    if (now - g_diagLastPrint > 500)
    {
        g_diagLastPrint = now;

        bool btn1 = wt_button1_pressed();
        bool btn2 = wt_button2_pressed();
        bool cap = wt_cap_touch_active();
        uint16_t micRaw = wt_mic_read_raw();
        uint16_t micLevel = wt_mic_level();
        uint16_t lightRaw = wt_light_read_raw();
        uint16_t lightLevel = wt_light_level();

        Serial.print("B1=");
        Serial.print(btn1 ? "P" : "-");
        Serial.print(" B2=");
        Serial.print(btn2 ? "P" : "-");
        Serial.print(" CAP=");
        Serial.print(cap ? "1" : "0");
        Serial.print(" MICR=");
        Serial.print(micRaw);
        Serial.print(" MICLEV=");
        Serial.print(micLevel);
        Serial.print(" LGTR=");
        Serial.print(lightRaw);
        Serial.print(" LGTLEV=");
        Serial.println(lightLevel);
    }
}

static void diag_render()
{
}

static void clock_setup()
{
    g_clockLastUpdate = 0;
    g_clockTimeValid = false;
}

static void clock_update(uint32_t now, uint32_t dt)
{
    const uint32_t updateInterval = 1000;

    if (now - g_clockLastUpdate >= updateInterval)
    {
        g_clockLastUpdate = now;

        tm info;
        if (getLocalTime(&info, 10))
        {
            g_clockTime = info;
            g_clockTimeValid = true;
        }
        else
        {
            g_clockTimeValid = false;
        }
    }
}

static void drawDigitGradient(uint8_t x, uint8_t y, uint8_t d, uint32_t cTop, uint32_t cBot)
{
    if (d > 9) return;
    for (uint8_t row = 0; row < DIGIT_H; ++row)
    {
        // Lerp color
        uint8_t r = ((cTop >> 16) & 0xFF) * (DIGIT_H - 1 - row) / (DIGIT_H - 1) + ((cBot >> 16) & 0xFF) * row / (DIGIT_H - 1);
        uint8_t g = ((cTop >> 8) & 0xFF) * (DIGIT_H - 1 - row) / (DIGIT_H - 1) + ((cBot >> 8) & 0xFF) * row / (DIGIT_H - 1);
        uint8_t b = (cTop & 0xFF) * (DIGIT_H - 1 - row) / (DIGIT_H - 1) + (cBot & 0xFF) * row / (DIGIT_H - 1);
        uint32_t col = wt_color(r, g, b);

        uint8_t bits = sprites_get_digit_row(d, row);
        for (uint8_t colIdx = 0; colIdx < DIGIT_W; ++colIdx)
        {
            if (bits & (1 << (DIGIT_W - 1 - colIdx)))
                wt_display_set_pixel_xy(x + colIdx, y + (DIGIT_H - 1 - row), col);
        }
    }
}

// Get adjusted clock time
static void getClockTime(int& hour, uint8_t& minute, uint8_t& second) {
    int8_t tzOffset = settings_get().tzOffset;
    hour = g_clockTime.tm_hour + tzOffset;
    if (hour < 0) hour += 24;
    if (hour >= 24) hour -= 24;
    minute = g_clockTime.tm_min;
    second = g_clockTime.tm_sec;
}

// Watchface 0: Digital (gradient with cycle)
static void clock_render_digital()
{
    static uint32_t lastCycle = 0;
    static uint8_t cycleIdx = 0;
    
    // Cycle every 30 seconds
    if (millis() - lastCycle > 30000) {
        lastCycle = millis();
        cycleIdx = (cycleIdx + 1) % 4;
    }
    
    uint32_t cTop, cBot;
    
    switch(cycleIdx) {
        case 0: cTop = wt_color(0, 255, 255); cBot = wt_color(0, 100, 255); break; // Cyan/Blue
        case 1: cTop = wt_color(255, 100, 200); cBot = wt_color(150, 0, 150); break; // Pink/Purple
        case 2: cTop = wt_color(100, 255, 100); cBot = wt_color(0, 150, 50); break; // Green
        case 3: cTop = wt_color(255, 200, 50); cBot = wt_color(255, 50, 0); break; // Orange/Red
    }

    int hour; uint8_t minute, second;
    getClockTime(hour, minute, second);

    uint8_t hTens = hour / 10;
    uint8_t hOnes = hour % 10;
    uint8_t mTens = minute / 10;
    uint8_t mOnes = minute % 10;

    uint8_t x = 1;
    drawDigitGradient(x, 0, hTens, cTop, cBot); x += 4;
    drawDigitGradient(x, 0, hOnes, cTop, cBot); x += 4;

    // Breathing colon
    float breathPhase = (millis() % 2000) / 2000.0f;
    uint8_t breath = (uint8_t)(80 + 175 * (0.5f + 0.5f * sinf(breathPhase * 6.28f)));
    uint32_t colColon = wt_color(0, breath, breath);
    wt_display_set_pixel_xy(x, 5, colColon);
    wt_display_set_pixel_xy(x, 2, colColon);
    x += 2;

    drawDigitGradient(x, 0, mTens, cTop, cBot); x += 4;
    drawDigitGradient(x, 0, mOnes, cTop, cBot);

    // Seconds timeline
    uint8_t tlLen = (second * WT_TIMELINE_PIXELS) / 60;
    for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i) {
        if (i < tlLen) wt_timeline_set_pixel(i, wt_color(0, 150, 255));
        else if (i == tlLen) wt_timeline_set_pixel(i, wt_color(0, 50, 100));
    }
}

// Watchface 1: Binary clock
static void clock_render_binary()
{
    int hour; uint8_t minute, second;
    getClockTime(hour, minute, second);

    // Binary representation: 4 cols for hour (0-23), 6 cols for minute (0-59), 6 cols for second
    // Each column shows bits vertically
    uint32_t colHr = wt_color(255, 150, 0);   // Orange for hours
    uint32_t colMn = wt_color(0, 200, 255);   // Cyan for minutes
    uint32_t colSc = wt_color(150, 100, 255); // Purple for seconds

    // Hours (5 bits, cols 0-4)
    for (uint8_t bit = 0; bit < 5; ++bit) {
        if (hour & (1 << (4 - bit))) {
            for (uint8_t y = 1; y < 6; ++y)
                wt_display_set_pixel_xy(bit, y, colHr);
        } else {
            wt_display_set_pixel_xy(bit, 3, wt_color(40, 30, 0));
        }
    }
    
    // Minutes (6 bits, cols 6-11)
    for (uint8_t bit = 0; bit < 6; ++bit) {
        if (minute & (1 << (5 - bit))) {
            for (uint8_t y = 1; y < 6; ++y)
                wt_display_set_pixel_xy(6 + bit, y, colMn);
        } else {
            wt_display_set_pixel_xy(6 + bit, 3, wt_color(0, 30, 40));
        }
    }
    
    // Seconds (6 bits, cols 13-18)
    for (uint8_t bit = 0; bit < 6; ++bit) {
        if (second & (1 << (5 - bit))) {
            for (uint8_t y = 1; y < 6; ++y)
                wt_display_set_pixel_xy(13 + bit, y, colSc);
        } else {
            wt_display_set_pixel_xy(13 + bit, 3, wt_color(20, 15, 40));
        }
    }
    
    // Timeline: Weather forecast (reusing weather rendering logic)
    Settings& cfg = settings_get();
    uint8_t hoursPerLed = cfg.forecastHours / WT_TIMELINE_PIXELS;
    if (hoursPerLed < 1) hoursPerLed = 1;
    
    for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i)
    {
        uint8_t forecastIdx = i * hoursPerLed;
        if (forecastIdx >= 12) forecastIdx = 11;
        wt_timeline_set_pixel(i, weatherColor(weather_get_forecast(forecastIdx).type));
    }
}

// Watchface 2: Vertical Stack (Hour Top, Min Bottom)
static void clock_render_minimal()
{
    int hour; uint8_t minute, second;
    getClockTime(hour, minute, second);

    // Color varies by time of day
    uint32_t col;
    if (hour >= 6 && hour < 10) col = wt_color(255, 200, 100); // Morning
    else if (hour >= 10 && hour < 17) col = wt_color(100, 200, 255); // Day
    else if (hour >= 17 && hour < 21) col = wt_color(255, 150, 50); // Evening
    else col = wt_color(150, 100, 255); // Night
    
    uint8_t hTens = hour / 10;
    uint8_t hOnes = hour % 10;
    uint8_t mTens = minute / 10;
    uint8_t mOnes = minute % 10;

    // Centered layout "12:34"
    uint8_t x = 2;
    drawDigit(x, 0, hTens, col); x += 4;
    drawDigit(x, 0, hOnes, col); x += 4;
    
    // Pulsing colon
    if (millis() % 1000 < 500) {
        wt_display_set_pixel_xy(x, 2, col);
        wt_display_set_pixel_xy(x, 4, col);
    }
    x += 2;
    
    drawDigit(x, 0, mTens, col); x += 4;
    drawDigit(x, 0, mOnes, col);
    
    // Timeline: Seconds filling up
    uint8_t secFill = (second * WT_TIMELINE_PIXELS) / 60;
    for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i) {
        if (i < secFill) wt_timeline_set_pixel(i, col);
    }
}

// Watchface 3: Progress bars (hour/minute/second as horizontal bars)
static void clock_render_bars()
{
    int hour; uint8_t minute, second;
    getClockTime(hour, minute, second);

    // Hour bar (row 5-6, spans 0-19 based on 0-23 hours)
    uint8_t hourLen = (hour * WT_MATRIX_WIDTH) / 24;
    for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x) {
        uint32_t col = (x < hourLen) ? wt_color(255, 100, 50) : wt_color(40, 15, 8);
        wt_display_set_pixel_xy(x, 5, col);
        wt_display_set_pixel_xy(x, 6, col);
    }
    
    // Minute bar (row 2-3, spans based on 0-59 minutes)
    uint8_t minLen = (minute * WT_MATRIX_WIDTH) / 60;
    for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x) {
        uint32_t col = (x < minLen) ? wt_color(50, 200, 255) : wt_color(8, 30, 40);
        wt_display_set_pixel_xy(x, 2, col);
        wt_display_set_pixel_xy(x, 3, col);
    }
    
    // Second indicator (row 0, single moving pixel)
    uint8_t secPos = (second * WT_MATRIX_WIDTH) / 60;
    for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x) {
        uint32_t col = (x == secPos) ? wt_color(150, 255, 150) : wt_color(15, 30, 15);
        wt_display_set_pixel_xy(x, 0, col);
    }

    // Timeline: minute markers
    for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i) {
        uint8_t mins = (i * 60) / WT_TIMELINE_PIXELS;
        if (minute >= mins)
            wt_timeline_set_pixel(i, wt_color(50, 150, 200));
        else
            wt_timeline_set_pixel(i, wt_color(10, 30, 40));
    }
}

// Watchface 4: Nixie
static void clock_render_nixie() {
    int hour; uint8_t minute, second;
    getClockTime(hour, minute, second);
    uint32_t now = millis();
    
    // Animated glow intensity
    float glow = 0.85f + 0.15f * sinf(now * 0.003f);
    uint32_t orange = wt_color((uint8_t)(255 * glow), (uint8_t)(120 * glow), (uint8_t)(20 * glow));
    uint32_t dimBg = wt_color(8, 3, 0);  // Very dark background
    uint32_t glowBg = wt_color(25, 10, 2); // Subtle glow around digits
    
    // Dark background first
    for(int x = 0; x < WT_MATRIX_WIDTH; ++x) {
        for(int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
            wt_display_set_pixel_xy(x, y, dimBg);
        }
    }
    
    // Draw glow halos behind digit positions
    int positions[4] = {1, 5, 10, 14};
    for (int p = 0; p < 4; ++p) {
        int dx = positions[p];
        for (int ox = -1; ox <= 4; ++ox) {
            for (int oy = -1; oy <= 6; ++oy) {
                int px = dx + ox;
                int py = oy;
                if (px >= 0 && px < WT_MATRIX_WIDTH && py >= 0 && py < WT_MATRIX_HEIGHT) {
                    wt_display_set_pixel_xy(px, py, glowBg);
                }
            }
        }
    }
    
    // Digits with bright orange
    uint8_t h1 = hour / 10, h2 = hour % 10, m1 = minute / 10, m2 = minute % 10;
    drawDigit(1, 0, h1, orange);
    drawDigit(5, 0, h2, orange);
    drawDigit(10, 0, m1, orange);
    drawDigit(14, 0, m2, orange);
    
    // Colon with flicker
    if ((now / 500) % 2) {
        wt_display_set_pixel_xy(9, 2, orange);
        wt_display_set_pixel_xy(9, 4, orange);
    }
}

// Watchface 5: Glitch
static void clock_render_glitch() {
    int hour; uint8_t minute, second;
    getClockTime(hour, minute, second);
    static uint32_t lastGlitch = 0;
    static uint32_t glitchCol = 0;
    static int glitchHour = 0;
    uint32_t now = millis();
    
    uint32_t col = wt_color(0, 255, 0); // Matrix green
    int displayHour = hour;
    
    // Occasional glitch - less frequent (every ~2 seconds on average)
    if (now - lastGlitch > 2000 && random(100) == 0) {
        lastGlitch = now;
        glitchCol = wt_color(random(255), random(255), random(255));
        glitchHour = random(24);
    }
    
    // Show glitch for 150ms
    if (now - lastGlitch < 150) {
        col = glitchCol;
        displayHour = glitchHour;
    }
    
    uint8_t x = 2;
    drawDigit(x, 0, displayHour / 10, col); x += 4;
    drawDigit(x, 0, displayHour % 10, col); x += 4;
    x += 2; // Colon space
    drawDigit(x, 0, minute / 10, col); x += 4;
    drawDigit(x, 0, minute % 10, col);
}

// Watchface 6: Pong - Hour=left score, Minute=right score, ball bounces with seconds
static void clock_render_pong() {
    int hour; uint8_t minute, second;
    getClockTime(hour, minute, second);
    
    // Draw center divider line (dotted)
    for (int y = 0; y < 7; y += 2) {
        wt_display_set_pixel_xy(10, y, wt_color(50, 50, 50));
    }
    
    // Left paddle - position based on seconds (bouncing)
    int ly = (second % 10);
    if (ly > 5) ly = 10 - ly;
    for (int i = 0; i < 3; ++i) {
        if (ly + i < 7) wt_display_set_pixel_xy(1, ly + i, wt_color(255, 255, 255));
    }
    
    // Right paddle - position based on seconds offset
    int ry = ((second + 3) % 10);
    if (ry > 5) ry = 10 - ry;
    for (int i = 0; i < 3; ++i) {
        if (ry + i < 7) wt_display_set_pixel_xy(18, ry + i, wt_color(255, 255, 255));
    }
    
    // Ball bouncing based on seconds
    int bx = 3 + (second % 14);
    if (bx > 16) bx = 30 - bx;
    int by = (second % 12);
    if (by > 6) by = 12 - by;
    wt_display_set_pixel_xy(bx, by, wt_color(255, 255, 0));
    
    // Scores: Hour on left, Minute on right (small digits at y=0)
    drawChar3x5(3, 1, '0' + hour / 10, wt_color(100, 100, 255));
    drawChar3x5(6, 1, '0' + hour % 10, wt_color(100, 100, 255));
    drawChar3x5(12, 1, '0' + minute / 10, wt_color(255, 100, 100));
    drawChar3x5(15, 1, '0' + minute % 10, wt_color(255, 100, 100));
}

// Watchface 7: Word Clock (shows "HALF PAST TEN" etc as text)
static void clock_render_word() {
    int hour; uint8_t minute, second;
    getClockTime(hour, minute, second);
    
    // Simplified word clock - scroll words
    const char* hourWords[] = {"TWELVE", "ONE", "TWO", "THREE", "FOUR", "FIVE", 
                               "SIX", "SEVEN", "EIGHT", "NINE", "TEN", "ELEVEN"};
    
    uint8_t h12 = hour % 12;
    uint8_t minBlock = minute / 15; // 0=o'clock, 1=quarter, 2=half, 3=quarter to
    
    static int16_t scrollX = 20;
    static uint32_t lastScroll = 0;
    if (millis() - lastScroll > 80) {
        scrollX--;
        lastScroll = millis();
    }
    
    // Build phrase
    String phrase;
    if (minBlock == 0) {
        phrase = hourWords[h12];
    } else if (minBlock == 1) {
        phrase = String("QUARTER PAST ") + hourWords[h12];
    } else if (minBlock == 2) {
        phrase = String("HALF PAST ") + hourWords[h12];
    } else {
        phrase = String("QUARTER TO ") + hourWords[(h12 + 1) % 12];
    }
    
    // Draw scrolling text
    uint32_t col = wt_color(255, 200, 100);
    for (int i = 0; i < (int)phrase.length(); ++i) {
        int16_t x = scrollX + i * 5;
        if (x > -5 && x < WT_MATRIX_WIDTH) {
            drawChar3x5(x, 0, phrase[i], col);
        }
    }
    
    int16_t totalW = phrase.length() * 5;
    if (scrollX < -totalW) scrollX = 20;
    
    // Seconds dots on timeline
    uint8_t secDot = (second * WT_TIMELINE_PIXELS) / 60;
    for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i) {
        if (i <= secDot) wt_timeline_set_pixel(i, wt_color(255, 200, 100));
    }
}

// Watchface 8: Bouncing ball clock
static void clock_render_bounce() {
    int hour; uint8_t minute, second;
    getClockTime(hour, minute, second);
    static uint32_t lastUpdate = 0;
    uint32_t now = millis();
    
    // Each digit as a bouncing ball
    static float y[4] = {3, 3, 3, 3};
    static float vy[4] = {0.1f, 0.15f, 0.12f, 0.08f};
    
    uint8_t digits[4] = {(uint8_t)(hour / 10), (uint8_t)(hour % 10), 
                          (uint8_t)(minute / 10), (uint8_t)(minute % 10)};
    
    // Slow down physics - only update every 50ms
    if (now - lastUpdate > 50) {
        lastUpdate = now;
        for (int i = 0; i < 4; ++i) {
            y[i] += vy[i];
            vy[i] += 0.03f; // Lighter gravity
            
            if (y[i] >= 5) {
                y[i] = 5;
                vy[i] = -vy[i] * 0.75f; // Bounce
                if (vy[i] > -0.08f) vy[i] = -0.2f - (second % 3) * 0.05f;
            }
        }
    }
    
    // Draw digits - offset so they stay within bounds
    uint32_t cols[4] = {wt_color(255, 100, 100), wt_color(255, 200, 100),
                        wt_color(100, 200, 255), wt_color(100, 255, 150)};
    
    int xpos[4] = {1, 5, 11, 15};
    for (int i = 0; i < 4; ++i) {
        int dy = (int)y[i] - 4; // Moved down one pixel
        if (dy < 0) dy = 0;
        if (dy > 1) dy = 1;
        drawDigit(xpos[i], dy, digits[i], cols[i]);
    }
    
    // Colon
    uint32_t colonCol = (second % 2) ? wt_color(255, 255, 255) : wt_color(50, 50, 50);
    wt_display_set_pixel_xy(9, 2, colonCol);
    wt_display_set_pixel_xy(9, 4, colonCol);
}

// Watchface 9: Matrix-style falling time
static void clock_render_matrix_clock() {
    int hour; uint8_t minute, second;
    getClockTime(hour, minute, second);
    static uint32_t lastUpdate = 0;
    uint32_t now = millis();
    
    static uint8_t trails[WT_MATRIX_WIDTH][WT_MATRIX_HEIGHT];
    static uint8_t drops[WT_MATRIX_WIDTH];
    static bool init = false;
    
    if (!init) {
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) drops[x] = random(7);
        init = true;
    }
    
    // Slow down - update every 80ms
    if (now - lastUpdate > 80) {
        lastUpdate = now;
        
        // Fade trails slowly
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
                if (trails[x][y] > 0) trails[x][y] -= (trails[x][y] > 20) ? 20 : trails[x][y];
            }
        }
        
        // Update drops - less frequently
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            if (random(15) == 0) {
                drops[x]--;
                if (drops[x] > 200) drops[x] = 6;
                trails[x][drops[x]] = 255;
            }
        }
    }
    
    // Render trails - much dimmer than digits
    for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
        for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
            if (trails[x][y] > 0) {
                // Dim trails - max 60 brightness
                uint8_t b = trails[x][y] / 4;
                wt_display_set_pixel_xy(x, y, wt_color(0, b, b / 4));
            }
        }
    }
    
    // Draw time on top - bright and clear
    uint32_t bright = wt_color(0, 255, 100);
    drawDigit(2, 0, hour / 10, bright);
    drawDigit(6, 0, hour % 10, bright);
    wt_display_set_pixel_xy(10, 2, bright);
    wt_display_set_pixel_xy(10, 4, bright);
    drawDigit(12, 0, minute / 10, bright);
    drawDigit(16, 0, minute % 10, bright);
}

// Watchface 10: Radar sweep clock - shows HH:MM with second hand sweep
static void clock_render_radar() {
    int hour; uint8_t minute, second;
    getClockTime(hour, minute, second);
    uint32_t now = millis();
    
    uint32_t bgDark = wt_color(0, 15, 10);
    uint32_t gridCol = wt_color(0, 40, 25);
    uint32_t sweepCol = wt_color(0, 255, 100);
    uint32_t digitCol = wt_color(0, 200, 80);
    
    // Dark background
    for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
        for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
            wt_display_set_pixel_xy(x, y, bgDark);
        }
    }
    
    // Concentric ring markers (subtle)
    int cx = 16, cy = 3;
    for (int i = 0; i < 12; ++i) {
        float a = i * 3.14159f / 6.0f;
        int px = cx + (int)(cosf(a) * 3);
        int py = cy + (int)(sinf(a) * 2.5f);
        if (px >= 0 && px < WT_MATRIX_WIDTH && py >= 0 && py < WT_MATRIX_HEIGHT) {
            wt_display_set_pixel_xy(px, py, gridCol);
        }
    }
    
    // Second hand sweep (rotates clockwise from 12 o'clock)
    float secAngle = (second + (now % 1000) / 1000.0f) / 60.0f * 2 * 3.14159f - 3.14159f / 2;
    
    // Sweep trail (fading)
    for (int t = 0; t < 8; ++t) {
        float trailAngle = secAngle - t * 0.08f;
        uint8_t b = 255 - t * 30;
        for (int r = 1; r <= 3; ++r) {
            int px = cx + (int)(cosf(trailAngle) * r * 1.2f);
            int py = cy - (int)(sinf(trailAngle) * r * 0.7f);
            if (px >= 0 && px < WT_MATRIX_WIDTH && py >= 0 && py < WT_MATRIX_HEIGHT) {
                wt_display_set_pixel_xy(px, py, wt_color(0, b, b/2));
            }
        }
    }
    
    // Center blip
    wt_display_set_pixel_xy(cx, cy, sweepCol);
    
    // Time display on left - HH:MM
    drawChar3x5(0, 1, '0' + hour / 10, digitCol);
    drawChar3x5(4, 1, '0' + hour % 10, digitCol);
    // Blinking colon
    if ((now / 500) % 2) {
        wt_display_set_pixel_xy(8, 2, digitCol);
        wt_display_set_pixel_xy(8, 4, digitCol);
    }
    drawChar3x5(9, 1, '0' + minute / 10, digitCol);
    drawChar3x5(13, 1, '0' + minute % 10, digitCol);
}

// Watchface 11: Flip clock (retro split-flap style) - animated
static void clock_render_flip() {
    int hour; uint8_t minute, second;
    getClockTime(hour, minute, second);
    uint32_t now = millis();
    
    uint32_t cardBg = wt_color(35, 35, 40);   // Card background
    uint32_t white = wt_color(255, 255, 240); // Digit color - bright
    uint32_t splitLine = wt_color(25, 25, 30);// Subtle split line
    
    // Fill card backgrounds (4 cards)
    int cardX[4] = {0, 5, 10, 15};
    for (int c = 0; c < 4; ++c) {
        for (int x = cardX[c]; x < cardX[c] + 4 && x < WT_MATRIX_WIDTH; ++x) {
            for (int y = 0; y < 7; ++y) {
                wt_display_set_pixel_xy(x, y, cardBg);
            }
        }
    }
    
    // Draw digits
    drawDigit(0, 0, hour / 10, white);
    drawDigit(5, 0, hour % 10, white);
    drawDigit(10, 0, minute / 10, white);
    drawDigit(15, 0, minute % 10, white);
    
    // Subtle split line - only draw where no digit pixels
    // The split is a very subtle darker line at y=3
    for (int c = 0; c < 4; ++c) {
        int cx = cardX[c];
        // Just darken the gaps between cards
        if (c > 0) {
            wt_display_set_pixel_xy(cx - 1, 3, splitLine);
        }
    }
    
    // Flip animation for seconds digit (rightmost)
    static uint8_t lastSec = 255;
    static uint32_t flipStart = 0;
    if (second != lastSec) {
        lastSec = second;
        flipStart = now;
    }
    
    // Colon between hour and minute
    wt_display_set_pixel_xy(9, 2, white);
    wt_display_set_pixel_xy(9, 4, white);
}

// Watchface 12: Cyberpunk neon
static void clock_render_cyber() {
    int hour; uint8_t minute, second;
    getClockTime(hour, minute, second);
    
    uint32_t now = millis();
    // Slower flicker animation
    float flicker = 0.85f + 0.15f * sinf(now * 0.003f);
    float flicker2 = 0.85f + 0.15f * sinf(now * 0.0025f + 1.0f);
    uint32_t neonPink = wt_color((uint8_t)(255 * flicker), 0, (uint8_t)(200 * flicker));
    uint32_t neonCyan = wt_color(0, (uint8_t)(255 * flicker2), (uint8_t)(255 * flicker2));
    uint32_t bgGlow = wt_color(10, 5, 15); // Dark purple bg
    
    // Background glow
    for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
        for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
            wt_display_set_pixel_xy(x, y, bgGlow);
        }
    }
    
    drawDigit(1, 0, hour / 10, neonPink);
    drawDigit(5, 0, hour % 10, neonPink);
    // Slower blinking colon
    if ((now / 800) % 2) {
        wt_display_set_pixel_xy(9, 2, neonCyan);
        wt_display_set_pixel_xy(9, 4, neonCyan);
    }
    drawDigit(11, 0, minute / 10, neonCyan);
    drawDigit(15, 0, minute % 10, neonCyan);
}

// Watchface 13: Analog clock face - full screen
static void clock_render_analog() {
    int hour; uint8_t minute, second;
    getClockTime(hour, minute, second);
    uint32_t now = millis();
    
    // Use full display - center at middle
    int cx = 10, cy = 3;
    uint32_t face = wt_color(30, 30, 40);     // Hour markers
    uint32_t hourCol = wt_color(255, 180, 50); // Hour hand - warm
    uint32_t minCol = wt_color(80, 180, 255);  // Minute hand - cool
    uint32_t secCol = wt_color(255, 50, 50);   // Second hand - red
    
    // Draw hour markers around the edge (12 positions)
    for (int i = 0; i < 12; ++i) {
        float a = i * 3.14159f / 6.0f - 3.14159f / 2.0f;
        // Use wide ellipse to fill screen
        int px = cx + (int)(cosf(a) * 9);
        int py = cy + (int)(sinf(a) * 3);
        if (px >= 0 && px < WT_MATRIX_WIDTH && py >= 0 && py < WT_MATRIX_HEIGHT) {
            // Brighter at 12, 3, 6, 9
            uint32_t markerCol = (i % 3 == 0) ? wt_color(80, 80, 100) : face;
            wt_display_set_pixel_xy(px, py, markerCol);
        }
    }
    
    // Hour hand - short and thick (negate sin for correct clock direction)
    float hAngle = ((hour % 12) + minute / 60.0f) * 3.14159f / 6.0f - 3.14159f / 2.0f;
    for (int r = 1; r <= 4; ++r) {
        int px = cx + (int)(cosf(hAngle) * r * 1.8f);
        int py = cy - (int)(sinf(hAngle) * r * 0.6f); // Negated for clock direction
        if (px >= 0 && px < WT_MATRIX_WIDTH && py >= 0 && py < WT_MATRIX_HEIGHT) {
            wt_display_set_pixel_xy(px, py, hourCol);
        }
    }
    
    // Minute hand - longer (negate sin for correct clock direction)
    float mAngle = minute * 3.14159f / 30.0f - 3.14159f / 2.0f;
    for (int r = 1; r <= 7; ++r) {
        int px = cx + (int)(cosf(mAngle) * r * 1.3f);
        int py = cy - (int)(sinf(mAngle) * r * 0.45f); // Negated for clock direction
        if (px >= 0 && px < WT_MATRIX_WIDTH && py >= 0 && py < WT_MATRIX_HEIGHT) {
            wt_display_set_pixel_xy(px, py, minCol);
        }
    }
    
    // Second hand - thin, sweeping (negate sin for correct clock direction)
    float sAngle = (second + (now % 1000) / 1000.0f) * 3.14159f / 30.0f - 3.14159f / 2.0f;
    for (int r = 2; r <= 8; ++r) {
        int px = cx + (int)(cosf(sAngle) * r * 1.2f);
        int py = cy - (int)(sinf(sAngle) * r * 0.4f); // Negated for clock direction
        if (px >= 0 && px < WT_MATRIX_WIDTH && py >= 0 && py < WT_MATRIX_HEIGHT) {
            wt_display_set_pixel_xy(px, py, secCol);
        }
    }
    
    // Center dot
    wt_display_set_pixel_xy(cx, cy, wt_color(255, 255, 255));
}

static void clock_render()
{
    wt_display_clear();
    wt_timeline_clear();

    if (net_is_ap_mode() || !g_clockTimeValid)
    {
        // Error state
        uint32_t red = wt_color(255, 50, 50);
        wt_display_set_pixel_xy(10, 3, red);
        wt_display_set_pixel_xy(9, 2, red); wt_display_set_pixel_xy(11, 2, red);
        wt_display_set_pixel_xy(8, 1, red); wt_display_set_pixel_xy(12, 1, red);
        return;
    }

    // Switch between watchfaces
    switch (g_clockPreset) {
        case 0: clock_render_digital(); break;
        case 1: clock_render_binary(); break;
        case 2: clock_render_minimal(); break;
        case 3: clock_render_bars(); break;
        case 4: clock_render_nixie(); break;
        case 5: clock_render_glitch(); break;
        case 6: clock_render_pong(); break;
        case 7: clock_render_word(); break;
        case 8: clock_render_bounce(); break;
        case 9: clock_render_matrix_clock(); break;
        case 10: clock_render_radar(); break;
        case 11: clock_render_flip(); break;
        case 12: clock_render_cyber(); break;
        case 13: clock_render_analog(); break;
        default: clock_render_digital(); break;
    }
}

static uint32_t weatherColor(uint8_t type)
{
    // Use pure, saturated colors for maximum punch
    switch (type)
    {
    case WEATHER_SUNNY:
    case WEATHER_CLEAR_NIGHT:
        return wt_color(255, 180, 0); // Deep Gold/Orange Sun
    case WEATHER_PARTLY_CLOUDY:
    case WEATHER_CLOUDY:
    case WEATHER_FOG:
        return wt_color(100, 100, 120); // Blue-ish Grey
    case WEATHER_RAIN:
    case WEATHER_DRIZZLE:
    case WEATHER_HEAVY_RAIN:
        return wt_color(0, 0, 255); // Pure Blue
    case WEATHER_STORM:
        return wt_color(100, 0, 200); // Deep Purple
    case WEATHER_SNOW:
    case WEATHER_SLEET:
        return wt_color(255, 255, 255); // Pure White
    case WEATHER_WIND:
        return wt_color(0, 255, 200); // Cyan/Turquoise
    default:
        return wt_color(255, 255, 0);
    }
}

// Temperature to color based on palette setting
static uint32_t tempToColor(int8_t temp)
{
    uint8_t palette = settings_get().tempPalette;
    
    if (palette == 1) {
        // Cool tones (purple-blue-cyan) - for cold regions
        if (temp < -20) return wt_color(80, 0, 120);      // Deep purple
        if (temp < -10) return wt_color(100, 50, 150);    // Purple
        if (temp < 0)   return wt_color(80, 80, 180);     // Blue-purple
        if (temp < 10)  return wt_color(60, 120, 200);    // Blue
        if (temp < 20)  return wt_color(80, 180, 220);    // Cyan-blue
        if (temp < 30)  return wt_color(100, 220, 220);   // Cyan
        return wt_color(150, 255, 255);                   // Bright cyan
    }
    else if (palette == 2) {
        // Warm tones (yellow-orange-red) - for warm regions  
        if (temp < 0)   return wt_color(255, 255, 200);   // Pale yellow
        if (temp < 10)  return wt_color(255, 255, 100);   // Yellow
        if (temp < 20)  return wt_color(255, 200, 50);    // Golden
        if (temp < 30)  return wt_color(255, 150, 50);    // Orange
        if (temp < 40)  return wt_color(255, 80, 50);     // Red-orange
        return wt_color(255, 50, 50);                     // Red
    }
    else {
        // Default palette (blue-cyan-green-yellow-red)
        if (temp < -10) return wt_color(80, 80, 200);     // Blue
        if (temp < 0)   return wt_color(100, 150, 255);   // Light blue
        if (temp < 10)  return wt_color(100, 220, 200);   // Cyan
        if (temp < 20)  return wt_color(150, 255, 100);   // Green
        if (temp < 30)  return wt_color(255, 220, 50);    // Yellow
        return wt_color(255, 80, 80);                     // Red
    }
}

static void weather_setup()
{
    g_weatherAnimFrame = 0;
    g_weatherLastAnim = 0;
}

static void weather_update(uint32_t now, uint32_t dt)
{
    // Animation speed: 1=slow(80ms), 5=normal(40ms), 10=fast(15ms)
    uint8_t speed = settings_get().animSpeed;
    uint32_t frameTime = 100 - speed * 8;  // 92ms to 20ms
    if (frameTime < 15) frameTime = 15;
    
    if (now - g_weatherLastAnim >= frameTime)
    {
        g_weatherLastAnim = now;
        g_weatherAnimFrame++;
    }
}

// Full-matrix animated weather backgrounds - designed to be recognizable
static uint32_t drawSunnyBg(uint8_t x, uint8_t y, uint32_t frame)
{
    // Bright Blue Sky, Yellow/Orange Sun
    // Sun at top-right
    float cx = 15.0f, cy = 6.0f;
    float dx = x - cx, dy = y - cy;
    float dist = sqrtf(dx * dx + dy * dy);
    
    // Sun Core
    if (dist < 3.5f) return wt_color(255, 200, 0); // Gold
    
    // Rays
    float angle = atan2f(dy, dx);
    float ray = fmodf(angle + frame * 0.1f, 0.8f);
    if (dist < 6.0f && ray < 0.4f) return wt_color(255, 150, 0); // Orange
    
    return wt_color(0, 150, 255); // Sky Blue
}

static uint32_t drawCloudyBg(uint8_t x, uint8_t y, uint32_t frame)
{
    // Scrolling clouds
    float t = frame * 0.05f;
    // Noise-like pattern
    float n = sinf(x * 0.3f + t) + sinf(y * 0.5f - t * 0.5f);
    
    if (n > 0.5f) return wt_color(200, 200, 200); // White cloud
    if (n > 0.0f) return wt_color(120, 120, 140); // Grey cloud
    return wt_color(60, 80, 120); // Dark Blue Sky
}

static uint32_t drawRainBg(uint8_t x, uint8_t y, uint32_t frame)
{
    // Dark Blue Sky + Blue Drops
    // Random drops
    if ((x * 7 + y * 13 + frame) % 17 == 0) return wt_color(100, 200, 255); // Light Blue Drop
    
    // BG
    return wt_color(0, 0, 80); // Dark Blue
}

static uint32_t drawStormBg(uint8_t x, uint8_t y, uint32_t frame)
{
    // Flash
    if (frame % 60 < 3) return wt_color(255, 255, 255);
    
    // Rain
    if ((x * 7 + y * 13 + frame) % 13 == 0) return wt_color(200, 200, 255);
    
    return wt_color(20, 0, 40); // Very Dark Purple
}

static uint32_t drawSnowBg(uint8_t x, uint8_t y, uint32_t frame)
{
    // Falling white pixels
    float t = frame * 0.1f;
    float fy = 7.0f - fmodf(y + t, 8.0f);
    // Simplified snow: just check pixel hash
    if ((uint8_t)(x * 17 + y * 5 + frame/2) % 23 == 0) return wt_color(255, 255, 255);
    
    return wt_color(10, 20, 40); // Dark Blue/Black
}

static uint32_t drawWindBg(uint8_t x, uint8_t y, uint32_t frame)
{
    // Horizontal streaks
    int streak = (x + frame) % 20;
    if (streak < 10 && y % 2 == 0) return wt_color(100, 255, 200); // Cyan wind
    return wt_color(0, 50, 100); // Dark Teal
}

// Helper: Build text mask for temperature
static uint8_t g_textMask[WT_MATRIX_WIDTH]; // Bitmask per column (7 bits each)

static void buildTempMask(int8_t temp, bool negative)
{
    memset(g_textMask, 0, sizeof(g_textMask));
    if (temp < 0) temp = -temp;
    if (temp > 99) temp = 99;
    
    uint8_t tens = temp / 10;
    uint8_t ones = temp % 10;
    uint8_t numDigits = (tens > 0) ? 2 : 1;
    uint8_t totalW = numDigits * (DIGIT_W + 1) + (negative ? 4 : 0) + 2;
    uint8_t startX = (WT_MATRIX_WIDTH - totalW) / 2;
    uint8_t cursor = startX;

    // Minus sign
    if (negative) {
        uint8_t yMid = DIGIT_H / 2;
        for (uint8_t dx = 0; dx < 3; ++dx)
            if (cursor + dx < WT_MATRIX_WIDTH) g_textMask[cursor + dx] |= (1 << yMid);
        cursor += 4;
    }
    
    // Digits
    auto addDigit = [&](uint8_t d) {
        if (d > 9) return;
        for (uint8_t row = 0; row < DIGIT_H; ++row) {
            uint8_t bits = sprites_get_digit_row(d, row);
            for (uint8_t col = 0; col < DIGIT_W; ++col) {
                if ((bits & (1 << (DIGIT_W - 1 - col))) && cursor + col < WT_MATRIX_WIDTH)
                    g_textMask[cursor + col] |= (1 << (DIGIT_H - 1 - row));
            }
        }
        cursor += DIGIT_W + 1;
    };
    
    if (tens > 0) addDigit(tens);
    addDigit(ones);
    
    // Degree symbol
    if (cursor < WT_MATRIX_WIDTH - 1) {
        g_textMask[cursor] |= (1 << (DIGIT_H - 1)) | (1 << (DIGIT_H - 2));
        g_textMask[cursor + 1] |= (1 << (DIGIT_H - 1)) | (1 << (DIGIT_H - 2));
    }
}

static void weather_render_classic()
{
    wt_display_clear();
    WeatherData current = weather_get_current();
    
    // Icon on left (0,0)
    drawWeatherIcon(0, 0, current.type, g_weatherAnimFrame);

    // Temp on right (right-aligned)
    int8_t temp = current.temp;
    bool negative = temp < 0;
    if (negative) temp = -temp;
    if (temp > 99) temp = 99;

    // Color from temperature palette
    uint32_t tCol = tempToColor(current.temp);

    uint8_t tens = temp / 10;
    uint8_t ones = temp % 10;
    uint8_t numDigits = (tens > 0) ? 2 : 1;
    
    // Align from right edge
    uint8_t cursor = WT_MATRIX_WIDTH - (numDigits * 4 + (negative ? 4 : 0) + 2);
    
    // Fix Overlap: if icon is wide (e.g. 13px) and text starts at 10px, we have overlap.
    // Shift text slightly if needed? Or just let it clip the icon?
    // Text is more important. Clear the background behind text.
    if (cursor < 11) {
        // Clear rect behind text
        for(uint8_t cx=cursor; cx<WT_MATRIX_WIDTH; cx++) {
            for(uint8_t cy=0; cy<WT_MATRIX_HEIGHT; cy++) {
                wt_display_set_pixel_xy(cx, cy, 0);
            }
        }
    }

    if (negative) {
        for (uint8_t dx = 0; dx < 3; ++dx)
            wt_display_set_pixel_xy(cursor + dx, DIGIT_H / 2, tCol);
        cursor += 4;
    }
    // No plus sign

    if (tens > 0) { drawDigit(cursor, 0, tens, tCol); cursor += DIGIT_W + 1; }
    drawDigit(cursor, 0, ones, tCol); cursor += DIGIT_W + 1;
    
    // Degree symbol near top
    if (cursor < WT_MATRIX_WIDTH) {
        wt_display_set_pixel_xy(cursor, 5, tCol);
        wt_display_set_pixel_xy(cursor + 1, 5, tCol);
        wt_display_set_pixel_xy(cursor, 4, tCol);
        wt_display_set_pixel_xy(cursor + 1, 4, tCol);
    }
}


// Fullscreen Style 1: Large centered temperature with weather icon
static void weather_render_fs_bar()
{
    WeatherData current = weather_get_current();
    uint32_t tCol = tempToColor(current.temp);
    
    // Draw digits on left
    int8_t temp = current.temp;
    bool negative = temp < 0;
    if (negative) temp = -temp;
    
    uint8_t tens = temp / 10;
    uint8_t ones = temp % 10;
    
    uint8_t x = 0;
    if (negative) {
        wt_display_set_pixel_xy(x, 3, tCol);
        wt_display_set_pixel_xy(x+1, 3, tCol);
        x += 3;
    }
    if (tens > 0) { drawDigit(x, 0, tens, tCol); x += 4; }
    drawDigit(x, 0, ones, tCol); x += 4;
    
    // Degree (Fixed Y position: 5 is top-ish)
    wt_display_set_pixel_xy(x, 5, tCol);
    wt_display_set_pixel_xy(x+1, 5, tCol);
    wt_display_set_pixel_xy(x, 4, tCol);
    wt_display_set_pixel_xy(x+1, 4, tCol);
    
    // Icon on right - Right aligned
    const SpriteData* s = sprites_get((SpriteType)(SPRITE_ICON_SUN + current.type));
    if (s) {
        uint8_t iconX = WT_MATRIX_WIDTH - s->width;
        drawWeatherIcon(iconX, 0, current.type, g_weatherAnimFrame);
    }
}

// Fullscreen Style 2: Split Screen - Icon Left, Temp Right
static void weather_render_fs_corner()
{
    WeatherData current = weather_get_current();
    
    // Solid Background Color based on weather type
    uint32_t bgCol = weatherColor(current.type);
    // Darken significantly for contrast with white text (1/5th brightness)
    uint8_t r = ((bgCol >> 16) & 0xFF) / 5; 
    uint8_t g = ((bgCol >> 8) & 0xFF) / 5;
    uint8_t b = (bgCol & 0xFF) / 5;
    bgCol = wt_color(r, g, b);

    for(int i=0; i<WT_MATRIX_WIDTH; i++) {
        for(int j=0; j<WT_MATRIX_HEIGHT; j++) {
            wt_display_set_pixel_xy(i, j, bgCol);
        }
    }

    // Large Weather Icon on Left
    drawWeatherIcon(0, 0, current.type, g_weatherAnimFrame);
    
    // Temp on Right (White text for contrast on dark bg)
    int8_t temp = current.temp;
    bool negative = temp < 0;
    if (negative) temp = -temp;
    
    uint32_t textCol = wt_color(255, 255, 255); // White text

    // Right align digits
    uint8_t numDigits = (temp >= 10) ? 2 : 1;
    uint8_t width = numDigits * 4 + (negative ? 3 : 0);
    uint8_t x = WT_MATRIX_WIDTH - width - 3; // Leave room for degree (2px)
    
    if (negative) {
        wt_display_set_pixel_xy(x, 3, textCol); x += 3; // Smaller minus
    }
    if (temp >= 10) {
        drawDigit(x, 0, temp/10, textCol); x += 4;
    }
    drawDigit(x, 0, temp%10, textCol); x += 4;
    
    // Degree
    wt_display_set_pixel_xy(x, 5, textCol);
    wt_display_set_pixel_xy(x+1, 5, textCol);
    wt_display_set_pixel_xy(x, 4, textCol);
    wt_display_set_pixel_xy(x+1, 4, textCol);
}

// Fullscreen Style 3: Clean animated with weather icon overlay
static void weather_render_fs_animated()
{
    WeatherData current = weather_get_current();
    
    // Draw weather animation on left half
    for (uint8_t x = 0; x < 12; ++x) {
        for (uint8_t y = 0; y < WT_MATRIX_HEIGHT; ++y) {
            uint32_t bg;
            switch (current.type) {
            case WEATHER_SUNNY: bg = drawSunnyBg(x, y, g_weatherAnimFrame); break;
            case WEATHER_CLOUDY:
            case WEATHER_PARTLY_CLOUDY: bg = drawCloudyBg(x, y, g_weatherAnimFrame); break;
            case WEATHER_RAIN:
            case WEATHER_DRIZZLE:
            case WEATHER_HEAVY_RAIN: bg = drawRainBg(x, y, g_weatherAnimFrame); break;
            case WEATHER_STORM: bg = drawStormBg(x, y, g_weatherAnimFrame); break;
            case WEATHER_SNOW:
            case WEATHER_SLEET: bg = drawSnowBg(x, y, g_weatherAnimFrame); break;
            case WEATHER_WIND: bg = drawWindBg(x, y, g_weatherAnimFrame); break;
            default: bg = wt_color(20, 25, 35); break;
            }
            // Show animation at full brightness
            wt_display_set_pixel_xy(x, y, bg);
        }
    }
    
    // Right side: temperature display on black
    for (uint8_t x = 12; x < WT_MATRIX_WIDTH; ++x) {
        for (uint8_t y = 0; y < WT_MATRIX_HEIGHT; ++y) {
            wt_display_set_pixel_xy(x, y, 0);
        }
    }
    
    // Add a semi-transparent box behind the text for legibility
    for (uint8_t x = 13; x < WT_MATRIX_WIDTH; ++x) {
         for (uint8_t y = 1; y < 6; ++y) {
             // Only draw if the background is bright, but we just cleared it to black above?
             // Ah, "Background mostly white, temp white".
             // The loop above clears x >= 12 to black.
             // So the temp should be visible.
             // Maybe the user meant the animation bleeds over?
             // Let's ensure the text color is high contrast.
         }
    }

    // Temperature color from palette - Ensure it's not white if background is white
    uint32_t tCol = tempToColor(current.temp);
    // Make it fully saturated/bright
    // Or just use white text on black background (which we established above).
    // User said "temp completely white, looks bad".
    // Maybe they want color? tempToColor returns color.
    
    // Draw temperature on right
    int8_t temp = current.temp;
    bool negative = temp < 0;
    if (negative) temp = -temp;
    if (temp > 99) temp = 99;
    
    uint8_t tens = temp / 10;
    uint8_t ones = temp % 10;
    uint8_t x = 13;
    
    if (negative) {
        wt_display_set_pixel_xy(x, 3, tCol);
        wt_display_set_pixel_xy(x+1, 3, tCol);
        x += 3;
    }
    if (tens > 0) { drawDigit(x, 0, tens, tCol); x += DIGIT_W + 1; }
    drawDigit(x, 0, ones, tCol);
}

// Weather Preset 6: Terminal - clean command line look
static void weather_render_terminal() {
    wt_display_clear();
    WeatherData current = weather_get_current();
    uint32_t green = wt_color(0, 255, 80);
    uint32_t dimGreen = wt_color(0, 100, 30);
    
    int8_t t = current.temp;
    bool neg = t < 0;
    t = abs(t);
    if(t>99) t=99;
    
    // Draw prompt ">" at left (simple arrow shape)
    wt_display_set_pixel_xy(0, 4, green);
    wt_display_set_pixel_xy(1, 3, green);
    wt_display_set_pixel_xy(0, 2, green);
    
    uint8_t x = 3;
    
    // Minus sign if negative
    if (neg) {
        wt_display_set_pixel_xy(x, 3, green);
        wt_display_set_pixel_xy(x+1, 3, green);
        x += 3;
    }
    
    // Temperature digits
    if (t >= 10) {
        drawDigit(x, 0, t/10, green);
        x += 4;
    }
    drawDigit(x, 0, t%10, green);
    x += 4;
    
    // Degree symbol (small circle)
    wt_display_set_pixel_xy(x, 5, green);
    wt_display_set_pixel_xy(x+1, 5, green);
    wt_display_set_pixel_xy(x, 6, green);
    wt_display_set_pixel_xy(x+1, 6, green);
    
    // Blinking horizontal cursor at end
    if ((millis()/500)%2) {
        uint8_t cx = WT_MATRIX_WIDTH - 3;
        wt_display_set_pixel_xy(cx, 0, green);
        wt_display_set_pixel_xy(cx+1, 0, green);
        wt_display_set_pixel_xy(cx+2, 0, green);
    }
}

// Weather Preset 7: Big Type
static void weather_render_bigtype() {
    wt_display_clear();
    WeatherData current = weather_get_current();
    uint32_t col = weatherColor(current.type);
    
    int8_t t = current.temp;
    uint8_t tens = abs(t) / 10;
    uint8_t ones = abs(t) % 10;
    
    // Center aligned approx
    uint8_t w = (tens > 0 ? 7 : 3); 
    uint8_t x = (WT_MATRIX_WIDTH - w) / 2;
    
    if (tens > 0) { drawDigit(x, 0, tens, col); x += 4; }
    drawDigit(x, 0, ones, col);
    
    // Condition color bar at bottom
    for(int i=0; i<WT_MATRIX_WIDTH; ++i) wt_display_set_pixel_xy(i, 0, col);
}

// Weather Preset 8: Forecast Strip
static void weather_render_forecast_strip() {
    wt_display_clear();
    // 3 columns: Now, +1h, +2h
    // Bars with color
    
    for(int i=0; i<3; ++i) {
         WeatherType type = (i==0) ? weather_get_current().type : weather_get_forecast(i).type;
         int8_t temp = (i==0) ? weather_get_current().temp : weather_get_forecast(i).temp;
         uint32_t col = weatherColor(type);
         
         // Map temp -10..30 to 1..7 height
         int h = (temp + 10) * 7 / 40;
         if (h<1) h=1; if (h>7) h=7;
         
         uint8_t x = 2 + i * 6;
         // Draw bar
         for(int y=0; y<h; ++y) {
             wt_display_set_pixel_xy(x, y, col);
             wt_display_set_pixel_xy(x+1, y, col);
             wt_display_set_pixel_xy(x+2, y, col);
             wt_display_set_pixel_xy(x+3, y, col);
         }
    }
}

// Weather Preset 9: Pixel Art Scene - cute diorama with weather
static void weather_render_pixel_art() {
    WeatherData current = weather_get_current();
    uint32_t now = millis();
    
    // Check if night
    int hour = 12;
    if (g_clockTimeValid) {
        time_t rawTime = time(nullptr);
        struct tm* ti = localtime(&rawTime);
        hour = ti->tm_hour;
    }
    bool isNight = (hour >= 21 || hour < 6);
    
    // Sky gradient based on time and weather
    for (int y = 2; y < 7; ++y) {
        uint8_t skyR, skyG, skyB;
        float yNorm = (y - 2) / 4.0f;  // 0 at bottom, 1 at top
        
        if (isNight) {
            skyR = 5 + (uint8_t)(15 * yNorm);
            skyG = 5 + (uint8_t)(10 * yNorm);
            skyB = 30 + (uint8_t)(30 * yNorm);
        } else if (current.type == WEATHER_SUNNY) {
            skyR = 60 + (uint8_t)(40 * yNorm);
            skyG = 120 + (uint8_t)(60 * yNorm);
            skyB = 200 + (uint8_t)(55 * yNorm);
        } else if (current.type >= WEATHER_RAIN && current.type <= WEATHER_HEAVY_RAIN) {
            skyR = 50 + (uint8_t)(20 * yNorm);
            skyG = 55 + (uint8_t)(25 * yNorm);
            skyB = 70 + (uint8_t)(40 * yNorm);
        } else {
            skyR = 70 + (uint8_t)(30 * yNorm);
            skyG = 90 + (uint8_t)(40 * yNorm);
            skyB = 130 + (uint8_t)(50 * yNorm);
        }
        
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            wt_display_set_pixel_xy(x, y, wt_color(skyR, skyG, skyB));
        }
    }
    
    // Ground
    uint32_t groundCol = (current.temp < 0) ? wt_color(200, 210, 220) : wt_color(50, 120, 50);
    uint32_t groundCol2 = (current.temp < 0) ? wt_color(180, 190, 200) : wt_color(40, 90, 40);
    for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
        wt_display_set_pixel_xy(x, 0, groundCol2);
        wt_display_set_pixel_xy(x, 1, groundCol);
    }
    
    // House
    uint32_t wallCol = wt_color(180, 130, 80);
    uint32_t roofCol = wt_color(140, 60, 40);
    uint32_t windowCol = isNight ? wt_color(255, 220, 100) : wt_color(150, 200, 255);
    
    // Walls (x=1-5)
    for (int wx = 1; wx <= 5; ++wx) {
        wt_display_set_pixel_xy(wx, 2, wallCol);
        wt_display_set_pixel_xy(wx, 3, wallCol);
    }
    // Roof
    wt_display_set_pixel_xy(0, 4, roofCol);
    for (int rx = 1; rx <= 5; ++rx) wt_display_set_pixel_xy(rx, 4, roofCol);
    wt_display_set_pixel_xy(6, 4, roofCol);
    wt_display_set_pixel_xy(2, 5, roofCol);
    wt_display_set_pixel_xy(3, 5, roofCol);
    wt_display_set_pixel_xy(4, 5, roofCol);
    // Window
    wt_display_set_pixel_xy(3, 3, windowCol);
    
    // Sun or Moon (top area)
    if (isNight) {
        // Moon crescent
        wt_display_set_pixel_xy(15, 5, wt_color(220, 220, 180));
        wt_display_set_pixel_xy(16, 6, wt_color(255, 255, 220));
        wt_display_set_pixel_xy(16, 5, wt_color(240, 240, 200));
        // Twinkling stars
        uint8_t tw = 60 + (uint8_t)(40 * sinf(now * 0.003f));
        wt_display_set_pixel_xy(10, 6, wt_color(tw, tw, tw));
        wt_display_set_pixel_xy(13, 5, wt_color(tw/2, tw/2, tw/2));
        wt_display_set_pixel_xy(19, 6, wt_color(tw, tw, tw));
    } else if (current.type == WEATHER_SUNNY || current.type == WEATHER_PARTLY_CLOUDY) {
        // Sun with rays
        float pulse = 0.9f + 0.1f * sinf(now * 0.002f);
        uint32_t sunCol = wt_color((uint8_t)(255 * pulse), (uint8_t)(220 * pulse), 50);
        wt_display_set_pixel_xy(16, 5, sunCol);
        wt_display_set_pixel_xy(17, 5, sunCol);
        wt_display_set_pixel_xy(16, 6, sunCol);
        wt_display_set_pixel_xy(17, 6, sunCol);
        // Rays
        uint32_t rayCol = wt_color((uint8_t)(200 * pulse), (uint8_t)(180 * pulse), 30);
        wt_display_set_pixel_xy(15, 6, rayCol);
        wt_display_set_pixel_xy(18, 6, rayCol);
    }
    
    // Weather effects
    if (current.type >= WEATHER_RAIN && current.type <= WEATHER_HEAVY_RAIN) {
        // Rain drops with trails
        for (int i = 0; i < 6; ++i) {
            int rx = 8 + (now / 60 + i * 3) % 12;
            int ry = 6 - (now / 50 + i * 7) % 5;
            if (ry >= 2) {
                wt_display_set_pixel_xy(rx, ry, wt_color(100, 150, 255));
                if (ry + 1 <= 6) wt_display_set_pixel_xy(rx, ry + 1, wt_color(60, 90, 150));
            }
        }
    } else if (current.type == WEATHER_SNOW) {
        // Snowflakes
        for (int i = 0; i < 5; ++i) {
            float drift = sinf(now * 0.002f + i) * 2;
            int sx = 8 + (int)drift + (i * 3) % 10;
            int sy = 6 - (now / 150 + i * 5) % 5;
            if (sy >= 2 && sx < WT_MATRIX_WIDTH) {
                wt_display_set_pixel_xy(sx, sy, wt_color(255, 255, 255));
            }
        }
    }
    
    // Temperature - bottom right, clean display
    int8_t temp = current.temp;
    bool neg = temp < 0;
    if (neg) temp = -temp;
    if (temp > 99) temp = 99;
    
    uint32_t tCol = tempToColor(current.temp);
    uint8_t x = 10;
    if (neg) { wt_display_set_pixel_xy(x, 1, tCol); x += 2; }
    if (temp >= 10) { drawDigit(x, 0, temp/10, tCol); x += 4; }
    drawDigit(x, 0, temp%10, tCol);
}

// Weather Preset 10: Retro LCD style  
static void weather_render_lcd() {
    WeatherData current = weather_get_current();
    // Classic LCD green-on-dark style
    uint32_t lcdBg = wt_color(15, 25, 15);     // Dark LCD background
    uint32_t lcdDigit = wt_color(80, 200, 80); // Bright green digits
    uint32_t lcdDim = wt_color(25, 45, 25);    // Dim segment outlines
    
    int8_t temp = current.temp;
    bool neg = temp < 0;
    if (neg) temp = -temp;
    if (temp > 99) temp = 99;
    
    // Fill background
    for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
        for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
            wt_display_set_pixel_xy(x, y, lcdBg);
        }
    }
    
    // LCD frame border
    for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
        wt_display_set_pixel_xy(x, 0, lcdDim);
        wt_display_set_pixel_xy(x, 6, lcdDim);
    }
    
    // Calculate centered position - digits start at y=0 (not y=1!)
    uint8_t numDigits = (temp >= 10) ? 2 : 1;
    uint8_t totalWidth = numDigits * 4 + (neg ? 3 : 0);
    uint8_t startX = (WT_MATRIX_WIDTH - totalWidth - 2) / 2;
    
    if (neg) {
        // Minus sign centered vertically
        wt_display_set_pixel_xy(startX, 3, lcdDigit);
        wt_display_set_pixel_xy(startX + 1, 3, lcdDigit);
        startX += 3;
    }
    
    // Tens digit - y=0 so digits aren't cut off
    if (temp >= 10) {
        drawDigit(startX, 0, temp / 10, lcdDigit);
        startX += 4;
    }
    // Ones digit
    drawDigit(startX, 0, temp % 10, lcdDigit);
    startX += 4;
    
    // Degree symbol (small square) - positioned to not go out of bounds
    wt_display_set_pixel_xy(startX, 5, lcdDigit);
    wt_display_set_pixel_xy(startX + 1, 5, lcdDigit);
    wt_display_set_pixel_xy(startX, 6, lcdDigit);
    wt_display_set_pixel_xy(startX + 1, 6, lcdDigit);
}

// Weather Preset 11: Gradient Mood - weather-representative animated background
static void weather_render_mood() {
    WeatherData current = weather_get_current();
    uint32_t now = millis();
    float phase = now * 0.001f;
    
    int8_t t = current.temp;
    float tempNorm = (t + 20.0f) / 50.0f;
    if (tempNorm < 0) tempNorm = 0;
    if (tempNorm > 1) tempNorm = 1;
    
    // Weather-dependent animation style
    for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
        for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
            float wave;
            uint8_t r, g, b;
            
            switch (current.type) {
                case WEATHER_SUNNY:
                case WEATHER_CLEAR_NIGHT: {
                    // Warm radial pulse from center
                    float dx = x - 10.0f, dy = y - 3.0f;
                    float dist = sqrtf(dx*dx + dy*dy);
                    wave = 0.5f + 0.5f * sinf(dist * 0.3f - phase * 2);
                    r = (uint8_t)(200 * wave + 50);
                    g = (uint8_t)(150 * wave * tempNorm);
                    b = (uint8_t)(50 * wave);
                    break;
                }
                case WEATHER_RAIN:
                case WEATHER_DRIZZLE:
                case WEATHER_HEAVY_RAIN: {
                    // Vertical rain streaks
                    wave = 0.3f + 0.7f * sinf(y * 1.5f + x * 0.2f - phase * 3);
                    r = (uint8_t)(30 * wave);
                    g = (uint8_t)(60 * wave + 20);
                    b = (uint8_t)(150 * wave + 50);
                    break;
                }
                case WEATHER_SNOW: {
                    // Gentle swirling white/blue
                    wave = 0.5f + 0.5f * sinf(x * 0.3f + y * 0.5f + phase);
                    r = (uint8_t)(180 * wave + 40);
                    g = (uint8_t)(180 * wave + 40);
                    b = (uint8_t)(200 * wave + 55);
                    break;
                }
                case WEATHER_STORM: {
                    // Dark purple with occasional flash
                    wave = 0.3f + 0.2f * sinf(x * 0.4f + phase);
                    bool flash = ((now / 100) % 30 == 0);
                    r = flash ? 200 : (uint8_t)(60 * wave + 20);
                    g = flash ? 200 : (uint8_t)(30 * wave);
                    b = flash ? 255 : (uint8_t)(100 * wave + 40);
                    break;
                }
                default: {
                    // Default temp-based gradient
                    wave = sinf(x * 0.25f + y * 0.4f + phase) * 0.5f + 0.5f;
                    r = (uint8_t)(tempNorm * 180 * wave + 30);
                    g = (uint8_t)(60 * wave);
                    b = (uint8_t)((1.0f - tempNorm) * 180 * wave + 30);
                }
            }
            wt_display_set_pixel_xy(x, y, wt_color(r, g, b));
        }
    }
    
    // Temperature overlay - centered with shadow for legibility
    int8_t temp = current.temp;
    bool neg = temp < 0;
    if (neg) temp = -temp;
    if (temp > 99) temp = 99;
    
    uint8_t numDigits = (temp >= 10) ? 2 : 1;
    uint8_t width = numDigits * 4 + (neg ? 3 : 0);
    uint8_t startX = (WT_MATRIX_WIDTH - width) / 2;
    
    // Shadow
    uint32_t shadow = wt_color(0, 0, 0);
    if (neg) { wt_display_set_pixel_xy(startX+1, 4, shadow); }
    if (temp >= 10) drawDigit(startX + (neg ? 4 : 1), 1, temp / 10, shadow);
    drawDigit(startX + (neg ? 8 : (temp >= 10 ? 5 : 1)), 1, temp % 10, shadow);
    
    // White text
    uint32_t white = wt_color(255, 255, 255);
    if (neg) {
        wt_display_set_pixel_xy(startX, 3, white);
        wt_display_set_pixel_xy(startX + 1, 3, white);
        startX += 3;
    }
    if (temp >= 10) { drawDigit(startX, 0, temp / 10, white); startX += 4; }
    drawDigit(startX, 0, temp % 10, white);
}

// Weather Preset 4: Big Digits
static void weather_render_minimal()
{
    WeatherData current = weather_get_current();
    uint32_t tCol = tempToColor(current.temp);
    
    int8_t temp = current.temp;
    bool negative = temp < 0;
    if (negative) temp = -temp;
    
    uint8_t tens = temp / 10;
    uint8_t ones = temp % 10;
    
    // Calculate width
    uint8_t width = (tens > 0 ? 11 : 5) + (negative ? 3 : 0);
    uint8_t x = (WT_MATRIX_WIDTH - width) / 2;
    
    if (negative) {
        wt_display_set_pixel_xy(x, 3, tCol);
        wt_display_set_pixel_xy(x+1, 3, tCol);
        x += 3;
    }
    if (tens > 0) {
        drawBigDigit(x, 0, tens, tCol);
        x += 6;
    }
    drawBigDigit(x, 0, ones, tCol);
    
    // Small dot for degree
    wt_display_set_pixel_xy(x, 6, tCol);
}
// Weather Preset 5: Day/Night Cycle - proper sky gradient
static void weather_render_daynight()
{
    wt_display_clear();
    wt_timeline_clear();
    
    uint32_t now = millis();
    float cycle = (now / 60000.0f); // Full cycle every minute
    float sunPos = fmodf(cycle, 1.0f); // 0 to 1
    
    // Calculate if it's day or night based on sun position
    bool isDay = (sunPos > 0.1f && sunPos < 0.6f);
    
    // Sky gradient based on time
    for(int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
        for(int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            uint8_t r, g, b;
            float heightFactor = (float)y / 6.0f; // 0 at bottom, 1 at top
            
            if (isDay) {
                // Day sky: light blue gradient, lighter at horizon
                r = 80 + (uint8_t)(50 * (1.0f - heightFactor));
                g = 150 + (uint8_t)(50 * (1.0f - heightFactor));
                b = 220 + (uint8_t)(35 * (1.0f - heightFactor));
            } else {
                // Night sky: dark blue/purple gradient
                r = 10 + (uint8_t)(15 * heightFactor);
                g = 10 + (uint8_t)(10 * heightFactor);
                b = 40 + (uint8_t)(30 * heightFactor);
            }
            wt_display_set_pixel_xy(x, y, wt_color(r, g, b));
        }
    }
    
    // Sun (during day half of cycle)
    if (isDay) {
        int sunX = (int)((sunPos - 0.1f) * 2.0f * (WT_MATRIX_WIDTH + 4)) - 2;
        int sunY = 5; // Near top
        uint32_t sunCol = wt_color(255, 220, 50);
        uint32_t glowCol = wt_color(255, 180, 30);
        // Sun core
        if (sunX >= 0 && sunX < WT_MATRIX_WIDTH) wt_display_set_pixel_xy(sunX, sunY, sunCol);
        if (sunX+1 >= 0 && sunX+1 < WT_MATRIX_WIDTH) wt_display_set_pixel_xy(sunX+1, sunY, sunCol);
        if (sunX >= 0 && sunX < WT_MATRIX_WIDTH && sunY > 0) wt_display_set_pixel_xy(sunX, sunY-1, sunCol);
        if (sunX+1 >= 0 && sunX+1 < WT_MATRIX_WIDTH && sunY > 0) wt_display_set_pixel_xy(sunX+1, sunY-1, sunCol);
        // Glow
        if (sunX-1 >= 0 && sunX-1 < WT_MATRIX_WIDTH) wt_display_set_pixel_xy(sunX-1, sunY, glowCol);
        if (sunX+2 >= 0 && sunX+2 < WT_MATRIX_WIDTH) wt_display_set_pixel_xy(sunX+2, sunY, glowCol);
    } else {
        // Moon and stars (during night)
        float moonPhase = (sunPos < 0.1f) ? (sunPos + 0.4f) : (sunPos - 0.6f);
        int moonX = (int)(moonPhase * 2.5f * WT_MATRIX_WIDTH);
        uint32_t moonCol = wt_color(220, 220, 200);
        if (moonX >= 0 && moonX < WT_MATRIX_WIDTH) wt_display_set_pixel_xy(moonX, 5, moonCol);
        if (moonX+1 >= 0 && moonX+1 < WT_MATRIX_WIDTH) wt_display_set_pixel_xy(moonX+1, 5, moonCol);
        if (moonX >= 0 && moonX < WT_MATRIX_WIDTH) wt_display_set_pixel_xy(moonX, 4, moonCol);
        
        // Twinkling stars (fixed positions, random twinkle)
        uint32_t starCol = wt_color(150, 150, 180);
        uint32_t starDim = wt_color(60, 60, 80);
        if ((now / 300) % 3 != 0) wt_display_set_pixel_xy(3, 5, starCol);
        if ((now / 400) % 3 != 0) wt_display_set_pixel_xy(8, 6, starDim);
        if ((now / 350) % 3 != 0) wt_display_set_pixel_xy(14, 4, starCol);
        if ((now / 450) % 3 != 0) wt_display_set_pixel_xy(18, 5, starDim);
    }
    
    // Ground line
    uint32_t groundCol = wt_color(30, 50, 30);
    for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
        wt_display_set_pixel_xy(x, 0, groundCol);
    }

    // Temp overlay (bottom right corner, compact)
    WeatherData current = weather_get_current();
    if (current.valid) {
        uint32_t tCol = wt_color(255, 255, 255);
        int8_t t = current.temp;
        bool neg = t < 0;
        if (neg) t = -t;
        if (t > 99) t = 99;
        
        uint8_t x = WT_MATRIX_WIDTH - 8;
        if (neg) {
            wt_display_set_pixel_xy(x, 3, tCol);
            wt_display_set_pixel_xy(x+1, 3, tCol);
            x += 3;
        }
        if (t >= 10) {
            drawDigit(x, 0, t / 10, tCol);
            x += 4;
        }
        drawDigit(x, 0, t % 10, tCol);
    }
}



// Weather Preset 12: Typewriter - digits appear one by one with CRT effect
static void weather_render_typewriter() {
    static uint8_t charIndex = 0;
    static uint32_t lastType = 0;
    static int8_t lastTemp = 0;
    WeatherData current = weather_get_current();
    uint32_t now = millis();
    
    int8_t t = current.temp;
    bool neg = t < 0;
    if (neg) t = -t;
    if (t > 99) t = 99;
    
    // Reset animation if temp changed
    if (current.temp != lastTemp) {
        charIndex = 0;
        lastTemp = current.temp;
    }
    
    // Calculate total elements: minus (if neg) + tens (if >= 10) + ones + degree
    uint8_t totalItems = 1 + (neg ? 1 : 0) + (t >= 10 ? 1 : 0) + 1; // ones + optional minus + optional tens + degree
    
    // Type one element every 350ms
    if (now - lastType > 350) {
        lastType = now;
        charIndex++;
        if (charIndex > totalItems + 2) charIndex = 0; // Reset with pause
    }
    
    // Amber monochrome CRT look with scanline effect
    uint32_t amber = wt_color(255, 180, 0);
    uint32_t dimAmber = wt_color(40, 30, 0);
    
    // Subtle CRT scanlines
    for (int y = 0; y < WT_MATRIX_HEIGHT; y += 2) {
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            wt_display_set_pixel_xy(x, y, dimAmber);
        }
    }
    
    // Draw typed elements
    uint8_t x = 3;
    uint8_t itemIdx = 0;
    
    // Minus sign
    if (neg && itemIdx < charIndex) {
        wt_display_set_pixel_xy(x, 3, amber);
        wt_display_set_pixel_xy(x+1, 3, amber);
        wt_display_set_pixel_xy(x+2, 3, amber);
        itemIdx++;
        x += 4;
    } else if (neg) {
        itemIdx++;
        x += 4;
    }
    
    // Tens digit
    if (t >= 10) {
        if (itemIdx < charIndex) {
            drawDigit(x, 0, t / 10, amber);
        }
        itemIdx++;
        x += 4;
    }
    
    // Ones digit
    if (itemIdx < charIndex) {
        drawDigit(x, 0, t % 10, amber);
    }
    itemIdx++;
    x += 4;
    
    // Degree symbol
    if (itemIdx < charIndex) {
        wt_display_set_pixel_xy(x, 5, amber);
        wt_display_set_pixel_xy(x+1, 5, amber);
        wt_display_set_pixel_xy(x, 6, amber);
        wt_display_set_pixel_xy(x+1, 6, amber);
    }
    
    // Blinking block cursor at current position
    if ((now / 400) % 2 && charIndex <= totalItems) {
        uint8_t cursorX = 3;
        if (neg) cursorX += 4;
        if (t >= 10 && charIndex > (neg ? 1 : 0)) cursorX += 4;
        if (charIndex > (neg ? 1 : 0) + (t >= 10 ? 1 : 0)) cursorX += 4;
        
        for (int cy = 0; cy < 7; ++cy) {
            wt_display_set_pixel_xy(cursorX, cy, amber);
        }
    }
}

// Weather Preset 13: Waves - weather-linked animated water scene
static void weather_render_waves() {
    WeatherData current = weather_get_current();
    uint32_t now = millis();
    float phase = now * 0.002f;
    
    // Weather-dependent water color and intensity
    uint8_t waterR = 0, waterG = 40, waterB = 80;
    float waveIntensity = 1.0f;
    bool showRain = false;
    
    switch (current.type) {
        case WEATHER_SUNNY:
            waterR = 20; waterG = 80; waterB = 140;  // Bright blue
            waveIntensity = 0.8f;
            break;
        case WEATHER_STORM:
        case WEATHER_HEAVY_RAIN:
            waterR = 10; waterG = 30; waterB = 60;   // Dark stormy
            waveIntensity = 2.0f;
            showRain = true;
            break;
        case WEATHER_RAIN:
        case WEATHER_DRIZZLE:
            waterR = 15; waterG = 50; waterB = 90;
            waveIntensity = 1.3f;
            showRain = true;
            break;
        case WEATHER_CLOUDY:
            waterR = 20; waterG = 45; waterB = 70;   // Muted grey-blue
            break;
        default:
            break;
    }
    
    // Gradient background - sky to water
    for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
        uint8_t skyFade = (y > 3) ? (y - 3) * 20 : 0;
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            wt_display_set_pixel_xy(x, y, wt_color(waterR + skyFade/4, waterG + skyFade/2, waterB + skyFade));
        }
    }
    
    // Animated wave layers
    for (int layer = 0; layer < 3; ++layer) {
        uint8_t blue = waterB + 30 + layer * 25;
        uint8_t green = waterG + 20 + layer * 15;
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            float wave = sinf(x * 0.35f + phase * (1.0f + layer * 0.3f) * waveIntensity + layer * 1.5f);
            int y = layer + (int)(wave * waveIntensity);
            if (y >= 0 && y < 4) {
                wt_display_set_pixel_xy(x, y, wt_color(waterR, green, blue));
                // Foam on wave peaks
                if (wave > 0.6f && y + 1 < 4) {
                    wt_display_set_pixel_xy(x, y + 1, wt_color(180, 200, 220));
                }
            }
        }
    }
    
    // Rain overlay if needed
    if (showRain) {
        for (int i = 0; i < 4; ++i) {
            int rx = (now / 50 + i * 5) % WT_MATRIX_WIDTH;
            int ry = 6 - (now / 40 + i * 3) % 4;
            if (ry >= 3) wt_display_set_pixel_xy(rx, ry, wt_color(100, 150, 220));
        }
    }
    
    // Temperature display - y=0 so not cut off
    int8_t t = current.temp;
    bool neg = t < 0;
    if (neg) t = -t;
    if (t > 99) t = 99;
    
    uint32_t white = wt_color(255, 255, 255);
    uint8_t numDigits = (t >= 10) ? 2 : 1;
    uint8_t width = numDigits * 4 + (neg ? 3 : 0);
    uint8_t x = (WT_MATRIX_WIDTH - width) / 2;
    
    if (neg) { wt_display_set_pixel_xy(x, 3, white); wt_display_set_pixel_xy(x+1, 3, white); x += 3; }
    if (t >= 10) { drawDigit(x, 0, t/10, white); x += 4; }
    drawDigit(x, 0, t%10, white);
}

// Weather Preset 14: Split - weather-dependent contrasting halves
static void weather_render_split() {
    WeatherData current = weather_get_current();
    uint32_t now = millis();
    
    int8_t t = current.temp;
    bool neg = t < 0;
    if (neg) t = -t;
    if (t > 99) t = 99;
    
    // Weather-dependent color scheme
    uint8_t darkR = 20, darkG = 20, darkB = 40;
    uint8_t lightR = 200, lightG = 180, lightB = 100;
    
    switch (current.type) {
        case WEATHER_SUNNY:
            darkR = 40; darkG = 30; darkB = 10;
            lightR = 255; lightG = 220; lightB = 100;
            break;
        case WEATHER_RAIN:
        case WEATHER_DRIZZLE:
        case WEATHER_HEAVY_RAIN:
            darkR = 20; darkG = 30; darkB = 50;
            lightR = 100; lightG = 140; lightB = 200;
            break;
        case WEATHER_STORM:
            darkR = 30; darkG = 20; darkB = 50;
            lightR = 120; lightG = 80; lightB = 180;
            break;
        case WEATHER_SNOW:
            darkR = 40; darkG = 50; darkB = 60;
            lightR = 220; lightG = 230; lightB = 255;
            break;
        case WEATHER_CLOUDY:
            darkR = 40; darkG = 45; darkB = 50;
            lightR = 150; lightG = 160; lightB = 170;
            break;
        default:
            break;
    }
    
    // Animated diagonal split
    float animOffset = sinf(now * 0.001f) * 0.1f;
    
    for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
        for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
            float diag = (float)x / WT_MATRIX_WIDTH - (float)y / WT_MATRIX_HEIGHT + animOffset;
            if (diag < 0.2f) {
                float blend = (diag + 0.2f) / 0.4f;
                if (blend < 0) blend = 0;
                if (blend > 1) blend = 1;
                uint8_t r = darkR + (uint8_t)((lightR - darkR) * blend * 0.3f);
                uint8_t g = darkG + (uint8_t)((lightG - darkG) * blend * 0.3f);
                uint8_t b = darkB + (uint8_t)((lightB - darkB) * blend * 0.3f);
                wt_display_set_pixel_xy(x, y, wt_color(r, g, b));
            } else {
                float blend = (diag - 0.2f) / 0.8f;
                if (blend > 1) blend = 1;
                uint8_t r = lightR - (uint8_t)(30 * blend);
                uint8_t g = lightG - (uint8_t)(20 * blend);
                uint8_t b = lightB + (uint8_t)(20 * blend);
                wt_display_set_pixel_xy(x, y, wt_color(r, g, b));
            }
        }
    }
    
    // Centered temperature with shadow
    uint32_t tCol = wt_color(255, 255, 255);
    uint32_t shadow = wt_color(0, 0, 0);
    
    uint8_t numDigits = (t >= 10) ? 2 : 1;
    uint8_t width = numDigits * 4 + (neg ? 3 : 0);
    uint8_t x = (WT_MATRIX_WIDTH - width) / 2;
    
    // Shadow offset
    if (neg) { wt_display_set_pixel_xy(x+1, 4, shadow); }
    if (t >= 10) drawDigit(x + (neg ? 4 : 1), 1, t/10, shadow);
    drawDigit(x + (neg ? 8 : (t >= 10 ? 5 : 1)), 1, t%10, shadow);
    
    // Main digits
    if (neg) { wt_display_set_pixel_xy(x, 3, tCol); wt_display_set_pixel_xy(x+1, 3, tCol); x += 3; }
    if (t >= 10) { drawDigit(x, 0, t/10, tCol); x += 4; }
    drawDigit(x, 0, t%10, tCol);
}

// Weather Preset 15: Countdown - temp displayed as dramatic countdown ticker
static void weather_render_countdown() {
    static int displayTemp = 0;
    static uint32_t lastTick = 0;
    static bool counting = true;
    WeatherData current = weather_get_current();
    uint32_t now = millis();
    int8_t targetTemp = current.temp;
    
    // Animate counting up/down to target
    if (now - lastTick > 80) {
        lastTick = now;
        if (displayTemp < targetTemp) displayTemp++;
        else if (displayTemp > targetTemp) displayTemp--;
        else counting = false;
    }
    
    // Reset animation periodically
    if (!counting && (now % 10000) < 100) {
        displayTemp = targetTemp + (random(2) ? 20 : -20);
        counting = true;
    }
    
    int8_t t = displayTemp;
    bool neg = t < 0;
    if (neg) t = -t;
    if (t > 99) t = 99;
    
    // Red LED style background
    uint32_t bgRed = counting ? wt_color(40, 0, 0) : wt_color(0, 20, 0);
    for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
        for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
            wt_display_set_pixel_xy(x, y, bgRed);
        }
    }
    
    // Large LED digits
    uint32_t ledCol = counting ? wt_color(255, 50, 50) : wt_color(50, 255, 50);
    uint8_t x = 4;
    if (neg) { 
        wt_display_set_pixel_xy(x, 3, ledCol); 
        wt_display_set_pixel_xy(x+1, 3, ledCol); 
        wt_display_set_pixel_xy(x+2, 3, ledCol); 
        x += 4; 
    }
    drawDigit(x, 0, t / 10, ledCol); x += 5;
    drawDigit(x, 0, t % 10, ledCol);
    
    // Flashing border when counting
    if (counting && (now / 100) % 2) {
        for (int i = 0; i < WT_MATRIX_WIDTH; ++i) {
            wt_display_set_pixel_xy(i, 0, wt_color(255, 0, 0));
            wt_display_set_pixel_xy(i, 6, wt_color(255, 0, 0));
        }
    }
}

// Weather Preset 16: Thermometer - horizontal bar thermometer
static void weather_render_stack() {
    WeatherData current = weather_get_current();
    
    int8_t t = current.temp;
    bool neg = t < 0;
    int8_t absT = neg ? -t : t;
    if (absT > 99) absT = 99;
    
    // Dark background
    for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            wt_display_set_pixel_xy(x, y, wt_color(10, 12, 15));
        }
    }
    
    // Horizontal thermometer tube (y=1-2, x=0-17)
    uint32_t glassCol = wt_color(50, 60, 70);
    uint32_t mercuryCol = tempToColor(t);
    uint32_t emptyCol = wt_color(25, 30, 35);
    
    // Glass outline
    for (int x = 0; x < 18; ++x) {
        wt_display_set_pixel_xy(x, 0, glassCol);
        wt_display_set_pixel_xy(x, 3, glassCol);
    }
    wt_display_set_pixel_xy(18, 1, glassCol);
    wt_display_set_pixel_xy(18, 2, glassCol);
    
    // Mercury level - map temp to 0-16 pixels
    // -20 to +40 range -> 0 to 16
    int mercuryLen = (t + 20) * 16 / 60;
    if (mercuryLen < 1) mercuryLen = 1;
    if (mercuryLen > 16) mercuryLen = 16;
    
    // Fill tube
    for (int x = 1; x < 17; ++x) {
        if (x <= mercuryLen) {
            wt_display_set_pixel_xy(x, 1, mercuryCol);
            wt_display_set_pixel_xy(x, 2, mercuryCol);
        } else {
            wt_display_set_pixel_xy(x, 1, emptyCol);
            wt_display_set_pixel_xy(x, 2, emptyCol);
        }
    }
    
    // Bulb on left
    wt_display_set_pixel_xy(0, 1, mercuryCol);
    wt_display_set_pixel_xy(0, 2, mercuryCol);
    
    // Scale marks
    wt_display_set_pixel_xy(5, 0, wt_color(80, 80, 90));   // ~0°
    wt_display_set_pixel_xy(11, 0, wt_color(80, 80, 90));  // ~20°
    
    // Temperature digits (top area, centered)
    uint32_t textCol = wt_color(255, 255, 255);
    uint8_t numDigits = (absT >= 10) ? 2 : 1;
    uint8_t width = numDigits * 4 + (neg ? 3 : 0);
    uint8_t x = (WT_MATRIX_WIDTH - width) / 2;
    
    if (neg) {
        wt_display_set_pixel_xy(x, 5, textCol);
        wt_display_set_pixel_xy(x+1, 5, textCol);
        x += 3;
    }
    if (absT >= 10) {
        drawDigit(x, 4, absT / 10, textCol);
        x += 4;
    }
    drawDigit(x, 4, absT % 10, textCol);
}

// Weather Preset 17: Weather Icon Large - animated weather symbol with temp
static void weather_render_emoji() {
    WeatherData current = weather_get_current();
    uint32_t now = millis();
    
    // Weather-appropriate background
    uint8_t bgR = 20, bgG = 25, bgB = 35;
    switch (current.type) {
        case WEATHER_SUNNY: bgR = 40; bgG = 35; bgB = 20; break;
        case WEATHER_PARTLY_CLOUDY: bgR = 35; bgG = 38; bgB = 45; break;
        case WEATHER_RAIN:
        case WEATHER_HEAVY_RAIN:
        case WEATHER_DRIZZLE: bgR = 15; bgG = 25; bgB = 45; break;
        case WEATHER_SNOW:
        case WEATHER_SLEET: bgR = 30; bgG = 35; bgB = 40; break;
        case WEATHER_STORM: bgR = 25; bgG = 15; bgB = 35; break;
        case WEATHER_FOG: bgR = 35; bgG = 38; bgB = 40; break;
        case WEATHER_WIND: bgR = 25; bgG = 35; bgB = 45; break;
        case WEATHER_CLEAR_NIGHT: bgR = 10; bgG = 15; bgB = 30; break;
        default: break;
    }
    
    for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            wt_display_set_pixel_xy(x, y, wt_color(bgR, bgG, bgB));
        }
    }
    
    // Draw large weather symbol (left side, 8x7)
    float pulse = 0.85f + 0.15f * sinf(now * 0.003f);
    
    switch (current.type) {
        case WEATHER_SUNNY: {
            // Big sun with animated rays
            uint32_t sunCol = wt_color((uint8_t)(255*pulse), (uint8_t)(200*pulse), 30);
            uint32_t rayCol = wt_color((uint8_t)(200*pulse), (uint8_t)(150*pulse), 20);
            // Core
            for (int sy = 2; sy <= 4; ++sy)
                for (int sx = 2; sx <= 4; ++sx)
                    wt_display_set_pixel_xy(sx, sy, sunCol);
            // Rays
            wt_display_set_pixel_xy(3, 6, rayCol); wt_display_set_pixel_xy(3, 0, rayCol);
            wt_display_set_pixel_xy(0, 3, rayCol); wt_display_set_pixel_xy(6, 3, rayCol);
            wt_display_set_pixel_xy(1, 5, rayCol); wt_display_set_pixel_xy(5, 5, rayCol);
            wt_display_set_pixel_xy(1, 1, rayCol); wt_display_set_pixel_xy(5, 1, rayCol);
            break;
        }
        case WEATHER_CLEAR_NIGHT: {
            // Moon with stars
            uint32_t moonCol = wt_color((uint8_t)(220*pulse), (uint8_t)(220*pulse), (uint8_t)(180*pulse));
            uint32_t starCol = wt_color(200, 200, 255);
            // Crescent moon
            wt_display_set_pixel_xy(2, 1, moonCol); wt_display_set_pixel_xy(3, 1, moonCol);
            wt_display_set_pixel_xy(1, 2, moonCol); wt_display_set_pixel_xy(4, 2, moonCol);
            wt_display_set_pixel_xy(1, 3, moonCol); wt_display_set_pixel_xy(1, 4, moonCol);
            wt_display_set_pixel_xy(2, 5, moonCol); wt_display_set_pixel_xy(3, 5, moonCol);
            // Twinkling stars
            if ((now / 300) % 2 == 0) wt_display_set_pixel_xy(6, 1, starCol);
            if ((now / 400) % 2 == 0) wt_display_set_pixel_xy(5, 4, starCol);
            if ((now / 350) % 2 == 0) wt_display_set_pixel_xy(7, 3, starCol);
            break;
        }
        case WEATHER_PARTLY_CLOUDY: {
            // Sun behind cloud
            uint32_t sunCol = wt_color((uint8_t)(255*pulse), (uint8_t)(180*pulse), 30);
            uint32_t cloudCol = wt_color(160, 165, 175);
            // Sun peeking out
            wt_display_set_pixel_xy(5, 0, sunCol); wt_display_set_pixel_xy(6, 0, sunCol);
            wt_display_set_pixel_xy(6, 1, sunCol); wt_display_set_pixel_xy(7, 1, sunCol);
            wt_display_set_pixel_xy(7, 2, sunCol);
            // Cloud in front
            for (int cx = 0; cx <= 5; ++cx) wt_display_set_pixel_xy(cx, 3, cloudCol);
            for (int cx = 0; cx <= 6; ++cx) wt_display_set_pixel_xy(cx, 4, cloudCol);
            wt_display_set_pixel_xy(1, 5, cloudCol); wt_display_set_pixel_xy(4, 5, cloudCol);
            break;
        }
        case WEATHER_RAIN:
        case WEATHER_DRIZZLE:
        case WEATHER_HEAVY_RAIN: {
            // Cloud with animated rain falling DOWN
            uint32_t cloudCol = wt_color(120, 130, 150);
            for (int cx = 1; cx <= 6; ++cx) wt_display_set_pixel_xy(cx, 5, cloudCol);
            for (int cx = 0; cx <= 7; ++cx) wt_display_set_pixel_xy(cx, 6, cloudCol);
            // Animated rain drops falling down
            uint32_t rainCol = wt_color(100, 150, 255);
            int numDrops = (current.type == WEATHER_HEAVY_RAIN) ? 4 : (current.type == WEATHER_DRIZZLE) ? 2 : 3;
            for (int i = 0; i < numDrops; ++i) {
                int ry = (now / 80 + i * 5) % 5;  // 0-4, increases = falls down
                int rx = 1 + i * 2;
                if (ry < 5) wt_display_set_pixel_xy(rx, ry, rainCol);
            }
            break;
        }
        case WEATHER_SNOW: {
            // Cloud with drifting snowflakes
            uint32_t cloudCol = wt_color(150, 160, 170);
            for (int cx = 1; cx <= 6; ++cx) wt_display_set_pixel_xy(cx, 5, cloudCol);
            for (int cx = 0; cx <= 7; ++cx) wt_display_set_pixel_xy(cx, 6, cloudCol);
            // Animated snowflakes drifting down
            for (int i = 0; i < 4; ++i) {
                float drift = sinf(now * 0.002f + i) * 1.5f;
                int sy = (now / 150 + i * 7) % 5;  // Falls down
                int sx = 1 + i * 2 + (int)drift;
                if (sy < 5 && sx >= 0 && sx < 8)
                    wt_display_set_pixel_xy(sx, sy, wt_color(255, 255, 255));
            }
            break;
        }
        case WEATHER_SLEET: {
            // Cloud with mixed rain/snow
            uint32_t cloudCol = wt_color(140, 150, 165);
            for (int cx = 1; cx <= 6; ++cx) wt_display_set_pixel_xy(cx, 5, cloudCol);
            for (int cx = 0; cx <= 7; ++cx) wt_display_set_pixel_xy(cx, 6, cloudCol);
            // Mixed precipitation
            for (int i = 0; i < 3; ++i) {
                int ry = (now / 90 + i * 6) % 5;
                if (i % 2 == 0)
                    wt_display_set_pixel_xy(1 + i * 2, ry, wt_color(100, 150, 255)); // Rain
                else
                    wt_display_set_pixel_xy(1 + i * 2, ry, wt_color(255, 255, 255)); // Snow
            }
            break;
        }
        case WEATHER_STORM: {
            // Dark cloud with lightning
            uint32_t cloudCol = wt_color(80, 85, 100);
            for (int cx = 1; cx <= 6; ++cx) wt_display_set_pixel_xy(cx, 5, cloudCol);
            for (int cx = 0; cx <= 7; ++cx) wt_display_set_pixel_xy(cx, 6, cloudCol);
            // Animated lightning bolt
            if ((now / 200) % 8 < 2) {
                uint32_t boltCol = wt_color(255, 255, 100);
                wt_display_set_pixel_xy(3, 4, boltCol);
                wt_display_set_pixel_xy(4, 3, boltCol);
                wt_display_set_pixel_xy(3, 2, boltCol);
                wt_display_set_pixel_xy(4, 1, boltCol);
                wt_display_set_pixel_xy(5, 0, boltCol);
            }
            break;
        }
        case WEATHER_FOG: {
            // Horizontal fog lines with animation
            uint32_t fogBright = wt_color(150, 155, 160);
            uint32_t fogDim = wt_color(100, 105, 110);
            int offset = (now / 200) % 3;
            for (int y = 1; y < 6; y += 2) {
                for (int x = 0; x < 8; ++x) {
                    int xShift = (x + offset) % 8;
                    wt_display_set_pixel_xy(xShift, y, (x % 2 == 0) ? fogBright : fogDim);
                }
            }
            break;
        }
        case WEATHER_WIND: {
            // Animated wind streaks
            uint32_t windCol = wt_color(150, 180, 200);
            uint32_t windDim = wt_color(80, 100, 120);
            int offset = (now / 50) % 8;
            // Multiple wind lines
            for (int x = 0; x < 7; ++x) {
                int xPos = (x + offset) % 8;
                wt_display_set_pixel_xy(xPos, 2, windCol);
            }
            for (int x = 0; x < 5; ++x) {
                int xPos = (x + offset + 2) % 8;
                wt_display_set_pixel_xy(xPos, 4, windDim);
            }
            break;
        }
        case WEATHER_CLOUDY:
        default: {
            // Big fluffy cloud
            uint32_t cloudCol = wt_color(150, 155, 165);
            uint32_t cloudDark = wt_color(110, 115, 125);
            for (int cx = 1; cx <= 6; ++cx) { 
                wt_display_set_pixel_xy(cx, 3, cloudCol); 
                wt_display_set_pixel_xy(cx, 4, cloudCol); 
            }
            for (int cx = 0; cx <= 7; ++cx) wt_display_set_pixel_xy(cx, 5, cloudCol);
            wt_display_set_pixel_xy(2, 2, cloudCol); wt_display_set_pixel_xy(5, 2, cloudCol);
            wt_display_set_pixel_xy(3, 6, cloudDark); wt_display_set_pixel_xy(4, 6, cloudDark);
            break;
        }
    }
    
    // Temperature on right
    int8_t t = current.temp;
    bool neg = t < 0;
    if (neg) t = -t;
    if (t > 99) t = 99;
    
    uint32_t tCol = tempToColor(current.temp);
    uint8_t x = 10;
    if (neg) { wt_display_set_pixel_xy(x, 3, tCol); wt_display_set_pixel_xy(x+1, 3, tCol); x += 3; }
    if (t >= 10) { drawDigit(x, 0, t/10, tCol); x += 4; }
    drawDigit(x, 0, t%10, tCol);
}

// Weather Preset 18: Matrix Rain - falling rain with weather intensity
static void weather_render_matrix() {
    static uint8_t dropY[10];  // Y positions for drops (0=top, 6=bottom, falling DOWN)
    static uint8_t dropSpeed[10];
    static uint32_t lastDrop = 0;
    static bool init = false;
    WeatherData current = weather_get_current();
    uint32_t now = millis();
    
    // Initialize drops at random positions
    if (!init) {
        for (int i = 0; i < 10; ++i) {
            dropY[i] = random(7);
            dropSpeed[i] = 1 + random(2);
        }
        init = true;
    }
    
    // Weather affects rain intensity and color
    uint8_t intensity = 3;  // Default drops
    uint32_t bright, mid, dim;
    
    if (current.type >= WEATHER_RAIN && current.type <= WEATHER_HEAVY_RAIN) {
        intensity = (current.type == WEATHER_HEAVY_RAIN) ? 8 : 5;
        bright = wt_color(100, 180, 255);
        mid = wt_color(50, 100, 180);
        dim = wt_color(20, 50, 100);
    } else if (current.type == WEATHER_SNOW) {
        intensity = 4;
        bright = wt_color(255, 255, 255);
        mid = wt_color(180, 180, 200);
        dim = wt_color(100, 100, 120);
    } else {
        bright = wt_color(0, 255, 100);
        mid = wt_color(0, 150, 60);
        dim = wt_color(0, 60, 25);
    }
    
    // Update drops - falling DOWN (Y increases from 0 to 6)
    if (now - lastDrop > 60) {
        lastDrop = now;
        for (int i = 0; i < 10; ++i) {
            if (i < intensity) {
                dropY[i] = (dropY[i] + 1) % 10;  // Increment with wrap (falls down)
            }
        }
    }
    
    // Clear to dark
    for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            wt_display_set_pixel_xy(x, y, wt_color(0, 8, 4));
        }
    }
    
    // Draw falling drops - Y=0 is TOP, Y=6 is BOTTOM
    for (int col = 0; col < WT_MATRIX_WIDTH; col += 2) {
        int dropIdx = col / 2;
        if (dropIdx >= intensity) continue;
        
        int headY = dropY[dropIdx] % 7;  // Head position (0=top, 6=bottom)
        
        // Draw drop with trail ABOVE it (smaller Y values = higher on screen)
        if (headY >= 0 && headY < 7) wt_display_set_pixel_xy(col, headY, bright);
        if (headY - 1 >= 0) wt_display_set_pixel_xy(col, headY - 1, mid);
        if (headY - 2 >= 0) wt_display_set_pixel_xy(col, headY - 2, dim);
    }
    
    // Temperature - bottom center
    int8_t t = current.temp;
    bool neg = t < 0;
    if (neg) t = -t;
    if (t > 99) t = 99;
    
    uint8_t numDigits = (t >= 10) ? 2 : 1;
    uint8_t width = numDigits * 4 + (neg ? 3 : 0);
    uint8_t x = (WT_MATRIX_WIDTH - width) / 2;
    
    if (neg) { wt_display_set_pixel_xy(x, 3, bright); wt_display_set_pixel_xy(x+1, 3, bright); x += 3; }
    if (t >= 10) { drawDigit(x, 0, t/10, bright); x += 4; }
    drawDigit(x, 0, t%10, bright);
}

// Weather Preset 19: Cyber - neon glow with pulsing temperature display
static void weather_render_neon() {
    WeatherData current = weather_get_current();
    uint32_t now = millis();
    
    int8_t t = current.temp;
    bool neg = t < 0;
    int8_t absT = neg ? -t : t;
    if (absT > 99) absT = 99;
    
    // Dark background with subtle gradient
    for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            uint8_t base = 3 + y;
            wt_display_set_pixel_xy(x, y, wt_color(base, base + 2, base + 5));
        }
    }
    
    // Animated scan line moving across
    int scanX = (now / 40) % (WT_MATRIX_WIDTH + 4) - 2;
    for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
        if (scanX >= 0 && scanX < WT_MATRIX_WIDTH)
            wt_display_set_pixel_xy(scanX, y, wt_color(20, 40, 60));
        if (scanX + 1 >= 0 && scanX + 1 < WT_MATRIX_WIDTH)
            wt_display_set_pixel_xy(scanX + 1, y, wt_color(40, 80, 120));
        if (scanX + 2 >= 0 && scanX + 2 < WT_MATRIX_WIDTH)
            wt_display_set_pixel_xy(scanX + 2, y, wt_color(20, 40, 60));
    }
    
    // Neon color based on temperature with smooth pulse
    float pulse = 0.7f + 0.3f * sinf(now * 0.005f);
    uint8_t r, g, b;
    if (current.temp < 0) { r = 80; g = 180; b = 255; }       // Freezing - ice blue
    else if (current.temp < 10) { r = 0; g = 220; b = 255; }  // Cold - cyan
    else if (current.temp < 20) { r = 0; g = 255; b = 120; }  // Mild - green
    else if (current.temp < 30) { r = 255; g = 200; b = 0; }  // Warm - yellow
    else { r = 255; g = 50; b = 80; }                          // Hot - pink/red
    
    r = (uint8_t)(r * pulse);
    g = (uint8_t)(g * pulse);
    b = (uint8_t)(b * pulse);
    uint32_t neonCol = wt_color(r, g, b);
    uint32_t glowCol = wt_color(r/4, g/4, b/4);
    uint32_t dimGlow = wt_color(r/8, g/8, b/8);
    
    // Calculate digit positioning
    uint8_t numDigits = (absT >= 10) ? 2 : 1;
    uint8_t width = numDigits * 4 + (neg ? 3 : 0);
    uint8_t startX = (WT_MATRIX_WIDTH - width) / 2;
    
    // Draw outer glow halo
    for (int gx = startX - 2; gx <= startX + width + 1; ++gx) {
        if (gx >= 0 && gx < WT_MATRIX_WIDTH) {
            wt_display_set_pixel_xy(gx, 0, dimGlow);
            wt_display_set_pixel_xy(gx, 6, dimGlow);
        }
    }
    for (int gy = 1; gy < 6; ++gy) {
        if (startX - 2 >= 0) wt_display_set_pixel_xy(startX - 2, gy, dimGlow);
        if (startX + width + 1 < WT_MATRIX_WIDTH) wt_display_set_pixel_xy(startX + width + 1, gy, dimGlow);
    }
    
    // Draw inner glow border
    for (int gx = startX - 1; gx <= startX + width; ++gx) {
        if (gx >= 0 && gx < WT_MATRIX_WIDTH) {
            wt_display_set_pixel_xy(gx, 0, glowCol);
            wt_display_set_pixel_xy(gx, 6, glowCol);
        }
    }
    
    // Draw digits with neon glow
    uint8_t x = startX;
    if (neg) {
        wt_display_set_pixel_xy(x, 3, neonCol);
        wt_display_set_pixel_xy(x+1, 3, neonCol);
        x += 3;
    }
    if (absT >= 10) { drawDigit(x, 0, absT/10, neonCol); x += 4; }
    drawDigit(x, 0, absT%10, neonCol);
}

// Weather Preset 20: Particles - weather-driven particle system
static void weather_render_bubbles() {
    static float partY[8] = {0, 2, 4, 1, 3, 5, 2, 4};
    static float partX[8] = {2, 6, 10, 14, 4, 12, 8, 16};
    static uint32_t lastPart = 0;
    WeatherData current = weather_get_current();
    uint32_t now = millis();
    
    // Weather-dependent background and particle style
    uint8_t bgR = 20, bgG = 25, bgB = 35;
    uint32_t partCol, partTrail;
    float speed = 0.2f;
    bool goingUp = false;  // Most particles fall down
    
    switch (current.type) {
        case WEATHER_SUNNY:
            bgR = 50; bgG = 40; bgB = 20;
            partCol = wt_color(255, 200, 50);  // Golden dust
            partTrail = wt_color(150, 120, 30);
            goingUp = true;  // Rising heat
            speed = 0.15f;
            break;
        case WEATHER_RAIN:
        case WEATHER_HEAVY_RAIN:
            bgR = 15; bgG = 25; bgB = 45;
            partCol = wt_color(100, 160, 255);  // Rain
            partTrail = wt_color(50, 90, 150);
            speed = 0.35f;
            break;
        case WEATHER_SNOW:
            bgR = 25; bgG = 30; bgB = 40;
            partCol = wt_color(255, 255, 255);  // Snow
            partTrail = wt_color(150, 160, 180);
            speed = 0.1f;
            break;
        default:
            partCol = wt_color(150, 180, 200);
            partTrail = wt_color(80, 100, 120);
    }
    
    // Gradient background
    for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
        uint8_t fade = y * 3;
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            wt_display_set_pixel_xy(x, y, wt_color(bgR + fade/2, bgG + fade/2, bgB + fade));
        }
    }
    
    // Update particles
    if (now - lastPart > 40) {
        lastPart = now;
        for (int i = 0; i < 8; ++i) {
            float drift = sinf(now * 0.002f + i * 1.5f) * 0.15f;
            partX[i] += drift;
            if (goingUp) {
                partY[i] += speed + (i % 3) * 0.05f;
                if (partY[i] > 8) { partY[i] = -1; partX[i] = random(WT_MATRIX_WIDTH); }
            } else {
                partY[i] -= speed + (i % 3) * 0.05f;
                if (partY[i] < -1) { partY[i] = 8; partX[i] = random(WT_MATRIX_WIDTH); }
            }
        }
    }
    
    // Draw particles with trails
    for (int i = 0; i < 8; ++i) {
        int px = (int)partX[i];
        int py = (int)partY[i];
        if (py >= 0 && py < 7 && px >= 0 && px < WT_MATRIX_WIDTH) {
            wt_display_set_pixel_xy(px, py, partCol);
            int trailY = goingUp ? py - 1 : py + 1;
            if (trailY >= 0 && trailY < 7) wt_display_set_pixel_xy(px, trailY, partTrail);
        }
    }
    
    // Temperature
    int8_t t = current.temp;
    bool neg = t < 0;
    if (neg) t = -t;
    if (t > 99) t = 99;
    
    uint32_t tCol = wt_color(255, 255, 255);
    uint8_t numDigits = (t >= 10) ? 2 : 1;
    uint8_t width = numDigits * 4 + (neg ? 3 : 0);
    uint8_t x = (WT_MATRIX_WIDTH - width) / 2;
    
    if (neg) { wt_display_set_pixel_xy(x, 3, tCol); wt_display_set_pixel_xy(x+1, 3, tCol); x += 3; }
    if (t >= 10) { drawDigit(x, 0, t/10, tCol); x += 4; }
    drawDigit(x, 0, t%10, tCol);
}

// Weather Preset 21: Waveform - weather-reactive audio-style visualization  
static void weather_render_pulse() {
    WeatherData current = weather_get_current();
    uint32_t now = millis();
    
    // Weather-dependent wave pattern and color
    uint32_t lineCol, bgCol;
    float freq = 0.3f, amp = 2.0f;
    
    switch (current.type) {
        case WEATHER_SUNNY:
            lineCol = wt_color(255, 200, 50);
            bgCol = wt_color(30, 25, 10);
            freq = 0.25f; amp = 1.5f;  // Calm waves
            break;
        case WEATHER_STORM:
            lineCol = wt_color(200, 100, 255);
            bgCol = wt_color(20, 10, 30);
            freq = 0.5f; amp = 3.0f;  // Chaotic
            break;
        case WEATHER_RAIN:
        case WEATHER_HEAVY_RAIN:
            lineCol = wt_color(100, 180, 255);
            bgCol = wt_color(10, 20, 35);
            freq = 0.4f; amp = 2.5f;
            break;
        case WEATHER_WIND:
            lineCol = wt_color(150, 200, 180);
            bgCol = wt_color(15, 25, 20);
            freq = 0.6f; amp = 2.0f;
            break;
        default:
            lineCol = wt_color(0, 255, 150);
            bgCol = wt_color(5, 15, 10);
    }
    
    // Dark background
    for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            wt_display_set_pixel_xy(x, y, bgCol);
        }
    }
    
    // Draw animated waveform
    float phase = now * 0.003f;
    for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
        float wave = sinf(x * freq + phase) * amp;
        int y = 3 + (int)wave;
        if (y < 0) y = 0;
        if (y > 6) y = 6;
        
        wt_display_set_pixel_xy(x, y, lineCol);
        
        // Glow above/below
        uint8_t r = (lineCol >> 16) & 0xFF;
        uint8_t g = (lineCol >> 8) & 0xFF;
        uint8_t b = lineCol & 0xFF;
        uint32_t glowCol = wt_color(r/3, g/3, b/3);
        if (y > 0) wt_display_set_pixel_xy(x, y - 1, glowCol);
        if (y < 6) wt_display_set_pixel_xy(x, y + 1, glowCol);
    }
    
    // Weather icon indicator (top-right)
    uint32_t iconCol = wt_color(100, 100, 100);
    if (current.type == WEATHER_SUNNY) {
        wt_display_set_pixel_xy(18, 5, wt_color(255, 200, 50));
        wt_display_set_pixel_xy(19, 6, wt_color(200, 150, 30));
    } else if (current.type >= WEATHER_RAIN) {
        wt_display_set_pixel_xy(18, 6, wt_color(100, 150, 255));
        wt_display_set_pixel_xy(19, 5, wt_color(80, 120, 200));
    }
    
    // Temperature on left
    int8_t t = current.temp;
    bool neg = t < 0;
    if (neg) t = -t;
    if (t > 99) t = 99;
    
    uint32_t tCol = wt_color(255, 255, 255);
    uint8_t x = 1;
    if (neg) { wt_display_set_pixel_xy(x, 3, tCol); wt_display_set_pixel_xy(x+1, 3, tCol); x += 3; }
    if (t >= 10) { drawDigit(x, 0, t/10, tCol); x += 4; }
    drawDigit(x, 0, t%10, tCol);
}

// Weather Preset 22: TempBar - clean horizontal thermometer with gradient fill
static void weather_render_dot() {
    WeatherData current = weather_get_current();
    uint32_t now = millis();
    
    int8_t t = current.temp;
    bool neg = t < 0;
    int8_t absT = neg ? -t : t;
    if (absT > 99) absT = 99;
    
    // Clean dark background
    for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            wt_display_set_pixel_xy(x, y, wt_color(5, 5, 8));
        }
    }
    
    // Thermometer outline (rows 2-4, centered)
    uint32_t outlineCol = wt_color(40, 45, 55);
    for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
        wt_display_set_pixel_xy(x, 1, outlineCol);
        wt_display_set_pixel_xy(x, 5, outlineCol);
    }
    
    // Temperature bar fill (rows 2-4) - map -30 to +45 -> 0 to 18 pixels
    int barLen = (current.temp + 30) * 18 / 75;
    if (barLen < 1) barLen = 1;
    if (barLen > 18) barLen = 18;
    
    // Animated pulse
    float pulse = 0.9f + 0.1f * sinf(now * 0.003f);
    
    for (int x = 0; x < barLen; ++x) {
        // Gradient from cold to hot across the bar
        float ratio = (float)x / 18.0f;
        uint8_t r, g, b;
        if (ratio < 0.33f) {
            // Blue to cyan
            r = 30; g = (uint8_t)(100 + 100 * ratio * 3); b = 255;
        } else if (ratio < 0.66f) {
            // Cyan to yellow
            float t2 = (ratio - 0.33f) * 3;
            r = (uint8_t)(30 + 225 * t2); g = (uint8_t)(200 + 55 * t2); b = (uint8_t)(255 * (1 - t2));
        } else {
            // Yellow to red
            float t2 = (ratio - 0.66f) * 3;
            r = 255; g = (uint8_t)(255 * (1 - t2 * 0.7f)); b = 0;
        }
        
        r = (uint8_t)(r * pulse); g = (uint8_t)(g * pulse); b = (uint8_t)(b * pulse);
        uint32_t col = wt_color(r, g, b);
        uint32_t dimCol = wt_color(r/2, g/2, b/2);
        
        wt_display_set_pixel_xy(x, 2, dimCol);
        wt_display_set_pixel_xy(x, 3, col);
        wt_display_set_pixel_xy(x, 4, dimCol);
    }
    
    // End cap glow
    if (barLen > 0 && barLen < 18) {
        uint32_t capCol = tempToColor(current.temp);
        uint8_t cr = (capCol >> 16) & 0xFF;
        uint8_t cg = (capCol >> 8) & 0xFF;
        uint8_t cb = capCol & 0xFF;
        wt_display_set_pixel_xy(barLen, 3, wt_color(cr/3, cg/3, cb/3));
    }
    
    // Temperature digits (bottom right, small)
    uint32_t tCol = wt_color(255, 255, 255);
    char buf[5];
    snprintf(buf, sizeof(buf), "%d", current.temp);
    int len = strlen(buf);
    int x = WT_MATRIX_WIDTH - len * 4;
    for (int i = 0; i < len; ++i) {
        if (buf[i] == '-') {
            wt_display_set_pixel_xy(x, 6, tCol);
            wt_display_set_pixel_xy(x+1, 6, tCol);
            x += 3;
        } else {
            drawDigit(x, 0, buf[i] - '0', tCol);
            x += 4;
        }
    }
}

// Weather Preset 23: Aurora - flowing northern lights effect
static void weather_render_aurora() {
    WeatherData current = weather_get_current();
    uint32_t now = millis();
    
    // Dark sky background
    for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            wt_display_set_pixel_xy(x, y, wt_color(5, 8, 15));
        }
    }
    
    // Aurora waves - multiple layers with different speeds
    for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
        // Layer 1: Green aurora
        float wave1 = sinf(x * 0.4f + now * 0.002f) * 2.0f;
        int y1 = 3 + (int)wave1;
        if (y1 >= 0 && y1 < 7) {
            float bright = 0.6f + 0.4f * sinf(x * 0.3f + now * 0.003f);
            wt_display_set_pixel_xy(x, y1, wt_color(0, (uint8_t)(180*bright), (uint8_t)(80*bright)));
            if (y1+1 < 7) wt_display_set_pixel_xy(x, y1+1, wt_color(0, (uint8_t)(100*bright), (uint8_t)(50*bright)));
        }
        
        // Layer 2: Purple/pink aurora
        float wave2 = sinf(x * 0.3f - now * 0.0015f + 2.0f) * 1.5f;
        int y2 = 4 + (int)wave2;
        if (y2 >= 0 && y2 < 7) {
            float bright = 0.5f + 0.5f * sinf(x * 0.25f + now * 0.002f);
            wt_display_set_pixel_xy(x, y2, wt_color((uint8_t)(100*bright), 0, (uint8_t)(150*bright)));
        }
    }
    
    // Stars twinkling
    if ((now / 200) % 5 == 0) wt_display_set_pixel_xy(2, 0, wt_color(200, 200, 255));
    if ((now / 300) % 4 == 0) wt_display_set_pixel_xy(10, 1, wt_color(180, 180, 220));
    if ((now / 250) % 6 == 0) wt_display_set_pixel_xy(15, 0, wt_color(220, 220, 255));
    
    // Temperature display - bottom right
    int8_t t = current.temp;
    bool neg = t < 0;
    if (neg) t = -t;
    if (t > 99) t = 99;
    
    uint32_t tCol = wt_color(150, 255, 200);  // Aurora green
    uint8_t x = WT_MATRIX_WIDTH - ((t >= 10) ? 8 : 4) - (neg ? 3 : 0);
    if (neg) { wt_display_set_pixel_xy(x, 3, tCol); wt_display_set_pixel_xy(x+1, 3, tCol); x += 3; }
    if (t >= 10) { drawDigit(x, 0, t/10, tCol); x += 4; }
    drawDigit(x, 0, t%10, tCol);
}

// Weather Preset 24: Radar - weather radar sweep animation
static void weather_render_radar() {
    WeatherData current = weather_get_current();
    uint32_t now = millis();
    
    // Dark background with radar grid
    for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            uint8_t grid = ((x % 6 == 0) || (y % 3 == 0)) ? 15 : 5;
            wt_display_set_pixel_xy(x, y, wt_color(0, grid, 0));
        }
    }
    
    // Radar sweep line
    float angle = now * 0.003f;
    int cx = 9, cy = 3;  // Center
    for (int r = 1; r < 8; ++r) {
        int sx = cx + (int)(cosf(angle) * r);
        int sy = cy + (int)(sinf(angle) * r * 0.5f);
        if (sx >= 0 && sx < WT_MATRIX_WIDTH && sy >= 0 && sy < 7) {
            wt_display_set_pixel_xy(sx, sy, wt_color(0, 255, 50));
        }
    }
    
    // Weather blips based on condition
    uint32_t blipCol;
    int numBlips = 0;
    switch (current.type) {
        case WEATHER_SUNNY: blipCol = wt_color(255, 200, 0); numBlips = 1; break;
        case WEATHER_RAIN:
        case WEATHER_DRIZZLE: blipCol = wt_color(0, 150, 255); numBlips = 3; break;
        case WEATHER_HEAVY_RAIN: blipCol = wt_color(0, 100, 255); numBlips = 5; break;
        case WEATHER_STORM: blipCol = wt_color(255, 50, 50); numBlips = 4; break;
        case WEATHER_SNOW: blipCol = wt_color(200, 200, 255); numBlips = 3; break;
        default: blipCol = wt_color(100, 100, 100); numBlips = 2; break;
    }
    
    // Draw blips with fade effect
    for (int i = 0; i < numBlips; ++i) {
        int bx = 3 + (i * 7 + (now/500)) % 12;
        int by = 1 + (i * 3) % 5;
        float fade = 0.5f + 0.5f * sinf(now * 0.005f + i);
        uint8_t r = ((blipCol >> 16) & 0xFF) * fade;
        uint8_t g = ((blipCol >> 8) & 0xFF) * fade;
        uint8_t b = (blipCol & 0xFF) * fade;
        wt_display_set_pixel_xy(bx, by, wt_color(r, g, b));
    }
    
    // Temperature - top left in radar green
    int8_t t = current.temp;
    bool neg = t < 0;
    if (neg) t = -t;
    uint32_t tCol = wt_color(0, 200, 80);
    uint8_t x = 0;
    if (neg) { wt_display_set_pixel_xy(x, 3, tCol); wt_display_set_pixel_xy(x+1, 3, tCol); x += 3; }
    if (t >= 10) { drawDigit(x, 0, t/10, tCol); x += 4; }
    drawDigit(x, 0, t%10, tCol);
}

// Weather Preset 25: Glitch - digital glitch effect
static void weather_render_glitch() {
    WeatherData current = weather_get_current();
    uint32_t now = millis();
    static uint32_t lastGlitch = 0;
    static int glitchY = -1;
    static int glitchOffset = 0;
    
    // Trigger random glitch every ~2 seconds
    if (now - lastGlitch > 2000 + random(1000)) {
        lastGlitch = now;
        glitchY = random(7);
        glitchOffset = random(5) - 2;
    }
    
    // Clear glitch after 100ms
    if (now - lastGlitch > 100) {
        glitchY = -1;
    }
    
    // Dark background
    for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            wt_display_set_pixel_xy(x, y, wt_color(8, 8, 12));
        }
    }
    
    // Glitch scanlines
    if (glitchY >= 0) {
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            int gx = (x + glitchOffset + WT_MATRIX_WIDTH) % WT_MATRIX_WIDTH;
            wt_display_set_pixel_xy(gx, glitchY, wt_color(255, 0, 100));
        }
    }
    
    // Random noise pixels during glitch
    if (glitchY >= 0) {
        for (int i = 0; i < 5; ++i) {
            int nx = random(WT_MATRIX_WIDTH);
            int ny = random(7);
            wt_display_set_pixel_xy(nx, ny, wt_color(random(100), random(255), random(200)));
        }
    }
    
    // Temperature with RGB split effect
    int8_t t = current.temp;
    bool neg = t < 0;
    if (neg) t = -t;
    if (t > 99) t = 99;
    
    uint8_t numDigits = (t >= 10) ? 2 : 1;
    uint8_t width = numDigits * 4 + (neg ? 3 : 0);
    uint8_t startX = (WT_MATRIX_WIDTH - width) / 2;
    
    // Draw with color separation effect
    uint8_t x = startX;
    uint32_t mainCol = wt_color(255, 255, 255);
    
    // RGB ghost offset during glitch
    int rgbOff = (glitchY >= 0) ? 1 : 0;
    
    if (neg) { 
        if (rgbOff) wt_display_set_pixel_xy(x-1, 3, wt_color(255, 0, 0));
        wt_display_set_pixel_xy(x, 3, mainCol); 
        wt_display_set_pixel_xy(x+1, 3, mainCol);
        if (rgbOff) wt_display_set_pixel_xy(x+2, 3, wt_color(0, 255, 255));
        x += 3; 
    }
    if (t >= 10) { drawDigit(x, 0, t/10, mainCol); x += 4; }
    drawDigit(x, 0, t%10, mainCol);
}

// Weather Preset 26: Horizon - sunrise/sunset gradient scene
static void weather_render_horizon() {
    WeatherData current = weather_get_current();
    uint32_t now = millis();
    
    // Animated sun position (rises and sets)
    float sunPhase = sinf(now * 0.0005f);  // -1 to 1
    int sunY = 3 - (int)(sunPhase * 2.5f);  // 0-6
    
    // Sky gradient based on sun position
    for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            uint8_t r, g, b;
            float yRatio = (float)y / 6.0f;
            
            if (sunPhase > 0.3f) {
                // Day sky - blue gradient
                r = (uint8_t)(100 + 80 * yRatio);
                g = (uint8_t)(180 + 50 * yRatio);
                b = 255;
            } else if (sunPhase > -0.3f) {
                // Sunrise/sunset - orange to purple
                float blend = (sunPhase + 0.3f) / 0.6f;
                r = (uint8_t)(255 - 80 * yRatio);
                g = (uint8_t)(100 + 80 * blend - 60 * yRatio);
                b = (uint8_t)(100 + 100 * yRatio);
            } else {
                // Night sky - dark blue
                r = (uint8_t)(10 + 5 * yRatio);
                g = (uint8_t)(15 + 10 * yRatio);
                b = (uint8_t)(40 + 20 * yRatio);
            }
            wt_display_set_pixel_xy(x, y, wt_color(r, g, b));
        }
    }
    
    // Draw sun/moon
    if (sunPhase > -0.5f) {
        // Sun
        uint32_t sunCol = wt_color(255, 220, 100);
        if (sunY >= 0 && sunY < 7) wt_display_set_pixel_xy(3, sunY, sunCol);
        if (sunY-1 >= 0) wt_display_set_pixel_xy(3, sunY-1, sunCol);
        if (sunY+1 < 7) wt_display_set_pixel_xy(3, sunY+1, sunCol);
        wt_display_set_pixel_xy(2, sunY, sunCol);
        wt_display_set_pixel_xy(4, sunY, sunCol);
    } else {
        // Moon
        uint32_t moonCol = wt_color(200, 200, 180);
        wt_display_set_pixel_xy(3, 1, moonCol);
        wt_display_set_pixel_xy(4, 1, moonCol);
        wt_display_set_pixel_xy(2, 2, moonCol);
    }
    
    // Ground line
    for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
        wt_display_set_pixel_xy(x, 6, wt_color(30, 50, 30));
    }
    
    // Temperature - right side
    int8_t t = current.temp;
    bool neg = t < 0;
    if (neg) t = -t;
    if (t > 99) t = 99;
    
    uint32_t tCol = (sunPhase > 0) ? wt_color(50, 50, 80) : wt_color(200, 200, 255);
    uint8_t x = WT_MATRIX_WIDTH - ((t >= 10) ? 8 : 4) - (neg ? 3 : 0);
    if (neg) { wt_display_set_pixel_xy(x, 3, tCol); wt_display_set_pixel_xy(x+1, 3, tCol); x += 3; }
    if (t >= 10) { drawDigit(x, 0, t/10, tCol); x += 4; }
    drawDigit(x, 0, t%10, tCol);
}

// Weather Preset 27: Frost - ice crystal formation effect
static void weather_render_frost() {
    WeatherData current = weather_get_current();
    uint32_t now = millis();
    
    // Temperature affects frost intensity
    float frostLevel = 1.0f;
    if (current.temp > 5) frostLevel = 0.3f;
    else if (current.temp > 0) frostLevel = 0.6f;
    else if (current.temp < -10) frostLevel = 1.0f;
    
    // Dark blue background
    for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            wt_display_set_pixel_xy(x, y, wt_color(5, 10, 20));
        }
    }
    
    // Frost crystal patterns growing from edges
    float phase = now * 0.001f;
    
    // Crystal branches
    for (int i = 0; i < 6; ++i) {
        float angle = i * 1.047f + phase * 0.2f;  // 60 degree spacing
        int cx = 9, cy = 3;
        
        for (int r = 1; r < (int)(5 * frostLevel); ++r) {
            int fx = cx + (int)(cosf(angle) * r);
            int fy = cy + (int)(sinf(angle) * r * 0.5f);
            
            if (fx >= 0 && fx < WT_MATRIX_WIDTH && fy >= 0 && fy < 7) {
                float bright = 0.6f + 0.4f * sinf(phase + r * 0.5f);
                uint32_t frostCol = wt_color(
                    (uint8_t)(150 * bright * frostLevel),
                    (uint8_t)(200 * bright * frostLevel),
                    (uint8_t)(255 * bright * frostLevel)
                );
                wt_display_set_pixel_xy(fx, fy, frostCol);
            }
        }
    }
    
    // Edge frost on corners
    uint32_t edgeCol = wt_color((uint8_t)(100*frostLevel), (uint8_t)(150*frostLevel), (uint8_t)(200*frostLevel));
    wt_display_set_pixel_xy(0, 0, edgeCol);
    wt_display_set_pixel_xy(1, 0, edgeCol);
    wt_display_set_pixel_xy(0, 1, edgeCol);
    wt_display_set_pixel_xy(WT_MATRIX_WIDTH-1, 0, edgeCol);
    wt_display_set_pixel_xy(WT_MATRIX_WIDTH-2, 0, edgeCol);
    wt_display_set_pixel_xy(WT_MATRIX_WIDTH-1, 1, edgeCol);
    wt_display_set_pixel_xy(0, 6, edgeCol);
    wt_display_set_pixel_xy(WT_MATRIX_WIDTH-1, 6, edgeCol);
    
    // Temperature in ice blue
    int8_t t = current.temp;
    bool neg = t < 0;
    if (neg) t = -t;
    if (t > 99) t = 99;
    
    uint32_t tCol = wt_color(180, 220, 255);
    uint8_t numDigits = (t >= 10) ? 2 : 1;
    uint8_t width = numDigits * 4 + (neg ? 3 : 0);
    uint8_t x = (WT_MATRIX_WIDTH - width) / 2;
    
    if (neg) { wt_display_set_pixel_xy(x, 3, tCol); wt_display_set_pixel_xy(x+1, 3, tCol); x += 3; }
    if (t >= 10) { drawDigit(x, 0, t/10, tCol); x += 4; }
    drawDigit(x, 0, t%10, tCol);
}

static void weather_render()
{
    WeatherData current = weather_get_current();
    wt_display_clear();
    wt_timeline_clear();
    
    if (!current.valid)
    {
        uint8_t b = 80 + (millis() / 8) % 80;
        for (uint8_t i = 0; i < 3; ++i)
            wt_display_set_pixel_xy(9 + i, 3, wt_color(b, b, b));
        return;
    }

    // 23 weather display presets
    switch (g_weatherPreset) {
        case 0: weather_render_classic(); break;       // Icon + temp
        case 1: weather_render_fs_bar(); break;        // Horizontal gradient bar
        case 2: weather_render_fs_corner(); break;     // Mini icon + large temp
        case 3: weather_render_fs_animated(); break;   // Full animated bg
        case 4: weather_render_minimal(); break;       // Big temp only
        case 5: weather_render_daynight(); break;      // Day/Night Cycle
        case 6: weather_render_terminal(); break;      // Terminal
        case 7: weather_render_bigtype(); break;       // Big Type
        case 8: weather_render_forecast_strip(); break;// Forecast Strip
        case 9: weather_render_pixel_art(); break;     // Pixel Art Scene
        case 10: weather_render_lcd(); break;          // Retro LCD
        case 11: weather_render_mood(); break;         // Gradient Mood
        case 12: weather_render_typewriter(); break;   // Typewriter
        case 13: weather_render_waves(); break;        // Ocean Waves
        case 14: weather_render_split(); break;        // Diagonal Split
        case 15: weather_render_countdown(); break;    // Countdown
        case 16: weather_render_stack(); break;        // Thermometer
        case 17: weather_render_emoji(); break;        // Emoji Face
        case 18: weather_render_matrix(); break;       // Matrix Rain
        case 19: weather_render_neon(); break;         // Neon Sign
        case 20: weather_render_bubbles(); break;      // Bubbles
        case 21: weather_render_pulse(); break;        // Heartbeat/Pulse
        case 22: weather_render_dot(); break;          // TempBar
        case 23: weather_render_aurora(); break;       // Aurora
        case 24: weather_render_radar(); break;        // Radar
        case 25: weather_render_glitch(); break;       // Glitch
        case 26: weather_render_horizon(); break;      // Horizon
        case 27: weather_render_frost(); break;        // Frost
        default: weather_render_classic(); break;
    }

    // Timeline - forecast colors with current hour pulse
    // Index 0 (leftmost) = current weather, index 11 (rightmost) = future
    // forecastHours determines how many hours each LED represents
    uint32_t now = millis();
    Settings& cfg = settings_get();
    uint8_t hoursPerLed = cfg.forecastHours / WT_TIMELINE_PIXELS;
    if (hoursPerLed < 1) hoursPerLed = 1;
    
    for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i)
    {
        // Map LED index to forecast index based on forecastHours setting
        uint8_t forecastIdx = i * hoursPerLed;
        if (forecastIdx >= 12) forecastIdx = 11;  // Clamp to available forecast data
        
        uint32_t baseCol = weatherColor(weather_get_forecast(forecastIdx).type);
        
        // Leftmost LED (i=0) = current weather - subtle pulse
        if (i == 0) {
            uint8_t pulse = 180 + (uint8_t)(75 * sinf(now * 0.004f));
            uint8_t r = min(255, (int)(((baseCol >> 16) & 0xFF) * pulse / 150));
            uint8_t g = min(255, (int)(((baseCol >> 8) & 0xFF) * pulse / 150));
            uint8_t b = min(255, (int)((baseCol & 0xFF) * pulse / 150));
            wt_timeline_set_pixel(i, wt_color(r, g, b));
        } else {
            // Fade slightly towards future
            uint8_t fade = 255 - i * 8;
            uint8_t r = ((baseCol >> 16) & 0xFF) * fade / 255;
            uint8_t g = ((baseCol >> 8) & 0xFF) * fade / 255;
            uint8_t b = (baseCol & 0xFF) * fade / 255;
            wt_timeline_set_pixel(i, wt_color(r, g, b));
        }
    }
}

static void test_setup()
{
}

static void test_update(uint32_t now, uint32_t dt)
{
}

static void test_render()
{
    wt_display_clear();
    wt_timeline_clear();

    uint32_t red = wt_color(255, 0, 0);
    uint32_t green = wt_color(0, 255, 0);
    uint32_t blue = wt_color(0, 0, 255);

    for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x)
    {
        uint32_t col;
        if (x < 6)
        {
            col = red;
        }
        else if (x < 13)
        {
            col = green;
        }
        else
        {
            col = blue;
        }

        for (uint8_t y = 0; y < WT_MATRIX_HEIGHT; ++y)
        {
            wt_display_set_pixel_xy(x, y, col);
        }
    }

    for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i)
    {
        if (i < 4)
        {
            wt_timeline_set_pixel(i, red);
        }
        else if (i < 8)
        {
            wt_timeline_set_pixel(i, green);
        }
        else
        {
            wt_timeline_set_pixel(i, blue);
        }
    }
}

// ============== UNIFIED AUDIO BACKEND ==============
// Shared audio state for all visualizers
static uint8_t g_audioHeight = 0;      // 0-7 normalized level
static uint16_t g_audioAmplitude = 0;  // Raw amplitude
static uint8_t g_audioHistory[20];     // History for waveform/spectrum
static uint8_t g_audioHistoryIdx = 0;
static uint8_t g_audioBeat = 0;        // Beat detection pulse

static void audio_sample()
{
    // Sample audio and compute metrics
    int16_t minS = 2048, maxS = 2048;
    
    for (uint8_t i = 0; i < VU_SAMPLES; ++i)
    {
        int16_t raw = (int16_t)wt_mic_read_raw();
        if (raw < minS) minS = raw;
        if (raw > maxS) maxS = raw;
    }
    
    uint16_t amplitude = maxS - minS;
    g_audioAmplitude = amplitude;
    
    // Noise gate
    if (amplitude < g_vuNoiseFloor)
    {
        g_vuSilenceFrames++;
        if (g_vuSilenceFrames > 5) amplitude = 0;
    }
    else
    {
        g_vuSilenceFrames = 0;
    }
    
    // AGC
    if (amplitude > g_vuPeakLevel) g_vuPeakLevel = amplitude;
    else g_vuPeakLevel = (g_vuPeakLevel * 63) / 64;
    
    // Normalize to 0-7
    uint16_t range = (g_vuPeakLevel < 300) ? 300 : g_vuPeakLevel;
    uint8_t h = 0;
    if (amplitude > g_vuNoiseFloor)
        h = (amplitude - g_vuNoiseFloor) * 8 / (range - g_vuNoiseFloor);
    if (h > 7) h = 7;

    // Invert if requested
    if (settings_get().vuInvert) {
        h = 7 - h;
    }

    g_audioHeight = h;
    
    // Store in history (circular buffer)
    g_audioHistory[g_audioHistoryIdx] = h;
    g_audioHistoryIdx = (g_audioHistoryIdx + 1) % 20;
    
    // Simple beat detection
    if (h >= 5 && g_audioBeat == 0) g_audioBeat = 12; // Longer beat pulse
    if (g_audioBeat > 0) g_audioBeat--;
}

static void vu_setup()
{
    memset(g_vuSamples, 0, sizeof(g_vuSamples));
    memset(g_vuBars, 0, sizeof(g_vuBars));
    memset(g_vuPeaks, 0, sizeof(g_vuPeaks));
    memset(g_vuPeakHold, 0, sizeof(g_vuPeakHold));
    memset(g_audioHistory, 0, sizeof(g_audioHistory));
    g_vuPeakLevel = 0;
    g_vuSilenceFrames = 0;
    g_audioHistoryIdx = 0;
}

static void vu_update(uint32_t now, uint32_t dt)
{
    // Use unified audio backend
    audio_sample();
    
    uint8_t h = g_audioHeight;
    
    // Update bar heights with center-out wave shape
    for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x)
    {
        uint8_t dist = (x < 10) ? (9 - x) : (x - 10);
        int8_t target = h - (dist / 2);
        if (target < 0) target = 0;
        
        if (target > g_vuBars[x]) g_vuBars[x] = target;
        else if (g_vuBars[x] > 0) g_vuBars[x]--;
        
        if (g_vuBars[x] >= g_vuPeaks[x]) {
            g_vuPeaks[x] = g_vuBars[x];
            g_vuPeakHold[x] = 10;
        }
        else if (g_vuPeakHold[x] > 0) g_vuPeakHold[x]--;
        else if (g_vuPeaks[x] > 0 && (now % 3 == 0)) g_vuPeaks[x]--;
    }
}

// ============== VU VISUALIZER PRESETS ==============

// Preset 0: Spectrum bars using current palette
static void vu_render_spectrum()
{
    uint8_t palette = settings_get().vuPalette;
    
    for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x)
    {
        uint8_t h = g_vuBars[x];
        for (uint8_t y = 0; y < h; ++y)
        {
            // Map height to palette position (0=bottom, 7=top -> 0-255 palette)
            uint8_t palPos = y * 36;  // 0-252 range for 7 levels
            uint32_t col = settings_palette_color(palette, palPos, 255);
            wt_display_set_pixel_xy(x, y, col);
        }
        if (g_vuPeaks[x] > 0)
            wt_display_set_pixel_xy(x, g_vuPeaks[x], wt_color(255, 255, 255));
    }
}

// Preset 1: Waveform oscilloscope
static void vu_render_waveform()
{
    uint32_t col = wt_color(0, 255, 100);
    uint32_t dimCol = wt_color(0, 80, 30);
    
    for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x)
    {
        uint8_t idx = (g_audioHistoryIdx + x) % 20;
        uint8_t h = g_audioHistory[idx];
        
        // Draw vertical line for this sample
        uint8_t mid = 3;
        int8_t offset = (h > 3) ? (h - 3) : -(3 - h) / 2;
        uint8_t y = mid + offset;
        if (y > 6) y = 6;
        
        wt_display_set_pixel_xy(x, y, col);
        
        // Dim glow below
        if (y > 0) wt_display_set_pixel_xy(x, y - 1, dimCol);
    }
}

// Preset 2: Fire effect
static void vu_render_fire()
{
    static uint8_t heat[WT_MATRIX_WIDTH][WT_MATRIX_HEIGHT];
    
    // Cool down
    for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x)
        for (uint8_t y = 0; y < WT_MATRIX_HEIGHT; ++y)
            if (heat[x][y] > 0) heat[x][y] -= (heat[x][y] > 20) ? 15 : heat[x][y];
    
    // Add heat at bottom based on audio
    for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x)
    {
        uint8_t h = g_vuBars[x];
        uint8_t newHeat = h * 35;
        if (newHeat > heat[x][0]) heat[x][0] = newHeat;
    }
    
    // Rise and diffuse
    for (uint8_t y = WT_MATRIX_HEIGHT - 1; y > 0; --y)
    {
        for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x)
        {
            uint8_t left = (x > 0) ? heat[x-1][y-1] : heat[x][y-1];
            uint8_t center = heat[x][y-1];
            uint8_t right = (x < WT_MATRIX_WIDTH-1) ? heat[x+1][y-1] : heat[x][y-1];
            heat[x][y] = (left + center + center + right) / 4;
        }
    }
    
    // Render
    for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x)
    {
        for (uint8_t y = 0; y < WT_MATRIX_HEIGHT; ++y)
        {
            uint8_t h = heat[x][y];
            if (h > 0)
            {
                uint8_t r = (h > 128) ? 255 : h * 2;
                uint8_t g = (h > 64) ? (h - 64) * 2 : 0;
                uint8_t b = 0;
                wt_display_set_pixel_xy(x, y, wt_color(r, g, b));
            }
        }
    }
}

// Preset 3: Pulse rings from center
static void vu_render_pulse()
{
    static float phase = 0;
    phase += 0.15f + g_audioHeight * 0.05f;
    
    float cx = 9.5f, cy = 3.0f;
    
    for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x)
    {
        for (uint8_t y = 0; y < WT_MATRIX_HEIGHT; ++y)
        {
            float dx = x - cx;
            float dy = y - cy;
            float dist = sqrtf(dx * dx + dy * dy);
            
            float wave = sinf(dist * 0.8f - phase);
            float intensity = (wave + 1.0f) * 0.5f;
            intensity *= (float)g_audioHeight / 7.0f;
            
            if (intensity > 0.1f)
            {
                uint8_t hue = (uint8_t)((dist * 20 + phase * 10));
                uint32_t col = colorWheel(hue);
                uint8_t r = ((col >> 16) & 0xFF) * intensity;
                uint8_t g = ((col >> 8) & 0xFF) * intensity;
                uint8_t b = (col & 0xFF) * intensity;
                wt_display_set_pixel_xy(x, y, wt_color(r, g, b));
            }
        }
    }
}

// Preset 4: Rainbow waterfall
static void vu_render_waterfall()
{
    static uint8_t waterfall[WT_MATRIX_WIDTH][WT_MATRIX_HEIGHT];
    
    // Shift down
    for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x)
    {
        for (uint8_t y = 0; y < WT_MATRIX_HEIGHT - 1; ++y)
            waterfall[x][y] = waterfall[x][y + 1];
        waterfall[x][WT_MATRIX_HEIGHT - 1] = g_vuBars[x] * 30;
    }
    
    // Render
    uint32_t t = millis() / 100;
    for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x)
    {
        for (uint8_t y = 0; y < WT_MATRIX_HEIGHT; ++y)
        {
            uint8_t v = waterfall[x][y];
            if (v > 10)
            {
                uint8_t hue = (x * 12 + y * 20 + t) & 0xFF;
                uint32_t col = colorWheel(hue);
                float bright = (float)v / 255.0f;
                uint8_t r = ((col >> 16) & 0xFF) * bright;
                uint8_t g = ((col >> 8) & 0xFF) * bright;
                uint8_t b = (col & 0xFF) * bright;
                    wt_display_set_pixel_xy(x, y, wt_color(r, g, b));
            }
        }
    }
}

// Preset 5: Strobe flash on beat
static void vu_render_strobe()
{
    if (g_audioBeat > 5) {
        // Full white flash on strong beat
        uint32_t col = wt_color(255, 255, 255);
        for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x)
            for (uint8_t y = 0; y < WT_MATRIX_HEIGHT; ++y)
                wt_display_set_pixel_xy(x, y, col);
    } else if (g_audioBeat > 0) {
        // Dim flash
        uint8_t b = g_audioBeat * 30;
        uint32_t col = wt_color(b, b, b);
        for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x)
            for (uint8_t y = 0; y < WT_MATRIX_HEIGHT; ++y)
                wt_display_set_pixel_xy(x, y, col);
    }
}

// Preset 6: Plasma effect modulated by audio
static void vu_render_plasma()
{
    static float t = 0;
    t += 0.05f + g_audioHeight * 0.02f;
    
    for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x) {
        for (uint8_t y = 0; y < WT_MATRIX_HEIGHT; ++y) {
            float v = sinf(x * 0.5f + t);
            v += sinf(y * 0.8f - t * 0.7f);
            v += sinf((x + y) * 0.3f + t * 0.5f);
            v += sinf(sqrtf(x * x + y * y) * 0.4f);
            
            float intensity = (v + 4.0f) / 8.0f;
            intensity *= (0.3f + g_audioHeight / 10.0f);
            
            uint8_t hue = (uint8_t)((v * 30 + t * 50));
            uint32_t col = colorWheel(hue);
            uint8_t r = ((col >> 16) & 0xFF) * intensity;
            uint8_t g = ((col >> 8) & 0xFF) * intensity;
            uint8_t b = (col & 0xFF) * intensity;
            wt_display_set_pixel_xy(x, y, wt_color(r, g, b));
        }
    }
}

// Preset 7: Bouncing balls
static void vu_render_balls()
{
    static float ballX[5] = {5, 10, 15, 3, 17};
    static float ballY[5] = {3, 4, 2, 5, 1};
    static float ballVX[5] = {0.3f, -0.4f, 0.2f, -0.3f, 0.5f};
    static float ballVY[5] = {0.2f, 0.3f, -0.2f, 0.1f, -0.4f};
    
    // Audio affects ball speed
    float audioMult = 0.5f + g_audioHeight * 0.3f;
    
    for (uint8_t i = 0; i < 5; ++i) {
        ballX[i] += ballVX[i] * audioMult;
        ballY[i] += ballVY[i] * audioMult;
        
        if (ballX[i] < 0) { ballX[i] = 0; ballVX[i] = -ballVX[i]; }
        if (ballX[i] >= WT_MATRIX_WIDTH) { ballX[i] = WT_MATRIX_WIDTH - 1; ballVX[i] = -ballVX[i]; }
        if (ballY[i] < 0) { ballY[i] = 0; ballVY[i] = -ballVY[i]; }
        if (ballY[i] >= WT_MATRIX_HEIGHT) { ballY[i] = WT_MATRIX_HEIGHT - 1; ballVY[i] = -ballVY[i]; }
        
        // Beat boost
        if (g_audioBeat > 4) {
            ballVY[i] += (random(100) - 50) / 100.0f;
            ballVX[i] += (random(100) - 50) / 100.0f;
        }
        
        uint32_t col = colorWheel(i * 50 + millis() / 20);
        wt_display_set_pixel_xy((uint8_t)ballX[i], (uint8_t)ballY[i], col);
    }
}

// Preset 8: Matrix rain (green falling code)
static void vu_render_matrix()
{
    static uint8_t drops[WT_MATRIX_WIDTH];
    static uint8_t speeds[WT_MATRIX_WIDTH];
    static uint8_t trails[WT_MATRIX_WIDTH][WT_MATRIX_HEIGHT];
    static bool init = false;
    
    if (!init) {
        for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x) {
            drops[x] = random(WT_MATRIX_HEIGHT);
            speeds[x] = 1 + random(3);
        }
        init = true;
    }
    
    // Fade trails
    for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x)
        for (uint8_t y = 0; y < WT_MATRIX_HEIGHT; ++y)
            if (trails[x][y] > 0) trails[x][y] -= (trails[x][y] > 30) ? 30 : trails[x][y];
    
    // Move drops - faster with audio
    for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x) {
        if ((millis() / (100 - g_audioHeight * 10)) % speeds[x] == 0) {
            drops[x]--;
            if (drops[x] > 200) { // Wrapped
                drops[x] = WT_MATRIX_HEIGHT - 1;
                speeds[x] = 1 + random(3);
            }
        }
        trails[x][drops[x]] = 255;
    }
    
    // Render
    for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x) {
        for (uint8_t y = 0; y < WT_MATRIX_HEIGHT; ++y) {
            if (trails[x][y] > 0) {
                uint8_t g = trails[x][y];
                wt_display_set_pixel_xy(x, y, wt_color(g/4, g, g/4));
            }
        }
    }
}

// Preset 9: Rainbow wave
static void vu_render_rainbow_wave()
{
    static float offset = 0;
    offset += 0.2f + g_audioHeight * 0.1f;
    
    for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x) {
        float wave = sinf(x * 0.4f + offset) * g_audioHeight / 2.0f + 3.5f;
        
        for (uint8_t y = 0; y < WT_MATRIX_HEIGHT; ++y) {
            float dist = fabsf(y - wave);
            if (dist < 1.5f) {
                float intensity = 1.0f - dist / 1.5f;
                uint8_t hue = (x * 12 + (uint8_t)(offset * 10)) & 0xFF;
                uint32_t col = colorWheel(hue);
                uint8_t r = ((col >> 16) & 0xFF) * intensity;
                uint8_t g = ((col >> 8) & 0xFF) * intensity;
                uint8_t b = (col & 0xFF) * intensity;
                wt_display_set_pixel_xy(x, y, wt_color(r, g, b));
            }
        }
    }
}

// Preset 10: VU meter mirror (classic)
static void vu_render_mirror()
{
    uint8_t palette = settings_get().vuPalette;
    uint8_t center = WT_MATRIX_WIDTH / 2;
    
    for (uint8_t x = 0; x < center; ++x) {
        uint8_t h = g_vuBars[x];
        for (uint8_t y = 0; y < h; ++y) {
            uint8_t palPos = y * 36;
            uint32_t col = settings_palette_color(palette, palPos, 255);
            wt_display_set_pixel_xy(center - 1 - x, y, col);
            wt_display_set_pixel_xy(center + x, y, col);
        }
    }
}

// Preset 11: Laser scanner
static void vu_render_laser()
{
    static float angle = 0;
    angle += 0.1f + g_audioHeight * 0.05f;
    
    float cx = 10, cy = 0;
    float dx = cosf(angle) * 25;
    float dy = sinf(angle) * 10;
    
    // Draw laser line
    for (float t = 0; t < 1.0f; t += 0.02f) {
        float x = cx + dx * t;
        float y = cy + dy * t;
        if (x >= 0 && x < WT_MATRIX_WIDTH && y >= 0 && y < WT_MATRIX_HEIGHT) {
            uint8_t hue = (uint8_t)(angle * 30 + t * 50);
            uint32_t col = colorWheel(hue);
            float bright = 0.3f + g_audioHeight / 10.0f;
            uint8_t r = ((col >> 16) & 0xFF) * bright;
            uint8_t g = ((col >> 8) & 0xFF) * bright;
            uint8_t b = (col & 0xFF) * bright;
            wt_display_set_pixel_xy((uint8_t)x, (uint8_t)y, wt_color(r, g, b));
        }
    }
    
    // Beat flash
    if (g_audioBeat > 4) {
        for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x)
            wt_display_set_pixel_xy(x, 0, wt_color(255, 255, 255));
    }
}

// ============================================================================
// PLAYFUL AUDIO PRESETS - Teenage Engineering Style
// ============================================================================

// Preset 12: Dancing Stick Figure
static void vu_render_dancer()
{
    static float phase = 0;
    phase += 0.05f + g_audioHeight * 0.02f; // Slower speed
    
    // Move dancer across screen slowly
    static float dancerPos = 10.0f;
    dancerPos = 10.0f + sinf(millis() * 0.001f) * 5.0f;
    int dx = (int)dancerPos;
    
    uint32_t col = wt_color(255, 200, 100);
    
    // Ensure within bounds
    if (dx < 2) dx = 2;
    if (dx > 17) dx = 17;
    
    // Head (Inverted Y: 0->6, 6 is top)
    wt_display_set_pixel_xy(dx, 6, col);
    
    // Body (Inverted Y)
    wt_display_set_pixel_xy(dx, 5, col);
    wt_display_set_pixel_xy(dx, 4, col);
    wt_display_set_pixel_xy(dx, 3, col);
    
    // Arms - wave with audio!
    int armL = dx - 2 + (int)(sinf(phase) * g_audioHeight / 4);
    int armR = dx + 2 + (int)(sinf(phase + 1.5f) * g_audioHeight / 4);
    int armY = 1 + (int)(g_audioHeight / 5);
    if (armY > 3) armY = 3;
    int invArmY = 6 - armY;
    
    wt_display_set_pixel_xy(armL, invArmY, col);
    wt_display_set_pixel_xy(dx-1, 5, col); // Shoulder
    wt_display_set_pixel_xy(dx+1, 5, col); // Shoulder
    wt_display_set_pixel_xy(armR, invArmY, col);
    
    // Legs - kick with beat!
    int legSpread = 1 + g_audioBeat / 2;
    if (legSpread > 3) legSpread = 3;
    // Original legs at 4,5. Feet 6.
    // Inverted: Legs 2,1. Feet 0.
    wt_display_set_pixel_xy(dx - legSpread, 1, col);
    wt_display_set_pixel_xy(dx - legSpread/2, 2, col);
    wt_display_set_pixel_xy(dx + legSpread, 1, col);
    wt_display_set_pixel_xy(dx + legSpread/2, 2, col);
    
    // Feet
    wt_display_set_pixel_xy(dx - legSpread - 1, 0, col);
    wt_display_set_pixel_xy(dx + legSpread + 1, 0, col);
    
    // Dance floor with lights (at top, y=6 -> y=0? No, floor is bottom 0)
    for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x) {
        uint8_t hue = (uint8_t)(x * 12 + phase * 30);
        uint32_t floorCol = colorWheel(hue);
        uint8_t bright = 30 + (g_audioBeat > 3 ? 100 : 0);
        uint8_t r = ((floorCol >> 16) & 0xFF) * bright / 255;
        uint8_t g = ((floorCol >> 8) & 0xFF) * bright / 255;
        uint8_t b = (floorCol & 0xFF) * bright / 255;
        wt_display_set_pixel_xy(x, 0, wt_color(r, g, b));
    }
}


// Preset 13: Heartbeat monitor - ECG style waveform
static void vu_render_heartbeat()
{
    static uint8_t history[WT_MATRIX_WIDTH];
    static uint8_t histIdx = 0;
    
    // 1. Draw the Heart (Left side)
    wt_display_clear();
    
    // Beat logic - animated size based on beat decay
    uint32_t red = wt_color(255, 0, 0);
    uint32_t dimRed = wt_color(100, 0, 0);
    
    // Heart shape base at x=1, y=1
    uint8_t hx = 1, hy = 1;
    
    if (g_audioBeat > 8) {
        // BIG HEART (Full)
        // . # . # .
        // # # # # #
        // # # # # #
        // . # # # .
        // . . # . .
        wt_display_set_pixel_xy(hx+1, hy+4, red); wt_display_set_pixel_xy(hx+3, hy+4, red);
        for(int x=0; x<5; ++x) wt_display_set_pixel_xy(hx+x, hy+3, red);
        for(int x=0; x<5; ++x) wt_display_set_pixel_xy(hx+x, hy+2, red);
        wt_display_set_pixel_xy(hx+1, hy+1, red); wt_display_set_pixel_xy(hx+2, hy+1, red); wt_display_set_pixel_xy(hx+3, hy+1, red);
        wt_display_set_pixel_xy(hx+2, hy+0, red);
    } else if (g_audioBeat > 4) {
        // MEDIUM HEART (Hollow-ish)
        wt_display_set_pixel_xy(hx+1, hy+4, red); wt_display_set_pixel_xy(hx+3, hy+4, red);
        wt_display_set_pixel_xy(hx+0, hy+3, red); wt_display_set_pixel_xy(hx+4, hy+3, red);
        wt_display_set_pixel_xy(hx+0, hy+2, red); wt_display_set_pixel_xy(hx+4, hy+2, red);
        wt_display_set_pixel_xy(hx+1, hy+1, red); wt_display_set_pixel_xy(hx+3, hy+1, red);
        wt_display_set_pixel_xy(hx+2, hy+0, red);
        // Fill center dim
        wt_display_set_pixel_xy(hx+2, hy+3, dimRed);
        wt_display_set_pixel_xy(hx+2, hy+2, dimRed);
    } else {
        // SMALL HEART
        // . . . . .
        // . # . # .
        // . # # # .
        // . . # . .
        // . . . . .
        wt_display_set_pixel_xy(hx+1, hy+3, dimRed); wt_display_set_pixel_xy(hx+3, hy+3, dimRed);
        wt_display_set_pixel_xy(hx+1, hy+2, dimRed); wt_display_set_pixel_xy(hx+2, hy+2, dimRed); wt_display_set_pixel_xy(hx+3, hy+2, dimRed);
        wt_display_set_pixel_xy(hx+2, hy+1, dimRed);
    }

    // 2. Draw ECG Line (Right side)
    // Shift history
    for (int i = WT_MATRIX_WIDTH - 1; i > 7; --i) {
        history[i] = history[i-1];
    }
    
    // New sample at x=8
    // Baseline y=3
    // Pulse if beat
    if (g_audioBeat > 10) history[8] = 6;
    else if (g_audioBeat > 8) history[8] = 0; // Dip before return
    else if (g_audioBeat > 0) history[8] = 4;
    else history[8] = 3; // Baseline
    
    uint32_t green = wt_color(0, 255, 0);
    for (int x = 8; x < WT_MATRIX_WIDTH; ++x) {
        // Draw line segment (simple dots for now, maybe connect?)
        // Just dots is fine for matrix
        wt_display_set_pixel_xy(x, history[x], green);
    }
}

// Preset 14: Traffic
static void vu_render_traffic() {
    wt_display_clear();
    // Cars move right. Speed = audio level.
    static float pos[3] = {0, 5, 10};
    static uint32_t cols[3] = {0, 0, 0};
    
    if (cols[0] == 0) {
        cols[0] = wt_color(255, 0, 0);
        cols[1] = wt_color(0, 255, 0);
        cols[2] = wt_color(0, 0, 255);
    }
    
    float speed = 0.1f + g_audioHeight * 0.05f;
    
    for(int i=0; i<3; ++i) {
        pos[i] += speed;
        if (pos[i] >= WT_MATRIX_WIDTH) pos[i] -= WT_MATRIX_WIDTH;
        
        int x = (int)pos[i];
        if (x < WT_MATRIX_WIDTH) {
            wt_display_set_pixel_xy(x, 2+i, cols[i]);
            if (x+1 < WT_MATRIX_WIDTH) wt_display_set_pixel_xy(x+1, 2+i, cols[i]);
        }
    }
}

// Preset 15: Pacman - static pacman with chomping mouth, dots scroll on beat
static void vu_render_pacman() {
    wt_display_clear();
    static float dotOffset = 0;
    static uint8_t mouthPhase = 0;
    uint32_t now = millis();
    
    // Pacman at left side
    int px = 1;
    uint32_t yel = wt_color(255, 255, 0);
    
    // Animate mouth with beat - 3 phases: closed, half, wide
    if (g_audioBeat > 5) {
        mouthPhase = 2; // Wide open
    } else if (g_audioBeat > 2) {
        mouthPhase = 1; // Half open
    } else if ((now / 150) % 3 == 0) {
        mouthPhase = (mouthPhase + 1) % 3; // Chomp animation
    }
    
    // Draw Pacman (5x5 circle with mouth)
    // Row 6 (top): .XX.
    wt_display_set_pixel_xy(px + 1, 6, yel);
    wt_display_set_pixel_xy(px + 2, 6, yel);
    wt_display_set_pixel_xy(px + 3, 6, yel);
    // Row 5: XXXX
    wt_display_set_pixel_xy(px, 5, yel);
    wt_display_set_pixel_xy(px + 1, 5, yel);
    wt_display_set_pixel_xy(px + 2, 5, yel);
    wt_display_set_pixel_xy(px + 3, 5, yel);
    wt_display_set_pixel_xy(px + 4, 5, yel);
    // Row 4: XXXX or XXX (mouth)
    wt_display_set_pixel_xy(px, 4, yel);
    wt_display_set_pixel_xy(px + 1, 4, yel);
    wt_display_set_pixel_xy(px + 2, 4, yel);
    if (mouthPhase == 0) { wt_display_set_pixel_xy(px + 3, 4, yel); wt_display_set_pixel_xy(px + 4, 4, yel); }
    else if (mouthPhase == 1) { wt_display_set_pixel_xy(px + 3, 4, yel); }
    // Row 3 (middle): XXXX or XX (wide mouth)
    wt_display_set_pixel_xy(px, 3, yel);
    wt_display_set_pixel_xy(px + 1, 3, yel);
    if (mouthPhase == 0) { wt_display_set_pixel_xy(px + 2, 3, yel); wt_display_set_pixel_xy(px + 3, 3, yel); wt_display_set_pixel_xy(px + 4, 3, yel); }
    else if (mouthPhase == 1) { wt_display_set_pixel_xy(px + 2, 3, yel); wt_display_set_pixel_xy(px + 3, 3, yel); }
    else { wt_display_set_pixel_xy(px + 2, 3, yel); }
    // Row 2: XXXX or XXX (mouth)
    wt_display_set_pixel_xy(px, 2, yel);
    wt_display_set_pixel_xy(px + 1, 2, yel);
    wt_display_set_pixel_xy(px + 2, 2, yel);
    if (mouthPhase == 0) { wt_display_set_pixel_xy(px + 3, 2, yel); wt_display_set_pixel_xy(px + 4, 2, yel); }
    else if (mouthPhase == 1) { wt_display_set_pixel_xy(px + 3, 2, yel); }
    // Row 1: XXXXX
    wt_display_set_pixel_xy(px, 1, yel);
    wt_display_set_pixel_xy(px + 1, 1, yel);
    wt_display_set_pixel_xy(px + 2, 1, yel);
    wt_display_set_pixel_xy(px + 3, 1, yel);
    wt_display_set_pixel_xy(px + 4, 1, yel);
    // Row 0 (bottom): .XXX.
    wt_display_set_pixel_xy(px + 1, 0, yel);
    wt_display_set_pixel_xy(px + 2, 0, yel);
    wt_display_set_pixel_xy(px + 3, 0, yel);
    
    // Eye
    wt_display_set_pixel_xy(px + 2, 5, wt_color(0, 0, 0));
    
    // Dots scroll from right to left on beat
    if (g_audioBeat > 3) {
        dotOffset += 0.4f;
    } else {
        dotOffset += 0.08f;
    }
    if (dotOffset > 4) dotOffset -= 4;
    
    uint32_t dotCol = wt_color(255, 180, 100);
    for (int i = 0; i < 4; ++i) {
        int dx = 9 + i * 3 - (int)dotOffset;
        if (dx >= 7 && dx < WT_MATRIX_WIDTH) {
            wt_display_set_pixel_xy(dx, 3, dotCol);
        }
    }
    
    // Power pellet (blinks with beat)
    if (g_audioBeat > 2 || (now / 200) % 2) {
        wt_display_set_pixel_xy(18, 3, wt_color(255, 200, 100));
        wt_display_set_pixel_xy(19, 3, wt_color(255, 200, 100));
        wt_display_set_pixel_xy(18, 4, wt_color(255, 200, 100));
        wt_display_set_pixel_xy(19, 4, wt_color(255, 200, 100));
    }
}

// Preset 16: Vortex
static void vu_render_vortex() {
    wt_display_clear();
    static float angle = 0;
    angle += 0.1f + g_audioHeight * 0.1f;
    
    float cx = 9.5f, cy = 3.0f;
    
    for(int x=0; x<WT_MATRIX_WIDTH; ++x) {
        for(int y=0; y<WT_MATRIX_HEIGHT; ++y) {
             float dx = x - cx;
             float dy = y - cy;
             float dist = sqrt(dx*dx + dy*dy);
             float a = atan2(dy, dx);
             
             // Spiral arm
             float v = sin(dist - angle * 2 + a * 3);
             if (v > 0.5f) {
                 uint32_t col = settings_palette_color(settings_get().vuPalette, (int)(dist*20));
                 wt_display_set_pixel_xy(x, y, col);
             }
        }
    }
}

// Preset 17: Equalizer with bouncing peaks
static void vu_render_eq() {
    uint8_t palette = settings_get().vuPalette;
    
    for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x) {
        uint8_t h = g_vuBars[x];
        
        // Draw bars with gradient
        for (uint8_t y = 0; y < h; ++y) {
            uint32_t col = settings_palette_color(palette, x * 12 + y * 20);
            wt_display_set_pixel_xy(x, y, col);
        }
        
        // Bouncing peak dot
        if (g_vuPeaks[x] > 0) {
            wt_display_set_pixel_xy(x, g_vuPeaks[x], wt_color(255, 255, 255));
        }
    }
}

// Preset 18: Disco Ball - rotating sparkles
static void vu_render_disco() {
    static float angle = 0;
    angle += 0.1f + g_audioHeight * 0.05f;
    
    // Dark background
    for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x) {
        for (uint8_t y = 0; y < WT_MATRIX_HEIGHT; ++y) {
            wt_display_set_pixel_xy(x, y, wt_color(10, 5, 20));
        }
    }
    
    // Rotating sparkles
    for (int i = 0; i < 8; ++i) {
        float a = angle + i * 0.785f; // 45 degrees apart
        float r = 3.0f + g_audioHeight * 0.5f;
        int x = 10 + (int)(cosf(a) * r);
        int y = 3 + (int)(sinf(a) * r * 0.5f);
        
        if (x >= 0 && x < WT_MATRIX_WIDTH && y >= 0 && y < WT_MATRIX_HEIGHT) {
            uint8_t hue = (uint8_t)(i * 32 + angle * 20);
            uint32_t col = colorWheel(hue);
            wt_display_set_pixel_xy(x, y, col);
            
            // Glow on beat
            if (g_audioBeat > 3 && x > 0 && x < WT_MATRIX_WIDTH-1) {
                wt_display_set_pixel_xy(x-1, y, col);
                wt_display_set_pixel_xy(x+1, y, col);
            }
        }
    }
}

// Preset 19: Fireworks - explosions on beat
static void vu_render_fireworks() {
    static float particles[20][4]; // x, y, vx, vy
    static uint8_t colors[20];
    static bool init = false;
    
    if (!init) {
        for (int i = 0; i < 20; ++i) {
            particles[i][0] = -1; // Inactive
        }
        init = true;
    }
    
    // Launch new firework on beat
    if (g_audioBeat > 8) {
        for (int i = 0; i < 20; ++i) {
            if (particles[i][0] < 0) {
                float cx = 5 + random(10);
                float cy = 2 + random(3);
                float a = random(360) * 0.0174f;
                particles[i][0] = cx;
                particles[i][1] = cy;
                particles[i][2] = cosf(a) * 0.5f;
                particles[i][3] = sinf(a) * 0.3f;
                colors[i] = random(256);
                break;
            }
        }
    }
    
    // Update and draw particles
    for (int i = 0; i < 20; ++i) {
        if (particles[i][0] >= 0) {
            particles[i][0] += particles[i][2];
            particles[i][1] += particles[i][3];
            particles[i][3] -= 0.02f; // Gravity
            
            int x = (int)particles[i][0];
            int y = (int)particles[i][1];
            
            if (x >= 0 && x < WT_MATRIX_WIDTH && y >= 0 && y < WT_MATRIX_HEIGHT) {
                wt_display_set_pixel_xy(x, y, colorWheel(colors[i]));
            }
            
            // Fade out
            if (y < 0 || x < 0 || x >= WT_MATRIX_WIDTH) {
                particles[i][0] = -1;
            }
        }
    }
}

// Preset 20: Pixel Rain (falling downward)
static void vu_render_pixel_rain() {
    static uint8_t rain[WT_MATRIX_WIDTH];
    static uint8_t rainColors[WT_MATRIX_WIDTH];
    
    // Rain falls DOWN based on audio
    for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x) {
        // Spawn new drops at top when audio triggers
        if (g_vuBars[x] > 3 && random(5) == 0) {
            rain[x] = 6; // Start at top
            rainColors[x] = random(256);
        }
        
        if (rain[x] < 200) { // Valid drop
            uint32_t col = colorWheel(rainColors[x]);
            wt_display_set_pixel_xy(x, rain[x], col);
            // Trail above
            if (rain[x] < 6) {
                uint8_t r = ((col >> 16) & 0xFF) / 3;
                uint8_t g = ((col >> 8) & 0xFF) / 3;
                uint8_t b = (col & 0xFF) / 3;
                wt_display_set_pixel_xy(x, rain[x] + 1, wt_color(r, g, b));
            }
            // Move down
            if (rain[x] > 0) rain[x]--;
            else rain[x] = 255; // Mark as done
        }
    }
}

// Preset 21: Nyan Cat - bigger cat with poptart body, rainbow streams left
static void vu_render_nyan() {
    static uint8_t rainbow[WT_MATRIX_WIDTH][6]; // 6 rainbow bands
    static float hueOffset = 0;
    
    wt_display_clear();
    
    // Cat at right side - bigger poptart cat
    int cx = 13; // Cat body start x
    
    // Shift rainbow left
    for (int x = 0; x < WT_MATRIX_WIDTH - 1; ++x) {
        for (int b = 0; b < 6; ++b) {
            rainbow[x][b] = rainbow[x + 1][b];
        }
    }
    
    // Add new rainbow column from behind cat (audio reactive)
    hueOffset += 3.0f + g_audioHeight * 2.0f;
    // Classic nyan rainbow: red, orange, yellow, green, blue, purple
    rainbow[cx - 1][0] = 0;    // Red
    rainbow[cx - 1][1] = 25;   // Orange  
    rainbow[cx - 1][2] = 45;   // Yellow
    rainbow[cx - 1][3] = 85;   // Green
    rainbow[cx - 1][4] = 160;  // Blue
    rainbow[cx - 1][5] = 200;  // Purple
    
    // Draw rainbow bands (y 0-5 from bottom)
    for (int x = 0; x < cx - 1; ++x) {
        for (int b = 0; b < 6; ++b) {
            if (x < WT_MATRIX_WIDTH) {
                uint8_t hue = rainbow[x][b] + (uint8_t)hueOffset;
                uint32_t col = colorWheel(hue);
                wt_display_set_pixel_xy(x, b, col);
            }
        }
    }
    
    // Draw Nyan Cat - poptart body with cat head/tail
    // Poptart body (pink frosting)
    uint32_t poptart = wt_color(255, 150, 180); // Pink
    uint32_t toast = wt_color(200, 150, 100);   // Brown toast edges
    uint32_t catGray = wt_color(120, 120, 120); // Gray cat
    uint32_t catDark = wt_color(80, 80, 80);    // Dark gray
    
    // Toast/poptart body (5 wide, 5 tall)
    for (int x = cx; x < cx + 5 && x < WT_MATRIX_WIDTH; ++x) {
        for (int y = 1; y < 6; ++y) {
            // Pink frosting in middle
            if (x > cx && x < cx + 4 && y > 1 && y < 5) {
                wt_display_set_pixel_xy(x, y, poptart);
            } else {
                wt_display_set_pixel_xy(x, y, toast);
            }
        }
    }
    
    // Sprinkles on poptart
    wt_display_set_pixel_xy(cx + 2, 3, wt_color(255, 50, 50));
    wt_display_set_pixel_xy(cx + 3, 4, wt_color(50, 50, 255));
    
    // Cat head (sticks out right)
    if (cx + 5 < WT_MATRIX_WIDTH) {
        wt_display_set_pixel_xy(cx + 5, 3, catGray);
        wt_display_set_pixel_xy(cx + 5, 4, catGray);
    }
    if (cx + 6 < WT_MATRIX_WIDTH) {
        wt_display_set_pixel_xy(cx + 6, 4, catGray); // Ear
    }
    
    // Cat feet (bottom)
    wt_display_set_pixel_xy(cx + 1, 0, catGray);
    wt_display_set_pixel_xy(cx + 3, 0, catGray);
    
    // Cat tail (left side, wagging with beat)
    int tailY = 3 + (g_audioBeat > 3 ? 1 : 0);
    wt_display_set_pixel_xy(cx - 1, tailY, catDark);
}

// Preset 22: Ocean waves
static void vu_render_ocean() {
    static float phase = 0;
    phase += 0.08f + g_audioHeight * 0.04f;
    
    for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
        float wave1 = sinf(x * 0.5f + phase) * 2;
        float wave2 = sinf(x * 0.3f - phase * 0.7f) * 1.5f;
        int height = 3 + (int)(wave1 + wave2 + g_audioHeight * 0.5f);
        if (height > 6) height = 6;
        
        for (int y = 0; y <= height; ++y) {
            float depth = 1.0f - (float)y / 7.0f;
            uint8_t b = (uint8_t)(150 + 100 * depth);
            uint8_t g = (uint8_t)(50 + 80 * depth);
            wt_display_set_pixel_xy(x, y, wt_color(0, g, b));
        }
        // Foam on top
        if (random(10) == 0) {
            wt_display_set_pixel_xy(x, height, wt_color(255, 255, 255));
        }
    }
}

// Preset 23: Tetris blocks falling - actual tetris piece shapes
static void vu_render_tetris() {
    // Tetris pieces: I, O, T, S, Z, L, J
    static int8_t pieces[5][2];   // x, y positions
    static uint8_t pieceTypes[5]; // piece type
    static uint8_t pieceHues[5];  // color
    static bool init = false;
    static uint32_t lastUpdate = 0;
    uint32_t now = millis();
    
    if (!init) {
        for (int i = 0; i < 5; ++i) {
            pieces[i][0] = -10; // Off screen
            pieces[i][1] = -10;
        }
        init = true;
    }
    
    // Spawn new pieces on beat
    if (g_audioBeat > 4) {
        for (int i = 0; i < 5; ++i) {
            if (pieces[i][1] < 0 && random(8) == 0) {
                pieces[i][0] = random(WT_MATRIX_WIDTH - 3);
                pieces[i][1] = 6;
                pieceTypes[i] = random(7);
                pieceHues[i] = random(256);
                break;
            }
        }
    }
    
    // Update falling - slower
    if (now - lastUpdate > 120) {
        lastUpdate = now;
        for (int i = 0; i < 5; ++i) {
            if (pieces[i][1] >= 0) {
                pieces[i][1]--;
                if (pieces[i][1] < -2) pieces[i][1] = -10;
            }
        }
    }
    
    // Draw pieces
    for (int i = 0; i < 5; ++i) {
        if (pieces[i][1] < -5) continue;
        int px = pieces[i][0];
        int py = pieces[i][1];
        uint32_t col = colorWheel(pieceHues[i]);
        
        // Draw based on piece type
        switch (pieceTypes[i] % 4) {
            case 0: // I piece (horizontal)
                for (int dx = 0; dx < 4; ++dx) {
                    if (px + dx >= 0 && px + dx < WT_MATRIX_WIDTH && py >= 0 && py < 7)
                        wt_display_set_pixel_xy(px + dx, py, col);
                }
                break;
            case 1: // O piece (square)
                for (int dx = 0; dx < 2; ++dx) {
                    for (int dy = 0; dy < 2; ++dy) {
                        if (px + dx >= 0 && px + dx < WT_MATRIX_WIDTH && py + dy >= 0 && py + dy < 7)
                            wt_display_set_pixel_xy(px + dx, py + dy, col);
                    }
                }
                break;
            case 2: // T piece
                if (py >= 0 && py < 7) {
                    for (int dx = 0; dx < 3; ++dx) {
                        if (px + dx >= 0 && px + dx < WT_MATRIX_WIDTH)
                            wt_display_set_pixel_xy(px + dx, py, col);
                    }
                }
                if (py + 1 >= 0 && py + 1 < 7 && px + 1 >= 0 && px + 1 < WT_MATRIX_WIDTH)
                    wt_display_set_pixel_xy(px + 1, py + 1, col);
                break;
            case 3: // L piece
                for (int dy = 0; dy < 3; ++dy) {
                    if (px >= 0 && px < WT_MATRIX_WIDTH && py + dy >= 0 && py + dy < 7)
                        wt_display_set_pixel_xy(px, py + dy, col);
                }
                if (px + 1 >= 0 && px + 1 < WT_MATRIX_WIDTH && py >= 0 && py < 7)
                    wt_display_set_pixel_xy(px + 1, py, col);
                break;
        }
    }
}

// Preset 24: Starfield (warp speed)
static void vu_render_starfield() {
    static float stars[15][3]; // x, y, speed
    static bool init = false;
    
    if (!init) {
        for (int i = 0; i < 15; ++i) {
            stars[i][0] = random(WT_MATRIX_WIDTH);
            stars[i][1] = random(WT_MATRIX_HEIGHT);
            stars[i][2] = 0.1f + random(10) * 0.05f;
        }
        init = true;
    }
    
    float speedMult = 1.0f + g_audioHeight * 2.0f;
    
    for (int i = 0; i < 15; ++i) {
        // Move star
        float dx = (stars[i][0] - 10) * stars[i][2] * speedMult * 0.1f;
        float dy = (stars[i][1] - 3) * stars[i][2] * speedMult * 0.05f;
        stars[i][0] += dx;
        stars[i][1] += dy;
        
        // Wrap around
        if (stars[i][0] < 0 || stars[i][0] >= WT_MATRIX_WIDTH || 
            stars[i][1] < 0 || stars[i][1] >= WT_MATRIX_HEIGHT) {
            stars[i][0] = 10 + random(3) - 1;
            stars[i][1] = 3 + random(3) - 1;
            stars[i][2] = 0.1f + random(10) * 0.05f;
        }
        
        uint8_t bright = (uint8_t)(100 + stars[i][2] * 800);
        wt_display_set_pixel_xy((int)stars[i][0], (int)stars[i][1], wt_color(bright, bright, bright));
    }
}

// Preset 25: Lava lamp blobs - proper rising/falling blobs
static void vu_render_lava() {
    static float blobs[5][4]; // x, y, vy, size
    static bool init = false;
    
    // Dark red/orange background
    for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
        for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
            // Gradient from bottom (warm) to top (darker)
            uint8_t r = 30 + y * 5;
            wt_display_set_pixel_xy(x, y, wt_color(r, 5, 10));
        }
    }
    
    if (!init) {
        for (int i = 0; i < 5; ++i) {
            blobs[i][0] = 2 + i * 4;           // x spread out
            blobs[i][1] = random(7);           // y
            blobs[i][2] = (random(10) - 5) * 0.015f; // slow vy
            blobs[i][3] = 1.0f + random(10) * 0.1f;  // size
        }
        init = true;
    }
    
    for (int i = 0; i < 5; ++i) {
        // Heat rises - blobs slowly float up, then sink when cool
        // Audio adds energy (makes them rise)
        float heat = g_audioHeight * 0.02f;
        
        // Buoyancy - tendency to rise from bottom, sink from top
        float buoyancy = (3.0f - blobs[i][1]) * 0.005f;
        
        blobs[i][2] += buoyancy + heat + (random(100) - 50) * 0.0005f;
        if (blobs[i][2] > 0.1f) blobs[i][2] = 0.1f;
        if (blobs[i][2] < -0.1f) blobs[i][2] = -0.1f;
        
        blobs[i][1] += blobs[i][2];
        if (blobs[i][1] < 0) { blobs[i][1] = 0; blobs[i][2] = 0.02f; }
        if (blobs[i][1] > 6) { blobs[i][1] = 6; blobs[i][2] = -0.02f; }
        
        // Draw blob with glow
        int bx = (int)blobs[i][0];
        int by = (int)blobs[i][1];
        uint8_t brightness = 180 + i * 15;
        uint32_t col = wt_color(255, brightness / 2, 0);
        uint32_t glow = wt_color(200, brightness / 4, 0);
        
        // Core
        wt_display_set_pixel_xy(bx, by, col);
        // Glow around
        if (bx > 0) wt_display_set_pixel_xy(bx - 1, by, glow);
        if (bx < WT_MATRIX_WIDTH - 1) wt_display_set_pixel_xy(bx + 1, by, glow);
        if (by > 0) wt_display_set_pixel_xy(bx, by - 1, glow);
        if (by < 6) wt_display_set_pixel_xy(bx, by + 1, glow);
    }
}

// Preset 26: Sacred Geometry - spirograph/mandala patterns
static void vu_render_geometry() {
    static float angle = 0;
    static float phase = 0;
    uint32_t now = millis();
    
    angle += 0.02f + g_audioHeight * 0.015f;
    phase += 0.03f;
    
    int cx = 10, cy = 3;
    
    // Multiple rotating elements creating complex patterns
    // Outer ring - 8 points
    for (int i = 0; i < 8; ++i) {
        float a = angle + i * 0.785f; // 45 degrees
        float r = 4.0f + sinf(phase + i * 0.5f) * g_audioHeight * 0.5f;
        int px = cx + (int)(cosf(a) * r * 1.8f);
        int py = cy + (int)(sinf(a) * r * 0.6f);
        if (px >= 0 && px < WT_MATRIX_WIDTH && py >= 0 && py < WT_MATRIX_HEIGHT) {
            wt_display_set_pixel_xy(px, py, colorWheel((uint8_t)(angle * 15 + i * 30)));
        }
    }
    
    // Inner ring - 6 points, counter-rotating
    for (int i = 0; i < 6; ++i) {
        float a = -angle * 1.5f + i * 1.047f; // 60 degrees
        float r = 2.5f + cosf(phase * 1.3f + i * 0.3f) * g_audioHeight * 0.3f;
        int px = cx + (int)(cosf(a) * r * 1.5f);
        int py = cy + (int)(sinf(a) * r * 0.5f);
        if (px >= 0 && px < WT_MATRIX_WIDTH && py >= 0 && py < WT_MATRIX_HEIGHT) {
            wt_display_set_pixel_xy(px, py, colorWheel((uint8_t)(angle * 20 + 128 + i * 40)));
        }
    }
    
    // Connecting lines (Lissajous-like)
    for (int t = 0; t < 12; ++t) {
        float tt = t * 0.5f + angle * 2;
        float lx = sinf(tt * 2 + phase) * (5 + g_audioHeight);
        float ly = sinf(tt * 3) * (2.5f + g_audioHeight * 0.3f);
        int px = cx + (int)(lx);
        int py = cy + (int)(ly);
        if (px >= 0 && px < WT_MATRIX_WIDTH && py >= 0 && py < WT_MATRIX_HEIGHT) {
            uint8_t bright = 100 + (uint8_t)(g_audioHeight * 30);
            wt_display_set_pixel_xy(px, py, wt_color(bright, bright, bright));
        }
    }
    
    // Center pulsing dot
    uint8_t pulse = 150 + (uint8_t)(sinf(now * 0.005f) * 50 + g_audioBeat * 10);
    wt_display_set_pixel_xy(cx, cy, wt_color(pulse, pulse, pulse));
}

// Preset 27: Sparkle VU - particles burst on beat
static void vu_render_sparkle() {
    static struct { uint8_t x, y, life, hue; } sparks[40];
    static uint8_t sparkIdx = 0;
    static uint32_t lastSpawn = 0;
    uint32_t now = millis();
    uint8_t palette = settings_get().vuPalette;
    
    // Spawn new sparks on beat
    if (g_audioBeat > 3 && now - lastSpawn > 30) {
        lastSpawn = now;
        int numSpawn = 1 + g_audioBeat / 3;
        for (int i = 0; i < numSpawn && i < 5; ++i) {
            sparks[sparkIdx].x = random(WT_MATRIX_WIDTH);
            sparks[sparkIdx].y = random(WT_MATRIX_HEIGHT);
            sparks[sparkIdx].life = 20 + random(10);
            sparks[sparkIdx].hue = random(256);
            sparkIdx = (sparkIdx + 1) % 40;
        }
    }
    
    // Update and render sparks
    for (int i = 0; i < 40; ++i) {
        if (sparks[i].life > 0) {
            sparks[i].life--;
            
            // Brightness based on life
            uint8_t bright;
            if (sparks[i].life > 15) {
                bright = (30 - sparks[i].life) * 17; // Fade in
            } else {
                bright = sparks[i].life * 17; // Fade out
            }
            
            uint32_t col = settings_palette_color(palette, sparks[i].hue, bright);
            wt_display_set_pixel_xy(sparks[i].x, sparks[i].y, col);
        }
    }
    
    // Audio level drives background dim glow
    if (g_audioHeight > 2) {
        uint8_t glow = g_audioHeight * 5;
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            wt_display_set_pixel_xy(x, 0, wt_color(glow/3, glow/4, glow/2));
        }
    }
}

// Preset 28: Aurora VU - flowing waves react to audio
static void vu_render_aurora() {
    static float phase = 0;
    uint8_t palette = settings_get().vuPalette;
    
    // Phase speed driven by audio
    phase += 0.03f + g_audioHeight * 0.02f;
    
    // Audio affects wave amplitude
    float audioBoost = 0.5f + g_audioHeight * 0.15f;
    
    for (uint8_t y = 0; y < WT_MATRIX_HEIGHT; ++y) {
        for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x) {
            float nx = (float)x - 10.0f;
            float ny = (float)y - 3.0f;
            
            // Multiple overlapping waves
            float wave = sinf(nx * 0.4f + phase) + cosf(ny * 0.5f - phase * 1.2f);
            wave += sinf((nx + ny) * 0.25f + phase * 0.8f) * audioBoost;
            
            // Beat adds vertical pulse
            if (g_audioBeat > 5) {
                wave += sinf(y * 0.8f + phase * 3) * 0.5f;
            }
            
            // Normalize to 0-1
            float norm = (wave + 3.0f) / 6.0f;
            if (norm < 0) norm = 0;
            if (norm > 1) norm = 1;
            
            uint8_t palPos = (uint8_t)(norm * 255);
            uint8_t bright = 150 + (uint8_t)(g_audioHeight * 20);
            wt_display_set_pixel_xy(x, y, settings_palette_color(palette, palPos, bright));
        }
    }
}

static void vu_render()
{
    wt_display_clear();
    wt_timeline_clear();
    
    applySoundReactiveOverlay(millis());
    
    switch (g_vuPreset) {
        case 0: vu_render_spectrum(); break;
        case 1: vu_render_waveform(); break;
        case 2: vu_render_fire(); break;
        case 3: vu_render_pulse(); break;
        case 4: vu_render_waterfall(); break;
        case 5: vu_render_strobe(); break;
        case 6: vu_render_plasma(); break;
        case 7: vu_render_balls(); break;
        case 8: vu_render_matrix(); break;
        case 9: vu_render_rainbow_wave(); break;
        case 10: vu_render_mirror(); break;
        case 11: vu_render_laser(); break;
        case 12: vu_render_dancer(); break;
        case 13: vu_render_heartbeat(); break;
        case 14: vu_render_traffic(); break;
        case 15: vu_render_pacman(); break;
        case 16: vu_render_vortex(); break;
        case 17: vu_render_eq(); break;
        case 18: vu_render_disco(); break;
        case 19: vu_render_fireworks(); break;
        case 20: vu_render_pixel_rain(); break;
        case 21: vu_render_nyan(); break;
        case 22: vu_render_ocean(); break;
        case 23: vu_render_tetris(); break;
        case 24: vu_render_starfield(); break;
        case 25: vu_render_lava(); break;
        case 26: vu_render_geometry(); break;
        case 27: vu_render_sparkle(); break;
        case 28: vu_render_aurora(); break;
        default: vu_render_spectrum(); break;
    }
}

static void ticker_setup()
{
    g_tickerLastFetch = 0;
    g_tickerPrice = 0;
    g_tickerPrevPrice = 0;
    g_tickerValid = false;
    g_tickerChange = 0;
}

static void ticker_update(uint32_t now, uint32_t dt)
{
    // Fetch price at configurable interval (default 5 minutes)
    uint32_t updateMs = (uint32_t)settings_get().btcUpdateMins * 60000UL;
    if (WiFi.status() == WL_CONNECTED && (now - g_tickerLastFetch > updateMs || g_tickerLastFetch == 0))
    {
        g_tickerLastFetch = now;
        
        HTTPClient http;
        // Use HTTPS and add User-Agent header (required by CoinGecko)
        http.begin("https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd");
        http.setTimeout(15000);
        http.addHeader("User-Agent", "WeatherThing/1.0");
        http.addHeader("Accept", "application/json");
        int code = http.GET();
        
        Serial.print("BTC API response: ");
        Serial.println(code);
        
        if (code == 200)
        {
            String payload = http.getString();
            int priceIdx = payload.indexOf("\"usd\":");
            if (priceIdx >= 0)
            {
                int start = priceIdx + 6;
                int end = payload.indexOf(",", start);
                if (end < 0) end = payload.indexOf("}", start);
                
                float newPrice = payload.substring(start, end).toFloat();
                
                // Track price change - Keep change indicator if stable
                if (g_tickerValid && newPrice > 0) {
                    if (newPrice > g_tickerPrice) g_tickerChange = 1;
                    else if (newPrice < g_tickerPrice) g_tickerChange = -1;
                    // Else keep previous change state
                    
                    g_tickerPrevPrice = g_tickerPrice;
                }
                
                if (newPrice > 0) {
                    g_tickerPrice = newPrice;
                    g_tickerValid = true;
                    Serial.print("BTC: $");
                    Serial.println(g_tickerPrice);
                }
            }
        }
        http.end();
    }
}

static void ticker_render()
{
    wt_display_clear();
    wt_timeline_clear();

    // Icon always golden (Bitcoin brand color)
    uint32_t iconCol = wt_color(255, 180, 50);  // Golden
    drawBitcoinIcon(0, 0, iconCol);

    if (!g_tickerValid) {
        // Pulsing dot while loading
        uint8_t b = 50 + (millis() / 10) % 100;
        wt_display_set_pixel_xy(10, 3, wt_color(b, b / 2, 0));
        return;
    }

    // Price in K (e.g. 98500 -> 98.5)
    uint16_t kVal = (uint16_t)(g_tickerPrice / 1000.0f);
    uint16_t dec = (uint16_t)(g_tickerPrice / 100.0f) % 10;

    // Color based on price movement (matches icon)
    uint32_t pCol;
    if (g_tickerChange > 0) pCol = wt_color(50, 255, 100);       // Green - up
    else if (g_tickerChange < 0) pCol = wt_color(255, 80, 80);   // Red - down  
    else pCol = wt_color(200, 200, 200);                         // Light grey - stable
    
    uint32_t pDim = wt_color(((pCol>>16)&0xFF)/2, ((pCol>>8)&0xFF)/2, (pCol&0xFF)/2);

    // Calculate total width
    uint8_t numDigits = (kVal >= 100) ? 3 : ((kVal >= 10) ? 2 : 1);
    uint8_t totalW = numDigits * 4 + 2 + 3; // Digits + Dot + Decimal
    
    bool showDec = true;
    // Available space: starts at x=7 (icon is ~6px)
    if (7 + totalW > WT_MATRIX_WIDTH) {
        showDec = false;
        totalW = numDigits * 4 - 1;
    }
    
    uint8_t x = 7 + (WT_MATRIX_WIDTH - 7 - totalW) / 2;

    if (kVal >= 100) { drawDigit(x, 0, kVal/100, pCol); x += 4; }
    if (kVal >= 10) { drawDigit(x, 0, (kVal/10)%10, pCol); x += 4; }
    drawDigit(x, 0, kVal%10, pCol); x += 4;
    
    if (showDec) {
        wt_display_set_pixel_xy(x, 0, pDim); x += 2;
        drawDigit(x, 0, dec, pDim);
    }
    
    // Timeline shows trend - animated pulse (green/red/grey)
    uint32_t tlCol;
    if (g_tickerChange > 0) {
        uint8_t pulse = 80 + ((millis() / 50) % 80);
        tlCol = wt_color(0, pulse, pulse / 3);
    } else if (g_tickerChange < 0) {
        uint8_t pulse = 80 + ((millis() / 50) % 80);
        tlCol = wt_color(pulse, 0, 0);
    } else {
        tlCol = wt_color(80, 80, 80);  // Grey for stable
    }
    for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i) wt_timeline_set_pixel(i, tlCol);
}

// ============== STOCK TICKER CARD ==============

// Draw a simple stock chart icon (7x7)
static void drawStockIcon(uint8_t x, uint8_t y, uint32_t col, bool up)
{
    // Draw mini chart with trend line
    uint32_t dimCol = wt_color(((col>>16)&0xFF)/3, ((col>>8)&0xFF)/3, (col&0xFF)/3);
    
    // Chart border
    wt_display_set_pixel_xy(x, y, dimCol);
    wt_display_set_pixel_xy(x, y + 1, dimCol);
    wt_display_set_pixel_xy(x, y + 2, dimCol);
    wt_display_set_pixel_xy(x, y + 3, dimCol);
    wt_display_set_pixel_xy(x, y + 4, dimCol);
    wt_display_set_pixel_xy(x + 1, y, dimCol);
    wt_display_set_pixel_xy(x + 2, y, dimCol);
    wt_display_set_pixel_xy(x + 3, y, dimCol);
    wt_display_set_pixel_xy(x + 4, y, dimCol);
    wt_display_set_pixel_xy(x + 5, y, dimCol);
    
    // Trend line
    if (up) {
        wt_display_set_pixel_xy(x + 1, y + 1, col);
        wt_display_set_pixel_xy(x + 2, y + 2, col);
        wt_display_set_pixel_xy(x + 3, y + 2, col);
        wt_display_set_pixel_xy(x + 4, y + 3, col);
        wt_display_set_pixel_xy(x + 5, y + 4, col);
        // Arrow up
        wt_display_set_pixel_xy(x + 4, y + 4, col);
        wt_display_set_pixel_xy(x + 5, y + 3, col);
    } else {
        wt_display_set_pixel_xy(x + 1, y + 4, col);
        wt_display_set_pixel_xy(x + 2, y + 3, col);
        wt_display_set_pixel_xy(x + 3, y + 3, col);
        wt_display_set_pixel_xy(x + 4, y + 2, col);
        wt_display_set_pixel_xy(x + 5, y + 1, col);
        // Arrow down
        wt_display_set_pixel_xy(x + 4, y + 1, col);
        wt_display_set_pixel_xy(x + 5, y + 2, col);
    }
}

static void stock_setup()
{
    g_stockLastFetch = 0;
    g_stockPrice = 0;
    g_stockPrevPrice = 0;
    g_stockValid = false;
    g_stockChange = 0;
}

static void stock_update(uint32_t now, uint32_t dt)
{
    Settings& cfg = settings_get();
    
    // Skip if no symbol configured
    if (!cfg.stockEnabled || cfg.stockSymbol[0] == '\0') {
        g_stockValid = false;
        return;
    }
    
    // Fetch price at configurable interval (default 5 minutes)
    uint32_t updateMs = (uint32_t)cfg.stockUpdateMins * 60000UL;
    if (WiFi.status() == WL_CONNECTED && (now - g_stockLastFetch > updateMs || g_stockLastFetch == 0))
    {
        g_stockLastFetch = now;
        
        // Use Yahoo Finance chart API (public, no auth)
        String url = "https://query1.finance.yahoo.com/v8/finance/chart/";
        url += cfg.stockSymbol;
        url += "?interval=1d&range=1d";
        
        HTTPClient http;
        http.begin(url);
        http.setTimeout(10000);
        http.addHeader("User-Agent", "WeatherThing/1.0");
        int code = http.GET();
        
        if (code == 200)
        {
            String payload = http.getString();
            
            // Parse regularMarketPrice from response
            int priceIdx = payload.indexOf("\"regularMarketPrice\":");
            if (priceIdx >= 0)
            {
                int start = priceIdx + 21;
                int end = payload.indexOf(",", start);
                if (end < 0) end = payload.indexOf("}", start);
                
                float newPrice = payload.substring(start, end).toFloat();
                
                if (newPrice > 0) {
                    // Track price change - Keep change indicator if stable
                    if (g_stockValid) {
                        if (newPrice > g_stockPrice) g_stockChange = 1;
                        else if (newPrice < g_stockPrice) g_stockChange = -1;
                        // Else keep previous change state
                        
                        g_stockPrevPrice = g_stockPrice;
                    }
                    
                    g_stockPrice = newPrice;
                    g_stockValid = true;
                    Serial.print("Stock ");
                    Serial.print(cfg.stockSymbol);
                    Serial.print(": $");
                    Serial.println(g_stockPrice);
                }
            }
        }
        http.end();
    }
}

static void stock_render()
{
    wt_display_clear();
    wt_timeline_clear();
    
    Settings& cfg = settings_get();
    
    // Check if stock is enabled
    if (!cfg.stockEnabled || cfg.stockSymbol[0] == '\0') {
        // Show "SET" message
        uint32_t col = wt_color(100, 100, 100);
        drawDigit(2, 0, 5, col);  // S looks like 5
        // Draw E
        wt_display_set_pixel_xy(7, 0, col);
        wt_display_set_pixel_xy(7, 3, col);
        wt_display_set_pixel_xy(7, 6, col);
        for (uint8_t y = 0; y < 7; ++y) wt_display_set_pixel_xy(6, y, col);
        // Draw T
        for (uint8_t x = 10; x < 14; ++x) wt_display_set_pixel_xy(x, 6, col);
        for (uint8_t y = 0; y < 7; ++y) wt_display_set_pixel_xy(12, y, col);
        return;
    }
    
    // Icon always golden
    uint32_t iconCol = wt_color(255, 180, 50);  // Golden
    drawStockIcon(0, 1, iconCol, g_stockChange >= 0);
    
    if (!g_stockValid) {
        // Pulsing dot while loading
        uint8_t b = 50 + (millis() / 10) % 100;
        wt_display_set_pixel_xy(10, 3, wt_color(b, b, b));
        return;
    }
    
    // Format price - show with appropriate precision
    // For prices < 1000, show 2 decimals (XX.XX)
    // For prices >= 1000, show in K format (XX.XK)
    uint32_t pCol;
    if (g_stockChange > 0) pCol = wt_color(50, 255, 100);       // Green
    else if (g_stockChange < 0) pCol = wt_color(255, 80, 80);   // Red
    else pCol = wt_color(200, 200, 200);                        // Grey - neutral
    
    uint32_t pDim = wt_color(((pCol>>16)&0xFF)/2, ((pCol>>8)&0xFF)/2, (pCol&0xFF)/2);
    
    uint8_t x = 7;
    
    if (g_stockPrice >= 1000) {
        // Show as XX.XK or XXXK
        uint16_t kVal = (uint16_t)(g_stockPrice / 1000.0f);
        uint16_t dec = (uint16_t)(g_stockPrice / 100.0f) % 10;
        
        bool showDec = (kVal < 100); // Only show decimal if < 100K
        
        if (kVal >= 100) {
             drawDigit(x, 0, kVal / 100, pCol); x += 4;
             drawDigit(x, 0, (kVal / 10) % 10, pCol); x += 4;
             drawDigit(x, 0, kVal % 10, pCol); x += 4;
        } else {
             if (kVal >= 10) { drawDigit(x, 0, kVal / 10, pCol); x += 4; }
             drawDigit(x, 0, kVal % 10, pCol); x += 4;
        }
        
        if (showDec) {
            wt_display_set_pixel_xy(x, 0, pDim); x += 2;
            drawDigit(x, 0, dec, pDim);
        }
        
    } else if (g_stockPrice >= 100) {
        // Show as XXX
        uint16_t val = (uint16_t)g_stockPrice;
        drawDigit(x, 0, val / 100, pCol); x += 4;
        drawDigit(x, 0, (val / 10) % 10, pCol); x += 4;
        drawDigit(x, 0, val % 10, pDim);
    } else if (g_stockPrice >= 10) {
        // Show as XX.X
        uint16_t val = (uint16_t)(g_stockPrice * 10);
        drawDigit(x, 0, val / 100, pCol); x += 4;
        drawDigit(x, 0, (val / 10) % 10, pCol); x += 4;
        wt_display_set_pixel_xy(x, 0, pDim); x += 2;
        drawDigit(x, 0, val % 10, pDim);
    } else {
        // Show as X.XX
        uint16_t val = (uint16_t)(g_stockPrice * 100);
        drawDigit(x, 0, val / 100, pCol); x += 4;
        wt_display_set_pixel_xy(x, 0, pDim); x += 2;
        drawDigit(x, 0, (val / 10) % 10, pCol); x += 4;
        drawDigit(x, 0, val % 10, pDim);
    }
    
    // Timeline shows trend (green/red/grey)
    uint32_t tlCol;
    if (g_stockChange > 0) {
        uint8_t pulse = 80 + ((millis() / 50) % 80);
        tlCol = wt_color(0, pulse, pulse / 3);
    } else if (g_stockChange < 0) {
        uint8_t pulse = 80 + ((millis() / 50) % 80);
        tlCol = wt_color(pulse, 0, 0);
    } else {
        tlCol = wt_color(80, 80, 80);  // Grey - neutral
    }
    for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i) wt_timeline_set_pixel(i, tlCol);
}

// ============== GAMES CARD ==============

// Game state
static float g_birdY = 3.0f;
static float g_birdVel = 0;
static int16_t g_pipeX = 20;
static uint8_t g_pipeGap = 2;
static uint8_t g_pipeGapY = 3;
static uint16_t g_gameScore = 0;
static bool g_gameOver = false;
static uint32_t g_gameOverTime = 0;

// Snake game state
static int8_t g_snakeX[50], g_snakeY[50];
static uint8_t g_snakeLen = 3;
static int8_t g_snakeDX = 1, g_snakeDY = 0;
static int8_t g_foodX = 10, g_foodY = 3;

// Breakout state
static float g_ballX = 10, g_ballY = 3;
static float g_ballDX = 0.3f, g_ballDY = 0.2f;
static uint8_t g_paddleX = 8;
static uint8_t g_bricks[20];  // Bitmap of 20 bricks at top

// Pong state
static uint8_t g_paddle1Y = 3, g_paddle2Y = 3;
static float g_pongBallX = 10, g_pongBallY = 3;
static float g_pongBallDX = 0.2f, g_pongBallDY = 0.1f;
static uint8_t g_score1 = 0, g_score2 = 0;

// Button state for games
static bool g_gameBtn1 = false, g_gameBtn2 = false;
static bool g_gameBtn1Edge = false, g_gameBtn2Edge = false;
static bool g_gameTouchEdge = false;

static void game_reset_flappy()
{
    g_birdY = 3.0f;
    g_birdVel = 0;
    g_pipeX = 22;
    g_pipeGapY = 2 + random(3);
    g_gameScore = 0;
    g_gameOver = false;
}

static void game_reset_snake()
{
    g_snakeLen = 3;
    for (uint8_t i = 0; i < g_snakeLen; ++i) {
        g_snakeX[i] = 10 - i;
        g_snakeY[i] = 3;
    }
    g_snakeDX = 1; g_snakeDY = 0;
    g_foodX = 15; g_foodY = 3;
    g_gameScore = 0;
    g_gameOver = false;
}

static void game_reset_breakout()
{
    g_ballX = 10; g_ballY = 2;
    g_ballDX = 0.08f; g_ballDY = 0.06f;  // Much slower ball
    g_paddleX = 8;
    for (uint8_t i = 0; i < 20; ++i) g_bricks[i] = 1;
    g_gameScore = 0;
    g_gameOver = false;
}

static void game_reset_pong()
{
    g_pongBallX = 10; g_pongBallY = 3;
    g_pongBallDX = 0.06f; g_pongBallDY = 0.04f;  // Slower ball
    g_paddle1Y = 3; g_paddle2Y = 3;
    g_score1 = 0; g_score2 = 0;
    g_gameOver = false;
}

static void games_setup()
{
    // Start the current game directly (no menu)
    switch (g_gameMode) {
        case 0: game_reset_flappy(); break;
        case 1: game_reset_snake(); break;
        case 2: game_reset_breakout(); break;
        case 3: game_reset_pong(); break;
    }
}

static void game_start_current()
{
    switch (g_gameMode) {
        case 0: game_reset_flappy(); break;
        case 1: game_reset_snake(); break;
        case 2: game_reset_breakout(); break;
        case 3: game_reset_pong(); break;
    }
}

static void game_update_flappy(uint32_t now)
{
    static uint32_t lastUpdate = 0;
    
    if (g_gameOver) {
        if (g_gameTouchEdge || g_gameBtn1Edge || g_gameBtn2Edge) {
            game_reset_flappy();
        }
        return;
    }
    
    // Flap on any button/touch
    if (g_gameTouchEdge || g_gameBtn1Edge || g_gameBtn2Edge) {
        g_birdVel = 0.4f;
    }
    
    // Update at 20fps (50ms intervals) - much slower
    if (now - lastUpdate < 50) return;
    lastUpdate = now;
    
    // Physics (slower gravity)
    g_birdVel -= 0.03f;
    g_birdY += g_birdVel;
    
    // Bounds
    if (g_birdY < 0) { g_birdY = 0; g_birdVel = 0; }
    if (g_birdY > 6) { g_birdY = 6; g_birdVel = 0; }
    
    // Move pipe (slower)
    static uint8_t pipeCounter = 0;
    pipeCounter++;
    if (pipeCounter >= 2) {  // Move pipe every 2nd frame
        pipeCounter = 0;
        g_pipeX--;
        if (g_pipeX < -3) {
            g_pipeX = 22;
            g_pipeGapY = 1 + random(4);
            g_gameScore++;
        }
    }
    
    // Collision with pipe
    uint8_t birdYi = (uint8_t)g_birdY;
    if (g_pipeX >= 2 && g_pipeX <= 4) {
        if (birdYi < g_pipeGapY || birdYi > g_pipeGapY + g_pipeGap) {
            g_gameOver = true;
            g_gameOverTime = now;
        }
    }
}

static void game_update_snake(uint32_t now)
{
    static uint32_t lastMove = 0;
    
    if (g_gameOver) {
        if (g_gameTouchEdge || g_gameBtn1Edge || g_gameBtn2Edge) {
            game_reset_snake();
        }
        return;
    }
    
    // Button 1 = turn left, Button 2 = turn right
    if (g_gameBtn1Edge) {
        int8_t tmp = g_snakeDX;
        g_snakeDX = g_snakeDY;
        g_snakeDY = -tmp;
    }
    if (g_gameBtn2Edge) {
        int8_t tmp = g_snakeDX;
        g_snakeDX = -g_snakeDY;
        g_snakeDY = tmp;
    }
    
    // Move every 250ms - human playable speed
    if (now - lastMove < 250) return;
    lastMove = now;
    
    // Move body
    for (int8_t i = g_snakeLen - 1; i > 0; --i) {
        g_snakeX[i] = g_snakeX[i-1];
        g_snakeY[i] = g_snakeY[i-1];
    }
    
    // Move head
    g_snakeX[0] += g_snakeDX;
    g_snakeY[0] += g_snakeDY;
    
    // Wrap around
    if (g_snakeX[0] < 0) g_snakeX[0] = WT_MATRIX_WIDTH - 1;
    if (g_snakeX[0] >= WT_MATRIX_WIDTH) g_snakeX[0] = 0;
    if (g_snakeY[0] < 0) g_snakeY[0] = WT_MATRIX_HEIGHT - 1;
    if (g_snakeY[0] >= WT_MATRIX_HEIGHT) g_snakeY[0] = 0;
    
    // Check self collision
    for (uint8_t i = 1; i < g_snakeLen; ++i) {
        if (g_snakeX[0] == g_snakeX[i] && g_snakeY[0] == g_snakeY[i]) {
            g_gameOver = true;
            return;
        }
    }
    
    // Check food
    if (g_snakeX[0] == g_foodX && g_snakeY[0] == g_foodY) {
        if (g_snakeLen < 50) g_snakeLen++;
        g_gameScore++;
        g_foodX = random(WT_MATRIX_WIDTH);
        g_foodY = random(WT_MATRIX_HEIGHT);
    }
}

static void game_update_breakout(uint32_t now)
{
    static uint32_t lastPaddleMove = 0;
    static uint32_t lastBallMove = 0;
    
    if (g_gameOver) {
        if (g_gameTouchEdge || g_gameBtn1Edge || g_gameBtn2Edge) {
            game_reset_breakout();
        }
        return;
    }
    
    // Paddle control with rate limiting (move every 80ms)
    if (now - lastPaddleMove > 80) {
        if (g_gameBtn1 && g_paddleX > 0) { g_paddleX--; lastPaddleMove = now; }
        if (g_gameBtn2 && g_paddleX < WT_MATRIX_WIDTH - 4) { g_paddleX++; lastPaddleMove = now; }
    }
    
    // Ball movement rate limiting (move every 50ms)
    if (now - lastBallMove < 50) return;
    lastBallMove = now;
    
    // Ball physics
    g_ballX += g_ballDX;
    g_ballY += g_ballDY;
    
    // Wall bounce
    if (g_ballX < 0) { g_ballX = 0; g_ballDX = -g_ballDX; }
    if (g_ballX >= WT_MATRIX_WIDTH) { g_ballX = WT_MATRIX_WIDTH - 1; g_ballDX = -g_ballDX; }
    if (g_ballY >= WT_MATRIX_HEIGHT - 1) { g_ballY = WT_MATRIX_HEIGHT - 1; g_ballDY = -g_ballDY; }
    
    // Paddle bounce
    if (g_ballY < 1 && g_ballDY < 0) {
        uint8_t bx = (uint8_t)g_ballX;
        if (bx >= g_paddleX && bx < g_paddleX + 4) {
            g_ballDY = -g_ballDY;
            g_ballY = 1;
            // Add spin based on where it hit
            g_ballDX += (bx - g_paddleX - 1.5f) * 0.05f;
        } else if (g_ballY < 0) {
            g_gameOver = true;
            return;
        }
    }
    
    // Brick collision (top row)
    if (g_ballY >= 5 && g_ballDY > 0) {
        uint8_t bx = (uint8_t)g_ballX;
        if (bx < 20 && g_bricks[bx]) {
            g_bricks[bx] = 0;
            g_ballDY = -g_ballDY;
            g_gameScore++;
        }
    }
    
    // Check win
    bool allGone = true;
    for (uint8_t i = 0; i < 20; ++i) if (g_bricks[i]) allGone = false;
    if (allGone) {
        g_gameOver = true;
    }
}

static void game_update_pong(uint32_t now)
{
    if (g_gameOver) {
        if (g_gameTouchEdge || g_gameBtn1Edge || g_gameBtn2Edge) {
            game_reset_pong();
        }
        return;
    }
    
    // Player 1 (left): btn1 = up, touch = down
    if (g_gameBtn1 && g_paddle1Y < 5) g_paddle1Y++;
    if (wt_cap_touch_active() && g_paddle1Y > 0) g_paddle1Y--;
    
    // Player 2 (right): btn2 = up, AI or manual
    if (g_gameBtn2 && g_paddle2Y < 5) g_paddle2Y++;
    // Simple AI if no btn2
    if (!g_gameBtn2) {
        if (g_pongBallY > g_paddle2Y + 1 && g_paddle2Y < 5) g_paddle2Y++;
        if (g_pongBallY < g_paddle2Y && g_paddle2Y > 0) g_paddle2Y--;
    }
    
    // Ball physics
    g_pongBallX += g_pongBallDX;
    g_pongBallY += g_pongBallDY;
    
    // Top/bottom bounce
    if (g_pongBallY < 0) { g_pongBallY = 0; g_pongBallDY = -g_pongBallDY; }
    if (g_pongBallY > 6) { g_pongBallY = 6; g_pongBallDY = -g_pongBallDY; }
    
    // Left paddle
    if (g_pongBallX < 2 && g_pongBallDX < 0) {
        uint8_t by = (uint8_t)g_pongBallY;
        if (by >= g_paddle1Y && by <= g_paddle1Y + 2) {
            g_pongBallDX = -g_pongBallDX * 1.02f;  // Slower speedup
            g_pongBallX = 2;
        } else if (g_pongBallX < 0) {
            g_score2++;
            g_pongBallX = 10; g_pongBallY = 3;
            g_pongBallDX = 0.06f;  // Reset to slow speed
            if (g_score2 >= 5) g_gameOver = true;
        }
    }
    
    // Right paddle
    if (g_pongBallX > 17 && g_pongBallDX > 0) {
        uint8_t by = (uint8_t)g_pongBallY;
        if (by >= g_paddle2Y && by <= g_paddle2Y + 2) {
            g_pongBallDX = -g_pongBallDX * 1.02f;  // Slower speedup
            g_pongBallX = 17;
        } else if (g_pongBallX > 19) {
            g_score1++;
            g_pongBallX = 10; g_pongBallY = 3;
            g_pongBallDX = -0.06f;  // Reset to slow speed
            if (g_score1 >= 5) g_gameOver = true;
        }
    }
    
    // Clamp ball speed (lower max)
    if (g_pongBallDX > 0.15f) g_pongBallDX = 0.15f;
    if (g_pongBallDX < -0.15f) g_pongBallDX = -0.15f;
}

static void games_update(uint32_t now, uint32_t dt)
{
    // Read buttons for game control - use global edge flags
    bool touch = wt_cap_touch_active();
    static bool lastTouch = false;
    
    // Use global edge detection instead of calling pressed() again
    g_gameBtn1 = wt_button1_is_down();
    g_gameBtn2 = wt_button2_is_down();
    g_gameTouchEdge = touch && !lastTouch;
    
    // When game over: BTN1 = next game, BTN2 = prev game, Touch = restart
    if (g_gameOver) {
        if (g_btn1Edge) {
            // Next game
            g_gameMode = (g_gameMode + 1) % GAME_MODE_COUNT;
            game_start_current();
        } else if (g_btn2Edge) {
            // Previous game
            g_gameMode = (g_gameMode + GAME_MODE_COUNT - 1) % GAME_MODE_COUNT;
            game_start_current();
        } else if (g_gameTouchEdge) {
            // Restart current game
            game_start_current();
        }
    }
    
    lastTouch = touch;
    
    // Run the current game
    switch (g_gameMode) {
        case 0: game_update_flappy(now); break;
        case 1: game_update_snake(now); break;
        case 2: game_update_breakout(now); break;
        case 3: game_update_pong(now); break;
    }
}

static void game_render_flappy()
{
    // Bird
    uint8_t by = (uint8_t)g_birdY;
    uint32_t birdCol = g_gameOver ? wt_color(255, 0, 0) : wt_color(255, 255, 0);
    wt_display_set_pixel_xy(3, by, birdCol);
    wt_display_set_pixel_xy(2, by, wt_color(255, 150, 0));
    
    // Pipe
    if (g_pipeX >= 0 && g_pipeX < WT_MATRIX_WIDTH) {
        for (uint8_t y = 0; y < WT_MATRIX_HEIGHT; ++y) {
            if (y < g_pipeGapY || y > g_pipeGapY + g_pipeGap) {
                wt_display_set_pixel_xy(g_pipeX, y, wt_color(0, 200, 0));
                if (g_pipeX + 1 < WT_MATRIX_WIDTH)
                    wt_display_set_pixel_xy(g_pipeX + 1, y, wt_color(0, 150, 0));
            }
        }
    }
    
    // Score on timeline
    for (uint8_t i = 0; i < g_gameScore && i < WT_TIMELINE_PIXELS; ++i) {
        wt_timeline_set_pixel(i, wt_color(0, 255, 0));
    }
    
    if (g_gameOver) {
        // Flash "GAME OVER" effect
        if ((millis() / 200) % 2) {
            for (uint8_t x = 5; x < 15; ++x)
                wt_display_set_pixel_xy(x, 3, wt_color(255, 0, 0));
        }
    }
}

static void game_render_snake()
{
    // Food (blinking)
    if ((millis() / 100) % 2) {
        wt_display_set_pixel_xy(g_foodX, g_foodY, wt_color(255, 0, 0));
    }
    
    // Snake
    for (uint8_t i = 0; i < g_snakeLen; ++i) {
        uint8_t bright = 255 - i * 4;
        uint32_t col = (i == 0) ? wt_color(0, 255, 0) : wt_color(0, bright, 0);
        wt_display_set_pixel_xy(g_snakeX[i], g_snakeY[i], col);
    }
    
    // Score on timeline
    for (uint8_t i = 0; i < g_gameScore && i < WT_TIMELINE_PIXELS; ++i) {
        wt_timeline_set_pixel(i, wt_color(0, 255, 0));
    }
    
    if (g_gameOver) {
        for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i)
            wt_timeline_set_pixel(i, wt_color(255, 0, 0));
    }
}

static void game_render_breakout()
{
    // Paddle
    for (uint8_t x = g_paddleX; x < g_paddleX + 4; ++x) {
        wt_display_set_pixel_xy(x, 0, wt_color(100, 100, 255));
    }
    
    // Ball
    wt_display_set_pixel_xy((uint8_t)g_ballX, (uint8_t)g_ballY, wt_color(255, 255, 255));
    
    // Bricks (top row at y=6)
    for (uint8_t x = 0; x < 20; ++x) {
        if (g_bricks[x]) {
            uint8_t hue = x * 12;
            wt_display_set_pixel_xy(x, 6, colorWheel(hue));
        }
    }
    
    // Score on timeline
    for (uint8_t i = 0; i < g_gameScore && i < WT_TIMELINE_PIXELS; ++i) {
        wt_timeline_set_pixel(i, wt_color(255, 255, 0));
    }
}

static void game_render_pong()
{
    // Left paddle (player 1)
    for (uint8_t y = g_paddle1Y; y < g_paddle1Y + 3 && y < WT_MATRIX_HEIGHT; ++y) {
        wt_display_set_pixel_xy(0, y, wt_color(0, 150, 255));
    }
    
    // Right paddle (player 2)
    for (uint8_t y = g_paddle2Y; y < g_paddle2Y + 3 && y < WT_MATRIX_HEIGHT; ++y) {
        wt_display_set_pixel_xy(19, y, wt_color(255, 150, 0));
    }
    
    // Ball
    wt_display_set_pixel_xy((uint8_t)g_pongBallX, (uint8_t)g_pongBallY, wt_color(255, 255, 255));
    
    // Center line
    for (uint8_t y = 0; y < WT_MATRIX_HEIGHT; y += 2) {
        wt_display_set_pixel_xy(10, y, wt_color(50, 50, 50));
    }
    
    // Scores on timeline (split in half)
    for (uint8_t i = 0; i < g_score1 && i < 6; ++i)
        wt_timeline_set_pixel(i, wt_color(0, 150, 255));
    for (uint8_t i = 0; i < g_score2 && i < 6; ++i)
        wt_timeline_set_pixel(11 - i, wt_color(255, 150, 0));
    
    if (g_gameOver) {
        // Winner flash
        uint32_t winCol = (g_score1 > g_score2) ? wt_color(0, 150, 255) : wt_color(255, 150, 0);
        if ((millis() / 150) % 2) {
            for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i)
                wt_timeline_set_pixel(i, winCol);
        }
    }
}

static void render_game_menu()
{
    // Draw game name
    const char* name = g_gameNames[g_gameMode];
    uint8_t len = strlen(name);
    uint8_t textW = len * 4;
    int16_t x = (WT_MATRIX_WIDTH - textW) / 2;
    
    // Game-specific colors
    uint32_t color;
    switch (g_gameMode) {
        case 0: color = wt_color(255, 255, 0); break;   // Flappy - yellow
        case 1: color = wt_color(0, 255, 100); break;   // Snake - green
        case 2: color = wt_color(255, 100, 50); break;  // Breakout - orange
        case 3: color = wt_color(100, 200, 255); break; // Pong - cyan
        default: color = wt_color(255, 255, 255); break;
    }
    
    // Draw game name
    for (uint8_t i = 0; i < len; ++i) {
        drawChar3x5(x + i * 4, 1, name[i], color);
    }
    
    // Animated arrows on timeline (< prev | next >)
    uint32_t t = millis() / 200;
    uint8_t pulse = 100 + ((millis() / 100) % 100);
    
    // Left arrow (button 1)
    wt_timeline_set_pixel(0, wt_color(pulse, pulse, 0));
    wt_timeline_set_pixel(1, wt_color(pulse / 2, pulse / 2, 0));
    
    // Right arrow (button 2)  
    wt_timeline_set_pixel(WT_TIMELINE_PIXELS - 1, wt_color(pulse, pulse, 0));
    wt_timeline_set_pixel(WT_TIMELINE_PIXELS - 2, wt_color(pulse / 2, pulse / 2, 0));
    
    // Game indicator dots in middle
    for (uint8_t i = 0; i < GAME_MODE_COUNT; ++i) {
        uint8_t tlIdx = 4 + i * 2;
        if (tlIdx < WT_TIMELINE_PIXELS) {
            uint32_t dotCol = (i == g_gameMode) ? color : wt_color(40, 40, 40);
            wt_timeline_set_pixel(tlIdx, dotCol);
        }
    }
}

static void games_render()
{
    wt_display_clear();
    wt_timeline_clear();
    
    switch (g_gameMode) {
        case 0: game_render_flappy(); break;
        case 1: game_render_snake(); break;
        case 2: game_render_breakout(); break;
        case 3: game_render_pong(); break;
    }
}

// Sparkle/particle animation card
static void sparkle_setup()
{
    for (uint8_t i = 0; i < SPARKLE_MAX; ++i)
    {
        g_sparkles[i].life = 0;
    }
    g_sparkleMode = 0;
}

static void sparkle_update(uint32_t now, uint32_t dt)
{
    // Sound reactive - apply overlay
    applySoundReactiveOverlay(now);
    
    // Check cap touch to cycle modes
    bool btn = wt_cap_touch_active();
    static bool lastBtn = false;
    if (btn && !lastBtn)
    {
        g_sparkleMode = (g_sparkleMode + 1) % 3;
    }
    lastBtn = btn;
    
    // Update existing sparkles
    for (uint8_t i = 0; i < SPARKLE_MAX; ++i)
    {
        if (g_sparkles[i].life > 0)
        {
            g_sparkles[i].life--;
            
            // Mode-specific movement
            if (g_sparkleMode == 2 && g_sparkles[i].life > 0) // Rain mode - fall down
            {
                if ((now / 50) % 2 == 0 && g_sparkles[i].y > 0)
                {
                    g_sparkles[i].y--;
                }
            }
        }
    }
    
    // Spawn new sparkles - MORE on beat!
    uint8_t baseChance = (g_sparkleMode == 0) ? 3 : (g_sparkleMode == 1) ? 2 : 4;
    uint8_t spawnChance = baseChance + (g_audioBeat / 2); // Beat boosts spawning
    if (random(10) < spawnChance)
    {
        // Find empty slot
        for (uint8_t i = 0; i < SPARKLE_MAX; ++i)
        {
            if (g_sparkles[i].life == 0) {
                g_sparkles[i].life = 8 + random(12);
                g_sparkles[i].hue = random(256);
                // Spawn more toward center when loud
                if (g_audioHeight > 3) {
                    g_sparkles[i].x = 5 + random(10);
                    g_sparkles[i].y = 1 + random(5);
                } else {
                    g_sparkles[i].x = random(WT_MATRIX_WIDTH);
                    g_sparkles[i].y = random(WT_MATRIX_HEIGHT);
                }
                break;
            }
        }
    }
}

static void sparkle_render()
{
    wt_display_clear();
    wt_timeline_clear();
    
    uint8_t palette = settings_get().vuPalette;
    
    for (uint8_t i = 0; i < SPARKLE_MAX; ++i)
    {
        if (g_sparkles[i].life > 0)
        {
            // Brightness based on life (fade in then out)
            uint8_t bright;
            if (g_sparkles[i].life > 12)
            {
                bright = (20 - g_sparkles[i].life) * 32; // Fade in
            }
            else
            {
                bright = g_sparkles[i].life * 21; // Fade out
            }
            
            // Use palette colors for sparkles
            uint32_t col = settings_palette_color(palette, g_sparkles[i].hue, bright);
            wt_display_set_pixel_xy(g_sparkles[i].x, g_sparkles[i].y, col);
        }
    }
    
    // Timeline shows palette gradient with pulse
    for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i)
    {
        uint8_t pulse = (millis() / 50 + i * 20) % 256;
        uint8_t b = 50 + (pulse < 128 ? pulse : 255 - pulse);
        uint8_t palPos = i * 21;  // Spread across palette
        wt_timeline_set_pixel(i, settings_palette_color(palette, palPos, b));
    }
}

// ============================================================================

static int16_t g_mqttScrollX = 0;
static uint32_t g_mqttLastScroll = 0;
static uint8_t g_mqttDisplayMode = 0;  // 0=message, 1=status

// Simple icon bitmaps (7x7)
// ... (rest of the code remains the same)
static const uint8_t MQTT_ICON_BELL_DATA[] = {
    0b0001000,
    0b0011100,
    0b0011100,
    0b0111110,
    0b0111110,
    0b1111111,
    0b0001000
};

static const uint8_t MQTT_ICON_HOME_DATA[] = {
    0b0001000,
    0b0010100,
    0b0100010,
    0b1111111,
    0b0100010,
    0b0100010,
    0b0111110
};

static const uint8_t MQTT_ICON_ALERT_DATA[] = {
    0b0001000,
    0b0011100,
    0b0011100,
    0b0011100,
    0b0001000,
    0b0000000,
    0b0001000
};

static const uint8_t MQTT_ICON_INFO_DATA[] = {
    0b0001000,
    0b0000000,
    0b0011100,
    0b0001000,
    0b0001000,
    0b0001000,
    0b0011100
};

static const uint8_t MQTT_ICON_CHECK_DATA[] = {
    0b0000000,
    0b0000001,
    0b0000010,
    0b1000100,
    0b0101000,
    0b0010000,
    0b0000000
};

static void drawMqttIcon(uint8_t iconType, uint8_t x, uint8_t y, uint32_t color)
{
    const uint8_t* data = nullptr;
    
    switch (iconType) {
        case MQTT_ICON_BELL:  data = MQTT_ICON_BELL_DATA; break;
        case MQTT_ICON_HOME:  data = MQTT_ICON_HOME_DATA; break;
        case MQTT_ICON_ALERT: data = MQTT_ICON_ALERT_DATA; break;
        case MQTT_ICON_INFO:  data = MQTT_ICON_INFO_DATA; break;
        case MQTT_ICON_CHECK: data = MQTT_ICON_CHECK_DATA; break;
        default: return;
    }
    
    for (uint8_t row = 0; row < 7; ++row) {
        uint8_t bits = data[6 - row];  // Flip vertically
        for (uint8_t col = 0; col < 7; ++col) {
            if (bits & (1 << (6 - col))) {
                wt_display_set_pixel_xy(x + col, y + row, color);
            }
        }
    }
}

static void mqttcard_setup()
{
    g_mqttScrollX = WT_MATRIX_WIDTH;
    g_mqttLastScroll = millis();
    g_mqttDisplayMode = 0;
}

static void mqttcard_update(uint32_t now, uint32_t dt)
{
    (void)dt;
    
    // Check for new messages
    if (mqtt_has_new_message()) {
        g_mqttScrollX = WT_MATRIX_WIDTH;
        g_mqttDisplayMode = 0;
    }
    
    // Scroll text if message is long
    const char* msg = mqtt_get_message();
    int16_t msgLen = strlen(msg);
    int16_t textWidth = msgLen * 4;
    
    // Only scroll if text is wider than display area (after icon)
    int16_t displayArea = WT_MATRIX_WIDTH - 8;  // 8 pixels for icon
    
    if (textWidth > displayArea) {
        if (now - g_mqttLastScroll > 120) {
            g_mqttLastScroll = now;
            g_mqttScrollX--;
            if (g_mqttScrollX < -textWidth) {
                g_mqttScrollX = displayArea;
            }
        }
    } else {
        // Center short text
        g_mqttScrollX = 8 + (displayArea - textWidth) / 2;
    }
    
    // Toggle display mode with touch
    bool touch = wt_cap_touch_active();
    static bool lastTouch = false;
    if (touch && !lastTouch) {
        g_mqttDisplayMode = (g_mqttDisplayMode + 1) % 2;
    }
    lastTouch = touch;
}

static void mqttcard_render()
{
    wt_display_clear();
    wt_timeline_clear();
    
    uint32_t now = millis();
    bool connected = mqtt_is_connected();
    
    if (g_mqttDisplayMode == 1 || mqtt_get_message()[0] == '\0') {
        // Status mode - show connection status
        uint32_t statusColor = connected ? wt_color(0, 255, 100) : wt_color(255, 100, 0);
        
        // Draw home icon
        drawMqttIcon(MQTT_ICON_HOME, 0, 0, statusColor);
        
        // Draw status text
        const char* status = connected ? "ONLINE" : "OFFLINE";
        uint8_t len = strlen(status);
        int16_t x = 9;
        for (uint8_t i = 0; i < len; ++i) {
            drawChar3x5(x + i * 4, 1, status[i], statusColor);
        }
        
        // Timeline shows connection animation
        for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i) {
            uint8_t phase = (now / 100 + i * 15) % 256;
            uint8_t bright = connected ? (50 + phase / 3) : (30 + (phase % 60));
            if (connected) {
                wt_timeline_set_pixel(i, wt_color(0, bright, bright / 2));
            } else {
                wt_timeline_set_pixel(i, wt_color(bright, bright / 4, 0));
            }
        }
        return;
    }
    
    // Message mode
    uint32_t msgColor = mqtt_get_color();
    uint8_t r = (msgColor >> 16) & 0xFF;
    uint8_t g = (msgColor >> 8) & 0xFF;
    uint8_t b = msgColor & 0xFF;
    uint32_t displayColor = wt_color(r, g, b);
    
    // Draw icon on left
    uint8_t icon = mqtt_get_icon();
    if (icon > 0 && icon < MQTT_ICON_COUNT) {
        drawMqttIcon(icon, 0, 0, displayColor);
    }
    
    // Draw scrolling message
    const char* msg = mqtt_get_message();
    uint8_t len = strlen(msg);
    for (uint8_t i = 0; i < len && i < 32; ++i) {
        int16_t cx = g_mqttScrollX + (int16_t)i * 4;
        if (cx >= 7 && cx < WT_MATRIX_WIDTH + 4) {
            drawChar3x5(cx, 1, msg[i], displayColor);
        }
    }
    
    // Timeline shows message age (fades from bright to dim)
    uint32_t age = mqtt_get_message_age_ms();
    uint8_t baseBright = (age < 30000) ? (255 - age / 120) : 50;
    
    for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i) {
        uint8_t pulse = (now / 80 + i * 20) % 128;
        uint8_t bright = baseBright / 4 + pulse / 4;
        
        uint8_t pr = (r * bright) / 255;
        uint8_t pg = (g * bright) / 255;
        uint8_t pb = (b * bright) / 255;
        wt_timeline_set_pixel(i, wt_color(pr, pg, pb));
    }
}

// ============================================================================
// RSS Card - Custom RSS Feed Reader with Extended Character Support
// ============================================================================

static char g_rssTitle[2048] = "Loading RSS...";
static uint32_t g_rssLastFetch = 0;
static int16_t g_rssScrollX = 20;
static uint32_t g_rssLastScroll = 0;
static bool g_rssValid = false;

// Extended 4x7 font for Latvian characters (ĀČĒĢĪĶĻŅŠŪŽ)
static const uint8_t FONT_EXT[][7] = {
    // Uppercase (with macron/cedilla marks)
    {0b0100, 0b0110, 0b1001, 0b1111, 0b1001, 0b1001, 0b1001}, // Ā
    {0b0100, 0b0111, 0b1000, 0b1000, 0b1000, 0b1000, 0b0111}, // Č
    {0b0100, 0b1111, 0b1000, 0b1110, 0b1000, 0b1000, 0b1111}, // Ē
    {0b0111, 0b1000, 0b1000, 0b1011, 0b1001, 0b0111, 0b0010}, // Ģ
    {0b0100, 0b0000, 0b1110, 0b0100, 0b0100, 0b0100, 0b1110}, // Ī
    {0b1001, 0b1010, 0b1100, 0b1000, 0b1100, 0b1010, 0b0010}, // Ķ
    {0b1000, 0b1000, 0b1000, 0b1000, 0b1000, 0b1111, 0b0010}, // Ļ
    {0b1001, 0b1101, 0b1011, 0b1001, 0b1001, 0b1001, 0b0010}, // Ņ
    {0b0100, 0b0111, 0b1000, 0b0110, 0b0001, 0b0001, 0b1110}, // Š
    {0b0100, 0b0000, 0b1001, 0b1001, 0b1001, 0b1001, 0b0110}, // Ū
    {0b0100, 0b1111, 0b0001, 0b0010, 0b0100, 0b1000, 0b1111}, // Ž
    
    // Lowercase
    {0b0100, 0b0000, 0b0110, 0b1001, 0b1001, 0b1001, 0b0111}, // ā
    {0b0100, 0b0000, 0b0111, 0b1000, 0b1000, 0b1000, 0b0111}, // č
    {0b0100, 0b0000, 0b0110, 0b1001, 0b1111, 0b1000, 0b0111}, // ē
    {0b0000, 0b0111, 0b1001, 0b1001, 0b0111, 0b0001, 0b0010}, // ģ
    {0b0100, 0b0000, 0b0100, 0b0100, 0b0100, 0b0100, 0b0100}, // ī
    {0b1000, 0b1000, 0b1010, 0b1100, 0b1010, 0b1001, 0b0010}, // ķ
    {0b1100, 0b0100, 0b0100, 0b0100, 0b0100, 0b0100, 0b0010}, // ļ
    {0b0000, 0b0000, 0b1110, 0b1001, 0b1001, 0b1001, 0b0010}, // ņ
    {0b0100, 0b0000, 0b0111, 0b1000, 0b0110, 0b0001, 0b1110}, // š
    {0b0100, 0b0000, 0b1001, 0b1001, 0b1001, 0b1001, 0b0110}, // ū
    {0b0100, 0b0000, 0b1111, 0b0001, 0b0010, 0b0100, 0b1111}  // ž
};

// 4x7 digits for RSS
static const uint8_t FONT_DIGITS_7[][7] = {
    {0b0110, 0b1001, 0b1001, 0b1001, 0b1001, 0b1001, 0b0110}, // 0
    {0b0100, 0b1100, 0b0100, 0b0100, 0b0100, 0b0100, 0b1110}, // 1
    {0b0110, 0b1001, 0b0001, 0b0010, 0b0100, 0b1000, 0b1111}, // 2
    {0b1110, 0b0001, 0b0001, 0b0110, 0b0001, 0b0001, 0b1110}, // 3
    {0b1001, 0b1001, 0b1001, 0b1111, 0b0001, 0b0001, 0b0001}, // 4
    {0b1111, 0b1000, 0b1000, 0b1110, 0b0001, 0b0001, 0b1110}, // 5
    {0b0111, 0b1000, 0b1000, 0b1110, 0b1001, 0b1001, 0b0110}, // 6
    {0b1111, 0b0001, 0b0001, 0b0010, 0b0100, 0b0100, 0b0100}, // 7
    {0b0110, 0b1001, 0b1001, 0b0110, 0b1001, 0b1001, 0b0110}, // 8
    {0b0110, 0b1001, 0b1001, 0b0111, 0b0001, 0b0001, 0b1110}, // 9
};

// Bitmap for space and punctuation - 4x7
static const uint8_t FONT_SPACE[7] = {0,0,0,0,0,0,0};
static const uint8_t FONT_DOT[7] = {0,0,0,0,0,0,0b0100};
static const uint8_t FONT_COMMA[7] = {0,0,0,0,0,0b0100,0b1000};
static const uint8_t FONT_COLON[7] = {0,0,0b0100,0,0,0b0100,0};
static const uint8_t FONT_DASH[7] = {0,0,0,0b1111,0,0,0};
static const uint8_t FONT_QUEST[7] = {0b0110,0b1001,0b0001,0b0010,0b0100,0,0b0100};
static const uint8_t FONT_EXCL[7] = {0b0100,0b0100,0b0100,0b0100,0b0100,0,0b0100};
static const uint8_t FONT_APOS[7] = {0b0100,0b0100,0,0,0,0,0};
static const uint8_t FONT_QUOT[7] = {0b1010,0b1010,0,0,0,0,0};

static const uint8_t* getCharBitmap(uint8_t c) {
    if (c >= 'A' && c <= 'Z') return FONT_4X7[c - 'A'];
    if (c >= 'a' && c <= 'z') return FONT_4X7[c - 'a'];
    if (c >= '0' && c <= '9') return FONT_DIGITS_7[c - '0'];
    if (c >= 128 && c < 150) return FONT_EXT[c - 128];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') return FONT_SPACE;
    if (c == '.') return FONT_DOT;
    if (c == ',') return FONT_COMMA;
    if (c == ':') return FONT_COLON;
    if (c == '-' || c == '_') return FONT_DASH;
    if (c == '?' ) return FONT_QUEST;
    if (c == '!') return FONT_EXCL;
    if (c == '\'' || c == '`') return FONT_APOS;
    if (c == '"') return FONT_QUOT;
    return FONT_SPACE; // Unknown chars become space
}

static void cleanRSSString(String& s) {
    s.trim();
    // Handle CDATA
    if (s.startsWith("<![CDATA[")) {
        s = s.substring(9);
        int end = s.indexOf("]]>");
        if (end > 0) s = s.substring(0, end);
    }
    
    // Strip HTML tags
    while (true) {
        int tagStart = s.indexOf("<");
        if (tagStart < 0) break;
        int tagEnd = s.indexOf(">", tagStart);
        if (tagEnd < 0) break;
        s.remove(tagStart, tagEnd - tagStart + 1);
    }
    
    // Decode entities
    s.replace("&quot;", "\"");
    s.replace("&apos;", "'");
    s.replace("&lt;", "<");
    s.replace("&gt;", ">");
    s.replace("&amp;", "&");
    s.replace("&#39;", "'");
    s.replace("&#x27;", "'");
    s.replace("&nbsp;", " ");
    
    // Collapse spaces
    while (s.indexOf("  ") >= 0) {
        s.replace("  ", " ");
    }
    
    s.trim();
}

static void fetchRSS() {
    Settings& cfg = settings_get();
    if (strlen(cfg.rssUrl) < 5) {
        snprintf(g_rssTitle, sizeof(g_rssTitle), "Configure RSS URL in web panel");
        g_rssValid = false;
        return;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        snprintf(g_rssTitle, sizeof(g_rssTitle), "WiFi disconnected");
        g_rssValid = false;
        return;
    }
    
    Serial.printf("[RSS] Fetching: %s\n", cfg.rssUrl);
    
    HTTPClient http;
    WiFiClientSecure *secureClient = nullptr;
    
    // Use WiFiClientSecure for HTTPS URLs
    if (strncmp(cfg.rssUrl, "https://", 8) == 0) {
        secureClient = new WiFiClientSecure();
        secureClient->setInsecure(); // Skip certificate validation
        http.begin(*secureClient, cfg.rssUrl);
    } else {
        http.begin(cfg.rssUrl);
    }
    
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.addHeader("User-Agent", "WeatherThing/1.0");
    http.addHeader("Accept", "application/rss+xml, application/xml, text/xml");
    
    int httpCode = http.GET();
    Serial.printf("[RSS] HTTP code: %d\n", httpCode);
    
    if (httpCode != HTTP_CODE_OK) {
        snprintf(g_rssTitle, sizeof(g_rssTitle), "RSS error %d", httpCode);
        g_rssValid = false;
        http.end();
        if (secureClient) delete secureClient;
        return;
    }
    
    // Read response
    String response = "";
    WiFiClient *stream = http.getStreamPtr();
    uint32_t startTime = millis();
    int bytesRead = 0;
    const int maxBytes = 48000; // Increased buffer for multiple items
    
    while (stream->available() || (millis() - startTime) < 5000) {
        if (stream->available()) {
            int toRead = min(1024, stream->available());
            char buf[1025];
            int len = stream->readBytes(buf, toRead);
            if (len > 0) {
                buf[len] = 0;
                response += buf;
                bytesRead += len;
                if (bytesRead >= maxBytes) break;
                startTime = millis(); // Reset timeout on data
            }
        }
        delay(1);
    }
    
    http.end();
    
    // Parse Items
    String fullText = "";
    int searchPos = 0;
    int itemsFound = 0;
    int targetItems = (cfg.rssItemCount > 0) ? cfg.rssItemCount : 3;
    
    while (itemsFound < targetItems) {
        // Find item start
        int itemStart = response.indexOf("<item", searchPos);
        if (itemStart < 0) itemStart = response.indexOf("<entry", searchPos); // Atom support
        if (itemStart < 0) break;
        
        int itemEnd = response.indexOf("</item>", itemStart);
        if (itemEnd < 0) itemEnd = response.indexOf("</entry>", itemStart);
        if (itemEnd < 0) itemEnd = response.length();
        
        // Find Title
        int titleStart = response.indexOf("<title", itemStart);
        if (titleStart > 0 && titleStart < itemEnd) {
            int contentStart = response.indexOf(">", titleStart) + 1;
            int titleEnd = response.indexOf("</title>", contentStart);
            
            if (contentStart > 0 && titleEnd > 0) {
                String title = response.substring(contentStart, titleEnd);
                cleanRSSString(title);
                
                if (fullText.length() > 0) fullText += "   ***   ";
                fullText += title;
                
                // Find Description/Summary if requested
                if (cfg.rssFormat == 1) {
                    int descStart = response.indexOf("<description", itemStart);
                    if (descStart < 0) descStart = response.indexOf("<summary", itemStart);
                    
                    if (descStart > 0 && descStart < itemEnd) {
                        int dContentStart = response.indexOf(">", descStart) + 1;
                        int dEnd = response.indexOf("</", dContentStart);
                        
                        if (dContentStart > 0 && dEnd > 0) {
                            String desc = response.substring(dContentStart, dEnd);
                            cleanRSSString(desc);
                            if (desc.length() > 0) {
                                fullText += ": " + desc;
                            }
                        }
                    }
                }
            }
        }
        
        searchPos = itemEnd;
        itemsFound++;
    }
    
    if (fullText.length() == 0) {
        snprintf(g_rssTitle, sizeof(g_rssTitle), "No items found");
        g_rssValid = false;
    } else {
        // Convert UTF-8 to display format
        int outLen = 0;
        for(size_t i = 0; i < fullText.length() && outLen < (int)sizeof(g_rssTitle) - 1; i++) {
            uint8_t c = fullText[i];
            
            // Handle UTF-8 multi-byte sequences for Latvian chars
            if (c == 0xC4 || c == 0xC5) {
                if (i + 1 < fullText.length()) {
                    uint8_t c2 = fullText[++i];
                    uint8_t code = 0;
                    if (c == 0xC4) {
                        if(c2==0x80) code=128; else if(c2==0x81) code=139;
                        else if(c2==0x8C) code=129; else if(c2==0x8D) code=140;
                        else if(c2==0x92) code=130; else if(c2==0x93) code=141;
                        else if(c2==0x9C) code=131; else if(c2==0x9D) code=142;
                        else if(c2==0xAA) code=132; else if(c2==0xAB) code=143;
                        else if(c2==0xB6) code=133; else if(c2==0xB7) code=144;
                        else if(c2==0xBB) code=134; else if(c2==0xBC) code=145;
                    } else if (c == 0xC5) {
                        if(c2==0x85) code=135; else if(c2==0x86) code=146;
                        else if(c2==0x60) code=136; else if(c2==0x61) code=147;
                        else if(c2==0xAA) code=137; else if(c2==0xAB) code=148;
                        else if(c2==0xBD) code=138; else if(c2==0xBE) code=149;
                    }
                    if (code != 0) g_rssTitle[outLen++] = code;
                }
            } else if ((c & 0xE0) == 0xC0) {
                i++; // Skip other 2-byte char
            } else if ((c & 0xF0) == 0xE0) {
                i += 2; // Skip 3-byte char
            } else if ((c & 0xF8) == 0xF0) {
                i += 3; // Skip 4-byte char
            } else if (c < 128) {
                g_rssTitle[outLen++] = c;
            }
        }
        g_rssTitle[outLen] = 0;
        g_rssValid = true;
        Serial.printf("[RSS] Text: %s\n", g_rssTitle);
    }
    
    if (secureClient) delete secureClient;
}

static void rss_setup() {
    g_rssScrollX = WT_MATRIX_WIDTH;
    g_rssLastFetch = 0;
    Settings& cfg = settings_get();
    if (cfg.rssUrl[0] == 0) {
        strcpy(g_rssTitle, "Set RSS URL in web panel");
    } else {
        strcpy(g_rssTitle, "Loading RSS...");
    }
}

static void rss_update(uint32_t now, uint32_t dt) {
    Settings& cfg = settings_get();
    if (now - g_rssLastFetch > (uint32_t)cfg.rssUpdateMins * 60000 || g_rssLastFetch == 0) {
        fetchRSS();
        g_rssLastFetch = now;
    }
    
    uint32_t scrollSpeed = 100 - (cfg.rssSpeed * 8); 
    if (scrollSpeed < 20) scrollSpeed = 20;
    
    if (now - g_rssLastScroll > scrollSpeed) {
        g_rssScrollX--;
        int len = strlen(g_rssTitle);
        if (g_rssScrollX < -(len * 5)) { // 4 pixels wide + 1 space
            g_rssScrollX = WT_MATRIX_WIDTH;
        }
        g_rssLastScroll = now;
    }
}

static void rss_render() {
    wt_display_clear();
    wt_timeline_clear();
    Settings& cfg = settings_get();
    int len = strlen(g_rssTitle);
    
    for(int i=0; i<len; ++i) {
        int16_t x = g_rssScrollX + i * 5; // 4 pixels wide + 1 space
        if (x > -5 && x < WT_MATRIX_WIDTH) {
            uint32_t charCol = settings_palette_color(cfg.rssPalette, (i * 10) % 255);
            const uint8_t* bitmap = getCharBitmap((uint8_t)g_rssTitle[i]);
            // Full height 4x7 font: row 0 = top, row 6 = bottom
            for(int r=0; r<7; ++r) {
                for(int cx=0; cx<4; ++cx) {
                    if (bitmap[r] & (1<<(3-cx))) {
                        wt_display_set_pixel_xy(x + cx, 6 - r, charCol);
                    }
                }
            }
        }
    }
    for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i) {
        wt_timeline_set_pixel(i, settings_palette_color(cfg.rssPalette, i * 20));
    }
}

// ============== SOCIAL MEDIA CARDS ==============

// Shared social card state
static uint32_t g_ytLastFetch = 0;
static uint32_t g_ytSubs = 0;
static bool g_ytValid = false;

static uint32_t g_twitchLastFetch = 0;
static uint32_t g_twitchFollowers = 0;
static bool g_twitchValid = false;

static uint32_t g_twitterLastFetch = 0;
static uint32_t g_twitterFollowers = 0;
static bool g_twitterValid = false;

static uint32_t g_instaLastFetch = 0;
static uint32_t g_instaFollowers = 0;
static bool g_instaValid = false;

static uint32_t g_tiktokLastFetch = 0;
static uint32_t g_tiktokFollowers = 0;
static bool g_tiktokValid = false;

// Helper: Format large numbers (K/M notation)
// Returns formatted string in provided buffer
static void formatCount(uint32_t count, char* buf, size_t bufSize) {
    if (count >= 1000000) {
        // Millions: show X.XM
        uint32_t millions = count / 1000000;
        uint32_t remainder = (count % 1000000) / 100000;
        if (millions >= 100) {
            snprintf(buf, bufSize, "%luM", (unsigned long)millions);
        } else if (millions >= 10) {
            snprintf(buf, bufSize, "%luM", (unsigned long)millions);
        } else {
            snprintf(buf, bufSize, "%lu.%luM", (unsigned long)millions, (unsigned long)remainder);
        }
    } else if (count >= 10000) {
        // Tens of thousands: show XXK
        snprintf(buf, bufSize, "%luK", (unsigned long)(count / 1000));
    } else if (count >= 1000) {
        // Thousands: show X.XK
        uint32_t thousands = count / 1000;
        uint32_t remainder = (count % 1000) / 100;
        snprintf(buf, bufSize, "%lu.%luK", (unsigned long)thousands, (unsigned long)remainder);
    } else {
        // Under 1000: show exact number
        snprintf(buf, bufSize, "%lu", (unsigned long)count);
    }
}

// Draw YouTube play button icon (7x7) - Red with white triangle
static void drawYouTubeIcon(uint8_t x, uint8_t y) {
    uint32_t red = wt_color(255, 0, 0);
    uint32_t white = wt_color(255, 255, 255);
    
    // Red rounded rectangle
    wt_display_set_pixel_xy(x+1, y, red);
    wt_display_set_pixel_xy(x+2, y, red);
    wt_display_set_pixel_xy(x+3, y, red);
    wt_display_set_pixel_xy(x+4, y, red);
    wt_display_set_pixel_xy(x+5, y, red);
    for(int r=1; r<=5; r++) {
        for(int c=0; c<7; c++) {
            wt_display_set_pixel_xy(x+c, y+r, red);
        }
    }
    wt_display_set_pixel_xy(x+1, y+6, red);
    wt_display_set_pixel_xy(x+2, y+6, red);
    wt_display_set_pixel_xy(x+3, y+6, red);
    wt_display_set_pixel_xy(x+4, y+6, red);
    wt_display_set_pixel_xy(x+5, y+6, red);
    
    // White play triangle
    wt_display_set_pixel_xy(x+2, y+2, white);
    wt_display_set_pixel_xy(x+2, y+3, white);
    wt_display_set_pixel_xy(x+2, y+4, white);
    wt_display_set_pixel_xy(x+3, y+3, white);
    wt_display_set_pixel_xy(x+4, y+3, white);
}

// Draw Twitch icon (7x7) - Purple with speech bubble
static void drawTwitchIcon(uint8_t x, uint8_t y) {
    uint32_t purple = wt_color(145, 70, 255);
    
    // Speech bubble shape
    wt_display_set_pixel_xy(x+1, y, purple);
    wt_display_set_pixel_xy(x+2, y, purple);
    wt_display_set_pixel_xy(x+3, y, purple);
    wt_display_set_pixel_xy(x+4, y, purple);
    wt_display_set_pixel_xy(x+5, y, purple);
    for(int r=1; r<=4; r++) {
        wt_display_set_pixel_xy(x, y+r, purple);
        wt_display_set_pixel_xy(x+6, y+r, purple);
        wt_display_set_pixel_xy(x+1, y+r, purple);
        wt_display_set_pixel_xy(x+5, y+r, purple);
    }
    wt_display_set_pixel_xy(x, y+5, purple);
    wt_display_set_pixel_xy(x+1, y+5, purple);
    wt_display_set_pixel_xy(x+2, y+5, purple);
    wt_display_set_pixel_xy(x+3, y+5, purple);
    wt_display_set_pixel_xy(x+1, y+6, purple);
    wt_display_set_pixel_xy(x+2, y+6, purple);
}

// Draw Twitter/X icon (7x7) - Black X
static void drawTwitterIcon(uint8_t x, uint8_t y) {
    uint32_t black = wt_color(255, 255, 255); // White on dark display
    
    // X shape
    wt_display_set_pixel_xy(x, y, black);
    wt_display_set_pixel_xy(x+6, y, black);
    wt_display_set_pixel_xy(x+1, y+1, black);
    wt_display_set_pixel_xy(x+5, y+1, black);
    wt_display_set_pixel_xy(x+2, y+2, black);
    wt_display_set_pixel_xy(x+4, y+2, black);
    wt_display_set_pixel_xy(x+3, y+3, black);
    wt_display_set_pixel_xy(x+2, y+4, black);
    wt_display_set_pixel_xy(x+4, y+4, black);
    wt_display_set_pixel_xy(x+1, y+5, black);
    wt_display_set_pixel_xy(x+5, y+5, black);
    wt_display_set_pixel_xy(x, y+6, black);
    wt_display_set_pixel_xy(x+6, y+6, black);
}

// Draw Instagram icon (7x7) - Gradient camera
static void drawInstaIcon(uint8_t x, uint8_t y) {
    uint32_t pink = wt_color(255, 0, 100);
    uint32_t orange = wt_color(255, 150, 0);
    uint32_t purple = wt_color(180, 50, 200);
    
    // Outer square with gradient effect
    wt_display_set_pixel_xy(x+1, y, orange);
    wt_display_set_pixel_xy(x+2, y, orange);
    wt_display_set_pixel_xy(x+3, y, pink);
    wt_display_set_pixel_xy(x+4, y, pink);
    wt_display_set_pixel_xy(x+5, y, purple);
    for(int r=1; r<=5; r++) {
        wt_display_set_pixel_xy(x, y+r, (r<3) ? orange : pink);
        wt_display_set_pixel_xy(x+6, y+r, (r<3) ? pink : purple);
    }
    wt_display_set_pixel_xy(x+1, y+6, pink);
    wt_display_set_pixel_xy(x+2, y+6, pink);
    wt_display_set_pixel_xy(x+3, y+6, purple);
    wt_display_set_pixel_xy(x+4, y+6, purple);
    wt_display_set_pixel_xy(x+5, y+6, purple);
    
    // Camera lens circle
    wt_display_set_pixel_xy(x+2, y+2, pink);
    wt_display_set_pixel_xy(x+3, y+2, pink);
    wt_display_set_pixel_xy(x+4, y+2, pink);
    wt_display_set_pixel_xy(x+2, y+3, pink);
    wt_display_set_pixel_xy(x+4, y+3, pink);
    wt_display_set_pixel_xy(x+2, y+4, pink);
    wt_display_set_pixel_xy(x+3, y+4, pink);
    wt_display_set_pixel_xy(x+4, y+4, pink);
    
    // Flash dot
    wt_display_set_pixel_xy(x+5, y+1, wt_color(255, 255, 255));
}

// Draw TikTok icon (7x7) - Musical note with cyan/red offset
static void drawTikTokIcon(uint8_t x, uint8_t y) {
    uint32_t white = wt_color(255, 255, 255);
    uint32_t cyan = wt_color(0, 255, 255);
    uint32_t red = wt_color(255, 0, 80);
    
    // Note stem
    wt_display_set_pixel_xy(x+4, y, white);
    wt_display_set_pixel_xy(x+4, y+1, white);
    wt_display_set_pixel_xy(x+4, y+2, white);
    wt_display_set_pixel_xy(x+4, y+3, white);
    wt_display_set_pixel_xy(x+4, y+4, white);
    
    // Note head
    wt_display_set_pixel_xy(x+2, y+5, white);
    wt_display_set_pixel_xy(x+3, y+5, white);
    wt_display_set_pixel_xy(x+2, y+6, white);
    wt_display_set_pixel_xy(x+3, y+6, white);
    
    // Cyan offset (left)
    wt_display_set_pixel_xy(x+3, y, cyan);
    wt_display_set_pixel_xy(x+3, y+1, cyan);
    wt_display_set_pixel_xy(x+1, y+5, cyan);
    wt_display_set_pixel_xy(x+1, y+6, cyan);
    
    // Red offset (right)
    wt_display_set_pixel_xy(x+5, y+1, red);
    wt_display_set_pixel_xy(x+5, y+2, red);
    
    // Flag
    wt_display_set_pixel_xy(x+5, y, white);
    wt_display_set_pixel_xy(x+6, y+1, white);
}

// Helper to render social count with icon
static void renderSocialCard(void (*drawIcon)(uint8_t, uint8_t), uint32_t count, bool valid, uint32_t brandColor) {
    wt_display_clear();
    wt_timeline_clear();
    
    // Draw icon at left
    drawIcon(0, 0);
    
    if (!valid) {
        // Pulsing dot while loading
        uint8_t b = 50 + (millis() / 10) % 100;
        wt_display_set_pixel_xy(10, 3, wt_color(b, b, b));
        return;
    }
    
    // Format count
    char countStr[12];
    formatCount(count, countStr, sizeof(countStr));
    
    // Calculate text width
    int len = strlen(countStr);
    int textWidth = len * 4 + (len - 1); // 4px per char + 1px spacing
    
    // Start after icon (icon is 7px wide)
    int startX = 8 + (WT_MATRIX_WIDTH - 8 - textWidth) / 2;
    if (startX < 8) startX = 8;
    
    // Draw count
    for(int i = 0; i < len; i++) {
        int16_t x = startX + i * 5;
        if (x >= 0 && x < WT_MATRIX_WIDTH - 3) {
            drawDigit(x, 0, countStr[i] - '0', brandColor);
            // Handle letters (K, M, .)
            if (countStr[i] == 'K' || countStr[i] == 'M' || countStr[i] == '.') {
                const uint8_t* bitmap = getCharBitmap((uint8_t)countStr[i]);
                for(int r=0; r<7; ++r) {
                    for(int cx=0; cx<4; ++cx) {
                        if (bitmap[r] & (1<<(3-cx))) {
                            wt_display_set_pixel_xy(x + cx, 6 - r, brandColor);
                        }
                    }
                }
            }
        }
    }
    
    // Timeline in brand color
    for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i) {
        uint8_t r = (brandColor >> 16) & 0xFF;
        uint8_t g = (brandColor >> 8) & 0xFF;
        uint8_t b = brandColor & 0xFF;
        uint8_t pulse = 60 + ((millis() / 50 + i * 3) % 60);
        wt_timeline_set_pixel(i, wt_color(r * pulse / 120, g * pulse / 120, b * pulse / 120));
    }
}

// ============== YOUTUBE CARD ==============

static void youtube_setup() {
    g_ytLastFetch = 0;
    g_ytSubs = 0;
    g_ytValid = false;
}

static void youtube_update(uint32_t now, uint32_t dt) {
    Settings& cfg = settings_get();
    
    // Skip if not configured
    if (cfg.ytChannelId[0] == '\0' || cfg.ytApiKey[0] == '\0') {
        g_ytValid = false;
        return;
    }
    
    uint32_t updateMs = (uint32_t)cfg.socialUpdateMins * 60000UL;
    if (WiFi.status() == WL_CONNECTED && (now - g_ytLastFetch > updateMs || g_ytLastFetch == 0)) {
        g_ytLastFetch = now;
        
        String url = "https://www.googleapis.com/youtube/v3/channels?part=statistics&id=";
        url += cfg.ytChannelId;
        url += "&key=";
        url += cfg.ytApiKey;
        
        HTTPClient http;
        http.begin(url);
        http.setTimeout(15000);
        int code = http.GET();
        
        Serial.printf("[YouTube] API response: %d\n", code);
        
        if (code == 200) {
            String payload = http.getString();
            int subIdx = payload.indexOf("\"subscriberCount\"");
            if (subIdx >= 0) {
                int start = payload.indexOf("\"", subIdx + 17) + 1;
                int end = payload.indexOf("\"", start);
                if (start > 0 && end > start) {
                    g_ytSubs = payload.substring(start, end).toInt();
                    g_ytValid = true;
                    Serial.printf("[YouTube] Subscribers: %lu\n", (unsigned long)g_ytSubs);
                }
            }
        }
        http.end();
    }
}

static void youtube_render() {
    renderSocialCard(drawYouTubeIcon, g_ytSubs, g_ytValid, wt_color(255, 0, 0));
}

// ============== TWITCH CARD ==============

static void twitch_setup() {
    g_twitchLastFetch = 0;
    g_twitchFollowers = 0;
    g_twitchValid = false;
}

static void twitch_update(uint32_t now, uint32_t dt) {
    Settings& cfg = settings_get();
    
    // Skip if not configured
    if (cfg.twitchUser[0] == '\0' || cfg.twitchClientId[0] == '\0') {
        g_twitchValid = false;
        return;
    }
    
    uint32_t updateMs = (uint32_t)cfg.socialUpdateMins * 60000UL;
    if (WiFi.status() == WL_CONNECTED && (now - g_twitchLastFetch > updateMs || g_twitchLastFetch == 0)) {
        g_twitchLastFetch = now;
        
        // Note: Twitch API requires OAuth token, showing placeholder
        // For real implementation, would need OAuth flow
        Serial.println("[Twitch] API requires OAuth - showing placeholder");
        g_twitchFollowers = 0;
        g_twitchValid = false;
    }
}

static void twitch_render() {
    renderSocialCard(drawTwitchIcon, g_twitchFollowers, g_twitchValid, wt_color(145, 70, 255));
}

// ============== TWITTER/X CARD ==============

static void twitter_setup() {
    g_twitterLastFetch = 0;
    g_twitterFollowers = 0;
    g_twitterValid = false;
}

static void twitter_update(uint32_t now, uint32_t dt) {
    Settings& cfg = settings_get();
    
    // Skip if not configured
    if (cfg.twitterUser[0] == '\0') {
        g_twitterValid = false;
        return;
    }
    
    uint32_t updateMs = (uint32_t)cfg.socialUpdateMins * 60000UL;
    if (WiFi.status() == WL_CONNECTED && (now - g_twitterLastFetch > updateMs || g_twitterLastFetch == 0)) {
        g_twitterLastFetch = now;
        
        // Note: Twitter API v2 requires Bearer token
        Serial.println("[Twitter] API requires Bearer token - showing placeholder");
        g_twitterFollowers = 0;
        g_twitterValid = false;
    }
}

static void twitter_render() {
    renderSocialCard(drawTwitterIcon, g_twitterFollowers, g_twitterValid, wt_color(255, 255, 255));
}

// ============== INSTAGRAM CARD ==============

static void insta_setup() {
    g_instaLastFetch = 0;
    g_instaFollowers = 0;
    g_instaValid = false;
}

static void insta_update(uint32_t now, uint32_t dt) {
    Settings& cfg = settings_get();
    
    // Skip if not configured
    if (cfg.instaUser[0] == '\0') {
        g_instaValid = false;
        return;
    }
    
    uint32_t updateMs = (uint32_t)cfg.socialUpdateMins * 60000UL;
    if (WiFi.status() == WL_CONNECTED && (now - g_instaLastFetch > updateMs || g_instaLastFetch == 0)) {
        g_instaLastFetch = now;
        
        // Note: Instagram API requires Facebook business integration
        Serial.println("[Instagram] API requires FB integration - showing placeholder");
        g_instaFollowers = 0;
        g_instaValid = false;
    }
}

static void insta_render() {
    renderSocialCard(drawInstaIcon, g_instaFollowers, g_instaValid, wt_color(255, 0, 100));
}

// ============== TIKTOK CARD ==============

static void tiktok_setup() {
    g_tiktokLastFetch = 0;
    g_tiktokFollowers = 0;
    g_tiktokValid = false;
}

static void tiktok_update(uint32_t now, uint32_t dt) {
    Settings& cfg = settings_get();
    
    // Skip if not configured
    if (cfg.tiktokUser[0] == '\0') {
        g_tiktokValid = false;
        return;
    }
    
    uint32_t updateMs = (uint32_t)cfg.socialUpdateMins * 60000UL;
    if (WiFi.status() == WL_CONNECTED && (now - g_tiktokLastFetch > updateMs || g_tiktokLastFetch == 0)) {
        g_tiktokLastFetch = now;
        
        // Note: TikTok API requires OAuth
        Serial.println("[TikTok] API requires OAuth - showing placeholder");
        g_tiktokFollowers = 0;
        g_tiktokValid = false;
    }
}

static void tiktok_render() {
    renderSocialCard(drawTikTokIcon, g_tiktokFollowers, g_tiktokValid, wt_color(255, 255, 255));
}
