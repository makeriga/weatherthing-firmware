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

// Card order: Weather, Clock, BTC, Stock, Network, Audio, Sparkle, Aurora, Games, MQTT
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
    {rss_setup, rss_update, rss_render}                // 10 (RSS)
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
    "RSS"        // 10
};

// Which cards are "musical" (show note icon) - VU (5), Sparkle (6), Aurora (7)
static const bool g_cardMusical[] = {
    false, false, false, false, false, true, true, true, false, false, false
};

// Title animation state
static bool g_showingTitle = false;
static uint32_t g_titleStartTime = 0;
static const uint32_t TITLE_DURATION_MS = 1500;  // Show title for 1.5 seconds
static int16_t g_titleScrollX = 0;

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
static bool g_soundReactive = true;
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
            // Animated morphing cloud
            float pulse = 0.9f + 0.1f * sinf(now * 0.0015f);
            uint8_t br = (uint8_t)(180 * pulse);
            uint32_t cloudCol = wt_color(br, br, (uint8_t)(br + 20));
            uint32_t darkCol = wt_color(br - 30, br - 30, br - 10);
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
            
            // Rain drops
            uint8_t dropCount = (type == WEATHER_DRIZZLE) ? 2 : (type == WEATHER_RAIN) ? 4 : 6;
            uint8_t fallSpeed = (type == WEATHER_HEAVY_RAIN) ? 2 : (type == WEATHER_DRIZZLE) ? 6 : 4;
            
            for (uint8_t d = 0; d < dropCount; ++d) {
                int8_t dropX = x + 1 + ((d * 7 + 3) % 6);
                int8_t dropY = y + 3 - ((frame / fallSpeed + d * 5) % 4);
                
                uint32_t dropCol = wt_color(80, 140, 255);
                if (dropY >= y && dropY <= y + 3) {
                    wt_display_set_pixel_xy(dropX, dropY, dropCol);
                    if (type == WEATHER_HEAVY_RAIN && dropY < y + 3) {
                        wt_display_set_pixel_xy(dropX, dropY + 1, wt_color(40, 80, 180));
                    }
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
            
            // Drifting snowflakes
            for (uint8_t f = 0; f < 5; ++f) {
                float drift = sinf(now * 0.0015f + f * 1.2f) * 2.0f;
                int8_t flakeX = x + 1 + (f * 5 / 4) % 5 + (int8_t)drift;
                int8_t flakeY = y + 3 - ((frame / 8 + f * 3) % 4);
                
                if (flakeX >= 0 && flakeX < WT_MATRIX_WIDTH && flakeY >= y && flakeY <= y + 3) {
                    wt_display_set_pixel_xy(flakeX, flakeY, wt_color(255, 255, 255));
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

// Render the title animation - short and centered
static void renderTitle(uint32_t now) {
    wt_display_clear();
    wt_timeline_clear();
    
    const char* name = g_cardNames[g_currentCard];
    bool musical = g_cardMusical[g_currentCard];
    
    // Calculate text width
    uint8_t len = strlen(name);
    uint8_t textW = len * 4;  // 3 pixels + 1 space per char
    if (musical) textW += 5;  // Note icon + space
    
    // Animation progress (0.0 to 1.0)
    float progress = (float)(now - g_titleStartTime) / TITLE_DURATION_MS;
    if (progress > 1.0f) progress = 1.0f;
    
    // Fade in, hold centered, fade out (no scrolling)
    int16_t x = (WT_MATRIX_WIDTH - textW) / 2;
    if (x < 0) x = 0;  // If too wide, start from left
    
    // Calculate brightness based on progress
    uint8_t bright = 255;
    if (progress < 0.15f) {
        bright = (uint8_t)(255 * progress / 0.15f);
    } else if (progress > 0.85f) {
        bright = (uint8_t)(255 * (1.0f - progress) / 0.15f);
    }
    
    // Color - use card-specific colors (match new card order)
    uint32_t color;
    switch (g_currentCard) {
        case CARD_WEATHER: color = wt_color(100, 180, 255); break;  // Sky blue
        case CARD_CLOCK:   color = wt_color(255, 255, 150); break;  // Warm white
        case CARD_BTC:     color = wt_color(255, 180, 50); break;   // Bitcoin orange
        case CARD_STOCK:   color = wt_color(100, 255, 100); break;  // Money green
        case CARD_NETWORK: color = wt_color(100, 200, 255); break;  // Cyan
        case CARD_VU:      color = wt_color(255, 50, 150); break;   // Pink
        case CARD_SPARKLE: color = wt_color(255, 255, 255); break;  // White
        case CARD_AURORA:  color = wt_color(100, 255, 150); break;  // Green
        case CARD_GAMES:   color = wt_color(255, 100, 255); break;  // Magenta
        case CARD_MQTT:    color = wt_color(50, 200, 255); break;   // HA Blue
        default: color = wt_color(200, 200, 200); break;
    }
    
    // Apply brightness
    uint8_t r = ((color >> 16) & 0xFF) * bright / 255;
    uint8_t g = ((color >> 8) & 0xFF) * bright / 255;
    uint8_t b = (color & 0xFF) * bright / 255;
    color = wt_color(r, g, b);
    
    // Draw music note if musical
    int16_t textX = x;
    if (musical) {
        drawNoteIcon(x, 1, color);
        textX += 5;
    }
    
    // Draw each character
    for (uint8_t i = 0; i < len; ++i) {
        drawChar3x5(textX + i * 4, 1, name[i], color);
    }
    
    // Simple timeline - solid color matching card
    for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i) {
        wt_timeline_set_pixel(i, wt_color(r/3, g/3, b/3));
    }
}

// Preset counts
static const uint8_t WEATHER_PRESET_COUNT = 12;  // 12 weather display styles
static const uint8_t CLOCK_PRESET_COUNT = 14;    // 14 clock watchfaces
static const uint8_t VU_PRESET_COUNT = 27;       // 27 visualizers

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
    g_showingTitle = true;
    g_titleStartTime = now;
    g_lastAutoCycle = now; // Reset auto cycle timer on manual switch
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
static const uint32_t SILENCE_THRESHOLD_MS = 500;  // 500ms of low signal = silence

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
    
    // Apply mic gain (1-10 -> 0.3x to 3x multiplier)
    float gainMult = 0.3f + (cfg.micGain - 1) * 0.3f;
    amp = (uint16_t)(amp * gainMult);
    
    // Dynamic noise floor based on vuNoiseGate setting (0-255 -> 100-600)
    uint16_t noiseFloor = 100 + cfg.vuNoiseGate * 2;
    
    // Adaptive gain control - slowly track peak level
    if (amp > g_agcPeak) {
        g_agcPeak = (g_agcPeak * 7 + amp) / 8;  // Fast rise
    } else {
        g_agcPeak = (g_agcPeak * 127 + 500) / 128;  // Very slow decay toward baseline
    }
    if (g_agcPeak < 500) g_agcPeak = 500;
    if (g_agcPeak > 4000) g_agcPeak = 4000;
    
    // Silence detection - if signal is low for a while, suppress output
    if (amp < noiseFloor) {
        if (g_silenceStartMs == 0) {
            g_silenceStartMs = now;
        }
        if (now - g_silenceStartMs > SILENCE_THRESHOLD_MS) {
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
    
    // Auto cycle
    if (cfg.cycleEnabled && cfg.cycleDuration > 0 && g_currentCard != CARD_GAMES) {
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

    // Background tasks (fetch tickers)
    if (now % 1000 == 0) { // Check every second
        ticker_update(now, 0);
        stock_update(now, 0);
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

// Weather Preset 9: Pixel Art Scene
static void weather_render_pixel_art() {
    WeatherData current = weather_get_current();
    uint32_t now = millis();
    
    // Check if night (between 9pm and 6am) - use clock if available
    int hour = 12;
    if (g_clockTimeValid) {
        time_t rawTime = time(nullptr);
        struct tm* ti = localtime(&rawTime);
        hour = ti->tm_hour;
    }
    bool isNight = (hour >= 21 || hour < 6);
    
    // Sky based on time and weather
    uint8_t skyR, skyG, skyB;
    if (isNight) {
        skyR = 10; skyG = 10; skyB = 40; // Dark night sky
    } else if (current.type == WEATHER_SUNNY) {
        skyR = 80; skyG = 150; skyB = 255; // Bright blue
    } else if (current.type == WEATHER_CLOUDY || current.type == WEATHER_PARTLY_CLOUDY) {
        skyR = 100; skyG = 100; skyB = 110; // Overcast
    } else {
        skyR = 60; skyG = 80; skyB = 120; // Default
    }
    
    // Draw sky (upper half)
    for (int y = 3; y < 7; ++y) {
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            wt_display_set_pixel_xy(x, y, wt_color(skyR, skyG, skyB));
        }
    }
    
    // Ground (green grass or snow)
    uint32_t groundCol = (current.temp < 0) ? wt_color(200, 200, 220) : wt_color(40, 100, 40);
    uint32_t groundCol2 = (current.temp < 0) ? wt_color(180, 180, 200) : wt_color(30, 80, 30);
    for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
        wt_display_set_pixel_xy(x, 0, groundCol2);
        wt_display_set_pixel_xy(x, 1, groundCol);
    }
    
    // House (centered left area)
    uint32_t wallCol = wt_color(180, 120, 60);
    uint32_t roofCol = wt_color(150, 60, 30);
    uint32_t windowCol = isNight ? wt_color(255, 220, 100) : wt_color(150, 200, 255);
    // Walls
    for (int x = 2; x <= 6; ++x) {
        wt_display_set_pixel_xy(x, 2, wallCol);
        wt_display_set_pixel_xy(x, 3, wallCol);
    }
    // Roof (triangle)
    wt_display_set_pixel_xy(3, 4, roofCol);
    wt_display_set_pixel_xy(4, 4, roofCol);
    wt_display_set_pixel_xy(5, 4, roofCol);
    wt_display_set_pixel_xy(4, 5, roofCol);
    // Window
    wt_display_set_pixel_xy(4, 3, windowCol);
    
    // Sun or Moon
    if (isNight) {
        // Moon (top right)
        wt_display_set_pixel_xy(16, 5, wt_color(220, 220, 180));
        wt_display_set_pixel_xy(17, 5, wt_color(250, 250, 200));
        wt_display_set_pixel_xy(17, 6, wt_color(220, 220, 180));
        // Stars
        if ((now / 300) % 2) wt_display_set_pixel_xy(12, 6, wt_color(100, 100, 120));
        if ((now / 400) % 2) wt_display_set_pixel_xy(14, 5, wt_color(80, 80, 100));
    } else if (current.type == WEATHER_SUNNY || current.type == WEATHER_PARTLY_CLOUDY) {
        // Sun (top right)
        uint32_t sunCol = wt_color(255, 220, 50);
        wt_display_set_pixel_xy(16, 5, sunCol);
        wt_display_set_pixel_xy(17, 5, sunCol);
        wt_display_set_pixel_xy(16, 6, sunCol);
        wt_display_set_pixel_xy(17, 6, sunCol);
    }
    
    // Rain overlay
    if (current.type >= WEATHER_RAIN && current.type <= WEATHER_HEAVY_RAIN) {
        for (int i = 0; i < 5; ++i) {
            int rx = (now / 80 + i * 4) % WT_MATRIX_WIDTH;
            int ry = 6 - (now / 60 + i * 2) % 5;
            wt_display_set_pixel_xy(rx, ry, wt_color(100, 150, 255));
        }
    }
    
    // Temperature display (right side) - high contrast white on dark area
    int8_t temp = current.temp;
    bool neg = temp < 0;
    if (neg) temp = -temp;
    if (temp > 99) temp = 99;
    
    // Draw dark background behind temp for contrast
    for (int tx = 9; tx < WT_MATRIX_WIDTH; ++tx) {
        for (int ty = 0; ty < 6; ++ty) {
            wt_display_set_pixel_xy(tx, ty, wt_color(20, 20, 30));
        }
    }
    
    // Bright white digits for visibility
    uint32_t tCol = wt_color(255, 255, 255);
    uint8_t x = 10;
    if (neg) {
        wt_display_set_pixel_xy(x, 2, tCol);
        wt_display_set_pixel_xy(x+1, 2, tCol);
        x += 3;
    }
    if (temp >= 10) {
        drawChar3x5(x, 0, '0' + temp/10, tCol);
        x += 4;
    }
    drawChar3x5(x, 0, '0' + temp%10, tCol);
}

// Weather Preset 10: Retro LCD style  
static void weather_render_lcd() {
    WeatherData current = weather_get_current();
    // Classic LCD green-on-dark style
    uint32_t lcdBg = wt_color(20, 35, 20);     // Dark LCD background
    uint32_t lcdDigit = wt_color(80, 200, 80); // Bright green digits
    uint32_t lcdDim = wt_color(30, 60, 30);    // Dim segments
    
    int8_t temp = current.temp;
    bool neg = temp < 0;
    if (neg) temp = -temp;
    
    // Fill background
    for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
        for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
            wt_display_set_pixel_xy(x, y, lcdBg);
        }
    }
    
    // Draw all 7-segment outlines dim (gives LCD look)
    // We show temp in center
    uint8_t startX = 3;
    if (neg) {
        // Minus sign
        wt_display_set_pixel_xy(startX, 3, lcdDigit);
        wt_display_set_pixel_xy(startX + 1, 3, lcdDigit);
        startX += 4;
    }
    
    // Tens digit
    if (temp >= 10 || neg) {
        drawDigit(startX, 1, temp / 10, lcdDigit);
        startX += 5;
    }
    // Ones digit
    drawDigit(startX, 1, temp % 10, lcdDigit);
    
    // Degree symbol (small square)
    uint8_t dx = startX + 4;
    wt_display_set_pixel_xy(dx, 5, lcdDigit);
    wt_display_set_pixel_xy(dx + 1, 5, lcdDigit);
    wt_display_set_pixel_xy(dx, 6, lcdDigit);
    wt_display_set_pixel_xy(dx + 1, 6, lcdDigit);
}

// Weather Preset 11: Gradient Mood
static void weather_render_mood() {
    WeatherData current = weather_get_current();
    uint32_t now = millis();
    float phase = now * 0.0008f; // Slower
    
    // Full-screen flowing gradient based on temp
    // Hot = red/orange, Cold = blue/purple
    int8_t t = current.temp;
    float tempNorm = (t + 20.0f) / 50.0f; // -20 to +30 -> 0 to 1
    if (tempNorm < 0) tempNorm = 0;
    if (tempNorm > 1) tempNorm = 1;
    
    for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
        for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
            float wave = sinf(x * 0.25f + y * 0.4f + phase) * 0.5f + 0.5f;
            // Blend between cold (blue/purple) and hot (red/orange)
            uint8_t r = (uint8_t)(tempNorm * 200 * wave + 30);
            uint8_t g = (uint8_t)(50 * wave);
            uint8_t b = (uint8_t)((1.0f - tempNorm) * 200 * wave + 30);
            wt_display_set_pixel_xy(x, y, wt_color(r, g, b));
        }
    }
    
    // Overlay temp number - centered, starting at y=0 so not cut off
    uint32_t white = wt_color(255, 255, 255);
    int8_t temp = current.temp;
    uint8_t startX = 6;
    if (temp < 0) {
        wt_display_set_pixel_xy(startX, 2, white);
        wt_display_set_pixel_xy(startX + 1, 2, white);
        startX += 3;
        temp = -temp;
    }
    drawDigit(startX, 0, temp / 10, white);
    drawDigit(startX + 4, 0, temp % 10, white);
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

    // Temp overlay (bottom right, small)
    WeatherData current = weather_get_current();
    if (current.valid) {
        uint32_t tCol = wt_color(255, 255, 255);
        int8_t t = current.temp;
        bool neg = t < 0;
        if (neg) t = -t;
        if (t > 99) t = 99;
        
        uint8_t x = WT_MATRIX_WIDTH - 7;
        if (neg) { wt_display_set_pixel_xy(x-2, 3, tCol); }
        if (t >= 10) { drawChar3x5(x-4, 1, '0' + t/10, tCol); }
        drawChar3x5(x, 1, '0' + t%10, tCol);
    }
}



// Weather Preset 12: Typewriter - letters appear one by one spelling temp
static void weather_render_typewriter() {
    static uint8_t charIndex = 0;
    static uint32_t lastType = 0;
    WeatherData current = weather_get_current();
    uint32_t now = millis();
    
    // Build the string to type: "-12°" or "25°"
    char tempStr[8];
    int8_t t = current.temp;
    int len = 0;
    if (t < 0) { tempStr[len++] = '-'; t = -t; }
    if (t >= 10) tempStr[len++] = '0' + (t / 10);
    tempStr[len++] = '0' + (t % 10);
    tempStr[len++] = '*'; // degree symbol placeholder
    tempStr[len] = '\0';
    
    // Type one char every 400ms
    if (now - lastType > 400) {
        lastType = now;
        charIndex++;
        if (charIndex > len + 3) charIndex = 0; // Reset with pause
    }
    
    // Amber monochrome CRT look
    uint32_t amber = wt_color(255, 180, 0);
    uint32_t dimAmber = wt_color(60, 40, 0);
    
    // Draw typed characters
    uint8_t x = 2;
    for (int i = 0; i < (int)charIndex && i < len; ++i) {
        if (tempStr[i] == '-') {
            wt_display_set_pixel_xy(x, 3, amber);
            wt_display_set_pixel_xy(x+1, 3, amber);
            x += 3;
        } else if (tempStr[i] == '*') {
            // Degree
            wt_display_set_pixel_xy(x, 5, amber);
            wt_display_set_pixel_xy(x+1, 5, amber);
            wt_display_set_pixel_xy(x, 6, amber);
            wt_display_set_pixel_xy(x+1, 6, amber);
        } else {
            drawChar3x5(x, 1, tempStr[i], amber);
            x += 4;
        }
    }
    
    // Blinking cursor at end
    if ((now / 300) % 2 && charIndex <= len) {
        for (int cy = 0; cy < 7; ++cy)
            wt_display_set_pixel_xy(x, cy, amber);
    }
}

// Weather Preset 13: Waves - temp floats on animated sine wave ocean
static void weather_render_waves() {
    WeatherData current = weather_get_current();
    uint32_t now = millis();
    float phase = now * 0.003f;
    
    // Draw animated wave layers
    for (int layer = 0; layer < 3; ++layer) {
        uint8_t blue = 80 + layer * 50;
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            float wave = sinf(x * 0.4f + phase + layer * 1.0f);
            int y = 2 + layer + (int)(wave * 1.2f);
            if (y >= 0 && y < WT_MATRIX_HEIGHT) {
                wt_display_set_pixel_xy(x, y, wt_color(0, 40, blue));
            }
        }
    }
    
    // Temperature floats on waves (bobbing motion)
    int8_t t = current.temp;
    bool neg = t < 0;
    if (neg) t = -t;
    
    float bob = sinf(phase * 1.5f) * 0.8f;
    int baseY = 4 + (int)bob;
    if (baseY < 0) baseY = 0;
    if (baseY > 5) baseY = 5;
    
    uint32_t white = wt_color(255, 255, 255);
    uint8_t x = 6;
    if (neg) { wt_display_set_pixel_xy(x, baseY + 1, white); wt_display_set_pixel_xy(x+1, baseY + 1, white); x += 3; }
    if (t >= 10) { drawChar3x5(x, baseY, '0' + t/10, white); x += 4; }
    drawChar3x5(x, baseY, '0' + t%10, white);
}

// Weather Preset 14: Split - screen split diagonally, temp on each half different style
static void weather_render_split() {
    WeatherData current = weather_get_current();
    uint32_t now = millis();
    
    int8_t t = current.temp;
    bool neg = t < 0;
    if (neg) t = -t;
    
    // Diagonal split - top-left is dark with bright text, bottom-right is bright with dark text
    for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
        for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
            bool topHalf = (x + y * 3) < 30;
            if (topHalf) {
                wt_display_set_pixel_xy(x, y, wt_color(10, 10, 30)); // Dark blue
            } else {
                wt_display_set_pixel_xy(x, y, wt_color(255, 200, 100)); // Warm cream
            }
        }
    }
    
    // Draw temp twice - once in each style
    // Top half: cyan outline style
    uint32_t cyan = wt_color(0, 255, 255);
    uint8_t x1 = 1;
    if (neg) { wt_display_set_pixel_xy(x1, 3, cyan); x1 += 2; }
    if (t >= 10) { drawChar3x5(x1, 1, '0' + t/10, cyan); x1 += 4; }
    drawChar3x5(x1, 1, '0' + t%10, cyan);
    
    // Bottom half: dark text
    uint32_t dark = wt_color(60, 30, 0);
    uint8_t x2 = 10;
    if (t >= 10) { drawChar3x5(x2, 1, '0' + t/10, dark); x2 += 4; }
    drawChar3x5(x2, 1, '0' + t%10, dark);
    
    // Animated diagonal line
    int offset = (now / 100) % 10;
    for (int i = 0; i < 20; ++i) {
        int x = (i + offset) % 20;
        int y = 6 - (x * 7 / 20);
        if (y >= 0 && y < 7) wt_display_set_pixel_xy(x, y, wt_color(255, 255, 255));
    }
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

// Weather Preset 16: Vertical Stack - temp digits stacked vertically, scrolling
static void weather_render_stack() {
    static float scrollY = 0;
    static uint32_t lastScroll = 0;
    WeatherData current = weather_get_current();
    uint32_t now = millis();
    
    // Slow vertical scroll
    if (now - lastScroll > 50) {
        lastScroll = now;
        scrollY += 0.15f;
        if (scrollY > 14) scrollY = 0;
    }
    
    int8_t t = current.temp;
    bool neg = t < 0;
    if (neg) t = -t;
    
    // Gradient background (vertical)
    for (int y = 0; y < WT_MATRIX_HEIGHT; ++y) {
        uint8_t shade = 20 + y * 8;
        for (int x = 0; x < WT_MATRIX_WIDTH; ++x) {
            wt_display_set_pixel_xy(x, y, wt_color(shade/3, shade/4, shade));
        }
    }
    
    // Draw vertically stacked: tens on top, ones below, scrolling
    uint32_t col = tempToColor(current.temp);
    
    // Tens digit
    int y1 = (int)scrollY - 7;
    if (y1 > -6 && y1 < 7) {
        if (t >= 10 || neg) {
            uint8_t d = t >= 10 ? t/10 : 0;
            // Draw centered horizontally, at y1
            for (int dy = 0; dy < 5 && y1 + dy >= 0 && y1 + dy < 7; ++dy) {
                drawChar3x5(8, y1, '0' + d, col);
            }
        }
    }
    
    // Ones digit
    int y2 = (int)scrollY;
    if (y2 > -6 && y2 < 7) {
        drawChar3x5(8, y2, '0' + t%10, col);
    }
    
    // Minus sign if negative (at very top)
    if (neg) {
        int ym = (int)scrollY - 14;
        if (ym >= 0 && ym < 7) {
            wt_display_set_pixel_xy(9, ym, col);
            wt_display_set_pixel_xy(10, ym, col);
        }
    }
    
    // Side bars showing temp (like a thermometer)
    int barHeight = (t + 10) * 7 / 50;
    if (barHeight < 1) barHeight = 1;
    if (barHeight > 7) barHeight = 7;
    for (int y = 0; y < barHeight; ++y) {
        wt_display_set_pixel_xy(0, y, col);
        wt_display_set_pixel_xy(19, y, col);
    }
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

    // 17 weather display presets
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
        case 16: weather_render_stack(); break;        // Vertical Stack
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

static char g_rssTitle[512] = "Loading RSS...";
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
    
    http.setTimeout(15000); // 15 second timeout for SSL handshake
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.addHeader("User-Agent", "WeatherThing/1.0");
    http.addHeader("Accept", "application/rss+xml, application/xml, text/xml");
    
    int httpCode = http.GET();
    Serial.printf("[RSS] HTTP code: %d\n", httpCode);
    
    if (httpCode != HTTP_CODE_OK) {
        if (httpCode < 0) {
            snprintf(g_rssTitle, sizeof(g_rssTitle), "RSS connection failed");
        } else if (httpCode == 404) {
            snprintf(g_rssTitle, sizeof(g_rssTitle), "RSS feed not found 404");
        } else if (httpCode >= 500) {
            snprintf(g_rssTitle, sizeof(g_rssTitle), "RSS server error %d", httpCode);
        } else {
            snprintf(g_rssTitle, sizeof(g_rssTitle), "RSS error %d", httpCode);
        }
        g_rssValid = false;
        http.end();
        if (secureClient) delete secureClient;
        return;
    }
    
    // Limit response size to prevent memory issues
    int contentLen = http.getSize();
    Serial.printf("[RSS] Content length: %d\n", contentLen);
    if (contentLen > 64000) {
        Serial.println("[RSS] Feed too large, reading partial");
    }
    
    // Read response in chunks, look for first <item><title>
    String response = "";
    WiFiClient *stream = http.getStreamPtr();
    uint32_t startTime = millis();
    int bytesRead = 0;
    const int maxBytes = 32000; // Limit memory usage
    
    while (stream->available() && bytesRead < maxBytes && (millis() - startTime) < 8000) {
        int toRead = min(512, stream->available());
        char buf[513];
        int len = stream->readBytes(buf, toRead);
        buf[len] = 0;
        response += buf;
        bytesRead += len;
        
        // Check if we have enough to parse
        if (response.indexOf("</title>") > 0 && response.indexOf("<item") > 0) {
            break; // Got what we need
        }
        yield(); // Let other tasks run
    }
    
    http.end();
    Serial.printf("[RSS] Read %d bytes\n", bytesRead);
    
    // Find first item's title
    int itemPos = response.indexOf("<item");
    if (itemPos < 0) {
        // Try <entry> for Atom feeds
        itemPos = response.indexOf("<entry");
    }
    
    if (itemPos < 0) {
        snprintf(g_rssTitle, sizeof(g_rssTitle), "No items in RSS feed");
        g_rssValid = false;
        if (secureClient) delete secureClient;
        return;
    }
    
    int titleStart = response.indexOf("<title", itemPos);
    if (titleStart < 0) {
        snprintf(g_rssTitle, sizeof(g_rssTitle), "RSS parse error - no title");
        g_rssValid = false;
        if (secureClient) delete secureClient;
        return;
    }
    
    // Skip past the <title> or <title ...> tag
    int titleContentStart = response.indexOf(">", titleStart) + 1;
    int titleEnd = response.indexOf("</title>", titleContentStart);
    
    if (titleContentStart <= 0 || titleEnd < 0) {
        snprintf(g_rssTitle, sizeof(g_rssTitle), "RSS parse error");
        g_rssValid = false;
        if (secureClient) delete secureClient;
        return;
    }
    
    String title = response.substring(titleContentStart, titleEnd);
    
    // Handle CDATA sections: <![CDATA[actual content]]>
    if (title.startsWith("<![CDATA[")) {
        title = title.substring(9); // Remove <![CDATA[
        int cdataEnd = title.indexOf("]]>");
        if (cdataEnd > 0) {
            title = title.substring(0, cdataEnd);
        }
    }
    
    // Decode HTML entities
    title.replace("&quot;", "\"");
    title.replace("&apos;", "'");
    title.replace("&lt;", "<");
    title.replace("&gt;", ">");
    title.replace("&amp;", "&");
    title.replace("&#39;", "'");
    title.replace("&#x27;", "'");
    title.replace("&nbsp;", " ");
    title.trim();
    
    Serial.printf("[RSS] Title: %s\n", title.c_str());
    
    // Convert to display format with UTF-8 handling
    int outLen = 0;
    for(size_t i = 0; i < title.length() && outLen < (int)sizeof(g_rssTitle) - 1; i++) {
        uint8_t c = title[i];
        
        // Handle UTF-8 multi-byte sequences for Latvian chars
        if (c == 0xC4 || c == 0xC5) {
            if (i + 1 < title.length()) {
                uint8_t c2 = title[++i];
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
                // Skip unknown UTF-8 sequences
            }
        } else if ((c & 0xE0) == 0xC0) {
            // Skip other 2-byte UTF-8 sequences
            if (i + 1 < title.length()) i++;
        } else if ((c & 0xF0) == 0xE0) {
            // Skip 3-byte UTF-8 sequences (emoji, etc)
            if (i + 2 < title.length()) i += 2;
        } else if ((c & 0xF8) == 0xF0) {
            // Skip 4-byte UTF-8 sequences
            if (i + 3 < title.length()) i += 3;
        } else if (c < 128) {
            // ASCII character
            g_rssTitle[outLen++] = c;
        }
    }
    g_rssTitle[outLen] = 0;
    
    if (outLen == 0) {
        snprintf(g_rssTitle, sizeof(g_rssTitle), "RSS title empty");
        g_rssValid = false;
    } else {
        g_rssValid = true;
        Serial.printf("[RSS] Display text: %s\n", g_rssTitle);
    }
    
    // Clean up secure client
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
