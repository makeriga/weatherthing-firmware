#include <Arduino.h>
#include <time.h>
#include <WiFi.h>
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
    {mqttcard_setup, mqttcard_update, mqttcard_render} // 9 (MQTT/Home Assistant)
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
    "MQTT"       // 9
};

// Which cards are "musical" (show note icon) - VU (5), Sparkle (6), Aurora (7)
static const bool g_cardMusical[] = {
    false, false, false, false, false, true, true, true, false, false
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
    uint32_t speed = 40 + g_audioLevel / 4;
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

    // Draw WiFi icon (left side) - animated signal strength
    uint8_t wx = 0, wy = 0;
    uint32_t wifiMain = hasIp ? wt_color(0, 220, 255) : wt_color(255, 100, 50);
    uint32_t wifiDim = hasIp ? wt_color(0, 100, 150) : wt_color(150, 50, 0);
    
    // Animated arcs (bottom to top)
    uint8_t arcPhase = (now / 300) % 4;
    
    // Bottom dot (always on)
    wt_display_set_pixel_xy(wx + 3, wy + 0, wifiMain);
    wt_display_set_pixel_xy(wx + 3, wy + 1, wifiMain);
    
    // First arc
    uint32_t arc1 = (arcPhase >= 1) ? wifiMain : wifiDim;
    wt_display_set_pixel_xy(wx + 2, wy + 2, arc1);
    wt_display_set_pixel_xy(wx + 4, wy + 2, arc1);
    
    // Second arc
    uint32_t arc2 = (arcPhase >= 2) ? wifiMain : wifiDim;
    wt_display_set_pixel_xy(wx + 1, wy + 3, arc2);
    wt_display_set_pixel_xy(wx + 2, wy + 4, arc2);
    wt_display_set_pixel_xy(wx + 4, wy + 4, arc2);
    wt_display_set_pixel_xy(wx + 5, wy + 3, arc2);
    
    // Third arc (outer)
    uint32_t arc3 = (arcPhase >= 3) ? wifiMain : wifiDim;
    wt_display_set_pixel_xy(wx + 0, wy + 4, arc3);
    wt_display_set_pixel_xy(wx + 1, wy + 5, arc3);
    wt_display_set_pixel_xy(wx + 2, wy + 6, arc3);
    wt_display_set_pixel_xy(wx + 4, wy + 6, arc3);
    wt_display_set_pixel_xy(wx + 5, wy + 5, arc3);
    wt_display_set_pixel_xy(wx + 6, wy + 4, arc3);

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

static void drawWeatherIcon(int8_t x, int8_t y, uint8_t type, uint8_t frame)
{
    SpriteType spriteType;
    switch(type) {
        case WEATHER_SUNNY:        spriteType = SPRITE_ICON_SUN; break;
        case WEATHER_CLEAR_NIGHT:  spriteType = SPRITE_ICON_SUN; break; 
        case WEATHER_PARTLY_CLOUDY:spriteType = SPRITE_ICON_CLOUD; break;
        case WEATHER_CLOUDY:       spriteType = SPRITE_ICON_CLOUD; break;
        case WEATHER_FOG:          spriteType = SPRITE_ICON_CLOUD; break; // No fog icon yet
        case WEATHER_DRIZZLE:      spriteType = SPRITE_ICON_RAIN; break;
        case WEATHER_RAIN:         spriteType = SPRITE_ICON_RAIN; break;
        case WEATHER_HEAVY_RAIN:   spriteType = SPRITE_ICON_RAIN; break;
        case WEATHER_STORM:        spriteType = SPRITE_ICON_STORM; break;
        case WEATHER_SNOW:         spriteType = SPRITE_ICON_SNOW; break;
        case WEATHER_SLEET:        spriteType = SPRITE_ICON_SNOW; break;
        case WEATHER_WIND:         spriteType = SPRITE_ICON_WIND; break;
        default:                   spriteType = SPRITE_ICON_CLOUD; break;
    }

    const SpriteData* s = sprites_get(spriteType);
    
    if (!s) return;
    
    // Animation offsets
    int8_t sunPulse = 0;
    int8_t rainShift = 0;
    
    if (type == WEATHER_SUNNY || type == WEATHER_CLEAR_NIGHT) {
        sunPulse = (frame / 10) % 2; 
    } else if (type == WEATHER_RAIN || type == WEATHER_HEAVY_RAIN || type == WEATHER_STORM) {
        rainShift = (frame / 5) % 3; 
    }
    
    for (uint8_t row = 0; row < s->height; ++row)
    {
        // Rain animation: Rotate the bottom rows
        int8_t srcRow = row;
        if (rainShift > 0 && row >= 4) { 
             srcRow = 4 + ((row - 4 - rainShift + 3) % 3);
        }
        
        int8_t dy = y + row;
        if (dy < 0 || dy >= WT_MATRIX_HEIGHT) continue;
        
        uint32_t bits = s->rows[srcRow];
        for (uint8_t col = 0; col < s->width; ++col)
        {
            // Sun animation: Skip corners on pulse
            if (sunPulse && (type == WEATHER_SUNNY || type == WEATHER_CLEAR_NIGHT) && (row == 0 || row == 6) && (col == 0 || col == s->width-1)) {
                continue;
            }
            
            int8_t dx = x + col;
            if (dx >= 0 && dx < WT_MATRIX_WIDTH)
            {
                if (bits & (1 << (s->width - 1 - col)))
                {
                    // Multi-color logic for Cloud + Rain/Snow
                    uint32_t pixelCol = weatherColor(type);
                    
                    // If this is a rainy/stormy type, and we are drawing the top part (cloud), make it Grey
                    // Cloud usually in rows 4,5,6 (bottom index in file, top visually if Y=0 is bottom??)
                    // Wait, I established earlier:
                    // Array Index 0,1,2 = Drops (Bottom visually if dy = y+row and y=0)
                    // Array Index 4,5,6 = Cloud (Top visually)
                    // So if row >= 4, it's cloud.
                    
                    if (type == WEATHER_RAIN || type == WEATHER_HEAVY_RAIN || type == WEATHER_STORM || type == WEATHER_DRIZZLE) {
                        if (srcRow >= 4) pixelCol = wt_color(100, 100, 120); // Grey Cloud
                        else pixelCol = wt_color(0, 0, 255); // Blue Rain
                    }
                    // Storm lightning?
                    if (type == WEATHER_STORM && frame % 20 < 2) {
                        pixelCol = wt_color(255, 255, 255); // Flash
                    }
                    
                    wt_display_set_pixel_xy(dx, dy, pixelCol);
                }
            }
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

// Simple 3x5 font for title text (uppercase letters only)
static const uint8_t FONT_3X5[][5] = {
    {0b111, 0b101, 0b111, 0b101, 0b101}, // A
    {0b110, 0b101, 0b110, 0b101, 0b110}, // B
    {0b111, 0b100, 0b100, 0b100, 0b111}, // C
    {0b110, 0b101, 0b101, 0b101, 0b110}, // D
    {0b111, 0b100, 0b110, 0b100, 0b111}, // E
    {0b111, 0b100, 0b110, 0b100, 0b100}, // F
    {0b111, 0b100, 0b101, 0b101, 0b111}, // G
    {0b101, 0b101, 0b111, 0b101, 0b101}, // H
    {0b111, 0b010, 0b010, 0b010, 0b111}, // I
    {0b011, 0b001, 0b001, 0b101, 0b111}, // J
    {0b101, 0b110, 0b100, 0b110, 0b101}, // K
    {0b100, 0b100, 0b100, 0b100, 0b111}, // L
    {0b101, 0b111, 0b111, 0b101, 0b101}, // M
    {0b101, 0b111, 0b111, 0b111, 0b101}, // N
    {0b111, 0b101, 0b101, 0b101, 0b111}, // O
    {0b111, 0b101, 0b111, 0b100, 0b100}, // P
    {0b111, 0b101, 0b101, 0b111, 0b011}, // Q
    {0b111, 0b101, 0b110, 0b101, 0b101}, // R
    {0b111, 0b100, 0b111, 0b001, 0b111}, // S
    {0b111, 0b010, 0b010, 0b010, 0b010}, // T
    {0b101, 0b101, 0b101, 0b101, 0b111}, // U
    {0b101, 0b101, 0b101, 0b101, 0b010}, // V
    {0b101, 0b101, 0b111, 0b111, 0b101}, // W
    {0b101, 0b101, 0b010, 0b101, 0b101}, // X
    {0b101, 0b101, 0b010, 0b010, 0b010}, // Y
    {0b111, 0b001, 0b010, 0b100, 0b111}, // Z
};

// Draw a single character (3x5)
static void drawChar3x5(int16_t x, uint8_t y, char c, uint32_t color) {
    int idx = -1;
    if (c >= 'A' && c <= 'Z') idx = c - 'A';
    else if (c >= 'a' && c <= 'z') idx = c - 'a';
    
    if (idx < 0 || idx > 25) return;
    
    for (uint8_t row = 0; row < 5; ++row) {
        uint8_t bits = FONT_3X5[idx][row];
        for (uint8_t col = 0; col < 3; ++col) {
            if (bits & (1 << (2 - col))) {
                int16_t px = x + col;
                if (px >= 0 && px < WT_MATRIX_WIDTH) {
                    wt_display_set_pixel_xy(px, y + 4 - row, color);
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
static const uint8_t WEATHER_PRESET_COUNT = 9;  // 9 weather display styles
static const uint8_t CLOCK_PRESET_COUNT = 7;    // 7 clock watchfaces
static const uint8_t VU_PRESET_COUNT = 17;      // 17 visualizers (12 classic + 5 playful)

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

// Start showing title animation for current card
static void startTitleAnimation(uint32_t now) {
    g_showingTitle = true;
    g_titleStartTime = now;
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
        g_currentCard = (g_currentCard + 1) % g_cardCount;
        g_cards[g_currentCard].setup();
        startTitleAnimation(now);
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
        // Move to previous card (and set preset to max)
        g_currentCard = (g_currentCard + g_cardCount - 1) % g_cardCount;
        
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

static void handleButtons(uint32_t now)
{
    static uint32_t bothHeldStart = 0;
    bool b1 = wt_button1_pressed();
    bool b2 = wt_button2_pressed();

    bool b1Edge = b1 && !g_lastBtn1;
    bool b2Edge = b2 && !g_lastBtn2;
    
    // Games card: buttons control the game
    // Use BOTH buttons pressed together to exit game
    if (g_currentCard == CARD_GAMES) {
        if (b1 && b2) {
            if (bothHeldStart == 0) bothHeldStart = now;
            if (now - bothHeldStart > 1000) {
                // Exit games after holding both for 1s
                bothHeldStart = 0;
                g_gameMode = 0;
                g_currentCard = (g_currentCard + 1) % g_cardCount;
                g_cards[g_currentCard].setup();
                startTitleAnimation(now);
            }
        } else {
            bothHeldStart = 0;
        }
        
        g_lastBtn1 = b1;
        g_lastBtn2 = b2;
        return;
    }
    
    // Button 1 = forward, Button 2 = backward (non-game cards)
    if (b1Edge) {
        cycleNext();
    }
    if (b2Edge) {
        cyclePrev();
    }

    g_lastBtn1 = b1;
    g_lastBtn2 = b2;
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

// Quadratic Bezier interpolation helper
static void bezierPoint(float t, float x0, float y0, float x1, float y1, float x2, float y2, float &ox, float &oy)
{
    float u = 1.0f - t;
    ox = u * u * x0 + 2.0f * u * t * x1 + t * t * x2;
    oy = u * u * y0 + 2.0f * u * t * y1 + t * t * y2;
}

// Boot animation: Handwritten W with smooth Bezier curves (McDonald's/Disney style)
static void renderBootAnimation(uint32_t now)
{
    wt_display_clear();
    wt_timeline_clear();

    uint32_t elapsed = now - g_bootStart;

    const uint32_t drawTime = 1400;
    const uint32_t holdTime = 600;
    const uint32_t fadeTime = 500;

    // Phase transitions
    if (g_bootPhase == 0 && elapsed > drawTime)
    {
        g_bootPhase = 1;
        g_bootStart = now;
        elapsed = 0;
    }
    else if (g_bootPhase == 1 && elapsed > holdTime)
    {
        g_bootPhase = 2;
        g_bootStart = now;
        elapsed = 0;
    }
    else if (g_bootPhase == 2 && elapsed > fadeTime)
    {
        g_bootPhase = 3;
        g_bootDone = true;
        return;
    }

    // W shape using 4 quadratic Bezier curves for smooth handwritten look
    // Curve 1: top-left down to first valley
    // Curve 2: first valley up to middle peak
    // Curve 3: middle peak down to second valley
    // Curve 4: second valley up to top-right
    
    // Control points for each curve (start, control, end)
    // x: 0-19, y: 0-6 (0=bottom, 6=top)
    static const float CURVES[4][6] = {
        // Curve 0: Start top-left, swoop down to valley 1
        {2.0f, 6.0f,   3.0f, 3.0f,   5.0f, 0.0f},
        // Curve 1: Valley 1 up to center peak  
        {5.0f, 0.0f,   7.0f, 4.0f,   9.5f, 5.0f},
        // Curve 2: Center peak down to valley 2
        {9.5f, 5.0f,  12.0f, 4.0f,  14.0f, 0.0f},
        // Curve 3: Valley 2 swoop up to top-right
        {14.0f, 0.0f, 16.0f, 3.0f,  18.0f, 6.0f}
    };

    // During draw phase, update trail
    if (g_bootPhase == 0)
    {
        float t = (float)elapsed / (float)drawTime;
        if (t > 1.0f) t = 1.0f;
        
        // Map t to curve index and local t
        float totalT = t * 4.0f;  // 4 curves
        uint8_t curveIdx = (uint8_t)totalT;
        if (curveIdx > 3) curveIdx = 3;
        float localT = totalT - curveIdx;
        if (localT > 1.0f) localT = 1.0f;
        
        float hx, hy;
        bezierPoint(localT,
            CURVES[curveIdx][0], CURVES[curveIdx][1],
            CURVES[curveIdx][2], CURVES[curveIdx][3],
            CURVES[curveIdx][4], CURVES[curveIdx][5],
            hx, hy);

        int8_t px = (int8_t)roundf(hx);
        int8_t py = (int8_t)roundf(hy);
        if (px < 0) px = 0; if (px >= WT_MATRIX_WIDTH) px = WT_MATRIX_WIDTH - 1;
        if (py < 0) py = 0; if (py >= WT_MATRIX_HEIGHT) py = WT_MATRIX_HEIGHT - 1;

        // Only add to trail if position changed
        if (g_bootTrailCount == 0 || g_bootTrailX[0] != px || g_bootTrailY[0] != py)
        {
            uint8_t limit = g_bootTrailCount;
            if (limit > BOOT_TRAIL_LEN - 1) limit = BOOT_TRAIL_LEN - 1;
            for (int8_t i = (int8_t)limit; i > 0; --i)
            {
                g_bootTrailX[i] = g_bootTrailX[i - 1];
                g_bootTrailY[i] = g_bootTrailY[i - 1];
            }
            g_bootTrailX[0] = px;
            g_bootTrailY[0] = py;
            if (g_bootTrailCount < BOOT_TRAIL_LEN) g_bootTrailCount++;
        }
    }

    // Calculate fade multiplier
    float fadeMul = 1.0f;
    if (g_bootPhase == 2)
    {
        fadeMul = 1.0f - (float)elapsed / (float)fadeTime;
        if (fadeMul < 0.0f) fadeMul = 0.0f;
    }

    // Draw the W trail - single pixel stroke with fading tail
    for (uint8_t i = 0; i < g_bootTrailCount; ++i)
    {
        float tailFade = 1.0f - (float)i / (float)g_bootTrailCount;
        if (tailFade < 0.15f) tailFade = 0.15f; // Keep tail slightly visible
        
        // Breathing during hold
        float breath = 1.0f;
        if (g_bootPhase == 1)
        {
            breath = 0.8f + 0.2f * sinf((now % 800) * 0.00785f);
        }

        float intensity = tailFade * breath * fadeMul;
        if (intensity <= 0.0f) continue;

        // Golden color gradient: bright head -> warm amber tail
        uint8_t r = (uint8_t)(255 * intensity);
        uint8_t g = (uint8_t)(180 * intensity * tailFade + 80 * intensity * (1.0f - tailFade));
        uint8_t b = (uint8_t)(50 * intensity * (1.0f - tailFade));

        int8_t px = g_bootTrailX[i];
        int8_t py = g_bootTrailY[i];
        if (px >= 0 && px < WT_MATRIX_WIDTH && py >= 0 && py < WT_MATRIX_HEIGHT)
        {
            wt_display_set_pixel_xy(px, py, wt_color(r, g, b));
        }
    }

    // Subtle timeline pulse
    uint8_t tlGlow = (uint8_t)((15 + 10 * sinf(now * 0.004f)) * fadeMul);
    for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i)
        wt_timeline_set_pixel(i, wt_color(tlGlow, tlGlow / 2, 0));
}

// WiFi notification with scrolling IP
static void renderWifiNotification(uint32_t now)
{
    wt_display_clear();
    wt_timeline_clear();
    
    uint32_t elapsed = now - g_wifiNotifyStart;
    
    // WiFi icon (simple antenna shape) at scroll position
    int16_t iconX = g_scrollX;
    
    // Draw WiFi icon (3x5)
    uint32_t wifiCol = wt_color(0, 200, 255);
    if (iconX >= -3 && iconX < WT_MATRIX_WIDTH) {
        if (iconX >= 0) wt_display_set_pixel_xy(iconX, 6, wifiCol);
        if (iconX+1 >= 0 && iconX+1 < WT_MATRIX_WIDTH) wt_display_set_pixel_xy(iconX+1, 6, wifiCol);
        if (iconX+2 >= 0 && iconX+2 < WT_MATRIX_WIDTH) wt_display_set_pixel_xy(iconX+2, 6, wifiCol);
        if (iconX+1 >= 0 && iconX+1 < WT_MATRIX_WIDTH) {
            wt_display_set_pixel_xy(iconX+1, 5, wifiCol);
            wt_display_set_pixel_xy(iconX+1, 4, wifiCol);
        }
        // Arcs
        if (iconX >= 0) wt_display_set_pixel_xy(iconX, 4, wt_color(0, 100, 150));
        if (iconX+2 >= 0 && iconX+2 < WT_MATRIX_WIDTH) wt_display_set_pixel_xy(iconX+2, 4, wt_color(0, 100, 150));
    }
    
    // Draw IP text after icon
    int16_t textX = iconX + 5;
    uint32_t textCol = wt_color(200, 200, 200);
    
    // Simple 3x5 mini font for IP address
    for (uint8_t i = 0; g_wifiIP[i] != '\0' && i < 16; ++i)
    {
        char c = g_wifiIP[i];
        int16_t cx = textX + i * 4;
        
        if (cx < -3 || cx >= WT_MATRIX_WIDTH) continue;
        
        if (c == '.') {
            if (cx >= 0 && cx < WT_MATRIX_WIDTH)
                wt_display_set_pixel_xy(cx, 1, textCol);
            continue;
        }
        
        if (c >= '0' && c <= '9') {
            uint8_t d = c - '0';
            for (uint8_t row = 0; row < DIGIT_H && row < 5; ++row) {
                uint8_t bits = sprites_get_digit_row(d, row + (DIGIT_H > 5 ? 1 : 0));
                for (uint8_t col = 0; col < DIGIT_W; ++col) {
                    if ((bits & (1 << (DIGIT_W - 1 - col))) && cx + col >= 0 && cx + col < WT_MATRIX_WIDTH)
                        wt_display_set_pixel_xy(cx + col, DIGIT_H - 1 - row - 1, textCol);
                }
            }
        }
    }
    
    // Scroll animation
    g_scrollX -= 1;
    
    // Calculate total width of message
    int16_t totalWidth = 5 + strlen(g_wifiIP) * 4;
    
    // Done scrolling?
    if (g_scrollX < -totalWidth || elapsed > 8000) {
        g_wifiNotifyPending = false;
    }
    
    // Timeline shows connection progress
    uint32_t tlCol = wt_color(0, 150, 100);
    for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i)
        wt_timeline_set_pixel(i, tlCol);
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
    // Disabled: WiFi notification was too fast/distracting
    // g_wifiNotifyPending = true;
    g_wifiNotifyPending = false;
    g_wifiNotifyStart = millis();
    g_scrollX = WT_MATRIX_WIDTH;
    
    Serial.print("WiFi notification queued: ");
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

// Draw scrolling text "NO WIFI" indicator
static void renderNoWifiScreen(uint32_t now)
{
    wt_display_clear();
    wt_timeline_clear();
    
    // Pulsing red timeline
    uint8_t pulse = 30 + (uint8_t)(25 * sinf(now * 0.003f));
    for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i)
        wt_timeline_set_pixel(i, wt_color(pulse, 0, 0));
    
    // Simple "WiFi" text with sad face - scrolls slowly
    // "WIFI :(" - using simple pixel patterns
    static const uint8_t MSG_LEN = 28;
    static const uint8_t MSG[7][MSG_LEN] = {
        {1,0,1,0,1,0,1,0,0,1,1,1,0,1,1,1,0,1,0,0,0,0,0,0,0,0,0,0}, // W I F I : (
        {1,0,1,0,1,0,1,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,1,0,0,1},
        {1,0,1,0,1,0,1,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,1,0,0,0,0},
        {1,0,1,0,1,0,1,0,0,1,0,0,0,1,1,0,0,1,0,0,1,1,0,1,0,0,0,0},
        {1,1,1,1,1,0,1,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,1,0,0,0,0},
        {0,1,0,1,0,0,1,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,1,0,0,1},
        {0,1,0,1,0,0,1,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0},
    };
    
    int16_t scroll = -((int16_t)(now / 100) % (MSG_LEN + 22)) + 20;
    uint32_t col = wt_color(200, 100, 50);
    
    for (uint8_t y = 0; y < 7; ++y)
    {
        for (uint8_t mx = 0; mx < MSG_LEN; ++mx)
        {
            int16_t x = scroll + mx;
            if (x >= 0 && x < WT_MATRIX_WIDTH && MSG[y][mx])
                wt_display_set_pixel_xy(x, y, col);
        }
    }
}

void cards_loop()
{
    uint32_t now = millis();
    uint32_t dt = now - g_lastTick;
    g_lastTick = now;

    sampleAudio(now);
    
    // Update brightness with settings
    Settings& cfg = settings_get();
    wt_update_brightness_auto(cfg.brightMin, cfg.brightMax, cfg.brightMode, cfg.brightManual, cfg.brightBlanking);

    // Boot animation takes priority
    if (!g_bootDone)
    {
        renderBootAnimation(now);
        wt_leds_show();
        return;
    }
    
    // Show no-WiFi screen if in AP mode (no credentials)
    if (net_is_ap_mode() && !net_has_wifi_creds())
    {
        renderNoWifiScreen(now);
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
    uint32_t orange = wt_color(255, 100, 0);
    uint32_t dim = wt_color(50, 15, 0);
    
    // Draw background glow (tubes)
    for(int x=0; x<19; ++x) {
        if (x==4 || x==9 || x==14) continue; // Spacers
        for(int y=0; y<7; ++y) wt_display_set_pixel_xy(x, y, dim);
    }
             
    // Digits
    uint8_t h1=hour/10, h2=hour%10, m1=minute/10, m2=minute%10;
    drawDigit(1, 0, h1, orange);
    drawDigit(5, 0, h2, orange);
    drawDigit(10, 0, m1, orange);
    drawDigit(14, 0, m2, orange);
}

// Watchface 5: Glitch
static void clock_render_glitch() {
    int hour; uint8_t minute, second;
    getClockTime(hour, minute, second);
    
    uint32_t col = wt_color(0, 255, 0); // Matrix green
    
    // Occasional glitch
    if (random(20) == 0) {
        col = wt_color(random(255), random(255), random(255));
        hour = random(24); // Fake time
    }
    
    uint8_t x = 2;
    drawDigit(x, 0, hour/10, col); x+=4;
    drawDigit(x, 0, hour%10, col); x+=4;
    x+=2; // Colon space
    drawDigit(x, 0, minute/10, col); x+=4;
    drawDigit(x, 0, minute%10, col);
}

// Watchface 6: Pong
static void clock_render_pong() {
    int hour; uint8_t minute, second;
    getClockTime(hour, minute, second);
    
    // Left paddle (Hour)
    int hy = map(hour, 0, 23, 0, 4);
    for(int i=0; i<3; ++i) wt_display_set_pixel_xy(0, hy+i, wt_color(255,255,255));
    
    // Right paddle (Minute)
    int my = map(minute, 0, 59, 0, 4);
    for(int i=0; i<3; ++i) wt_display_set_pixel_xy(WT_MATRIX_WIDTH-1, my+i, wt_color(255,255,255));
    
    // Ball (Second)
    int bx = map(second, 0, 59, 1, WT_MATRIX_WIDTH-2);
    int by = (second % 14);
    if (by > 6) by = 13 - by; // Bounce up/down
    
    wt_display_set_pixel_xy(bx, by, wt_color(255,255,0));
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

// Weather Preset 6: Terminal
static void weather_render_terminal() {
    wt_display_clear();
    WeatherData current = weather_get_current();
    uint32_t green = wt_color(0, 255, 50);
    
    // Draw "CMD" style prompt
    // > 24C
    
    int8_t t = current.temp;
    bool neg = t < 0;
    t = abs(t);
    if(t>99) t=99;
    
    uint8_t tens = t/10;
    uint8_t ones = t%10;
    
    // Cursor
    wt_display_set_pixel_xy(0, 1, green);
    wt_display_set_pixel_xy(1, 3, green);
    wt_display_set_pixel_xy(0, 5, green);
    
    uint8_t x = 3;
    if (neg) {
        wt_display_set_pixel_xy(x, 3, green);
        x += 3;
    }
    if (tens>0) {
        drawDigit(x, 0, tens, green);
        x += 4;
    }
    drawDigit(x, 0, ones, green);
    x += 4;
    
    // C
    if (x < WT_MATRIX_WIDTH - 3) {
        wt_display_set_pixel_xy(x, 6, green); wt_display_set_pixel_xy(x+1, 6, green); wt_display_set_pixel_xy(x+2, 6, green);
        wt_display_set_pixel_xy(x, 5, green);
        wt_display_set_pixel_xy(x, 4, green);
        wt_display_set_pixel_xy(x, 3, green);
        wt_display_set_pixel_xy(x, 0, green); wt_display_set_pixel_xy(x+1, 0, green); wt_display_set_pixel_xy(x+2, 0, green);
    }
    
    // Blinking block at end
    if ((millis()/500)%2) {
        uint8_t bx = WT_MATRIX_WIDTH-2;
        for(int y=0; y<5; ++y) wt_display_set_pixel_xy(bx, y, green);
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
// Weather Preset 5: Day/Night Cycle
static void weather_render_daynight()
{
    wt_display_clear();
    wt_timeline_clear();
    
    uint32_t now = millis();
    int16_t cycleLen = WT_MATRIX_WIDTH * 4;
    // Slow down the cycle: /500 instead of /100
    int16_t pos = (now / 500) % cycleLen; 
    
    // Background
    for(int x=0; x<WT_MATRIX_WIDTH; x++) {
        for(int y=0; y<WT_MATRIX_HEIGHT; y++) {
            wt_display_set_pixel_xy(x, y, wt_color(5, 5, 20));
        }
    }
    
    int16_t sunX = pos - 10;
    if (sunX >= -5 && sunX < WT_MATRIX_WIDTH + 5) {
        uint32_t sunCol = wt_color(255, 200, 0);
        wt_display_set_pixel_xy(sunX, 3, sunCol);
        wt_display_set_pixel_xy(sunX+1, 3, sunCol);
        wt_display_set_pixel_xy(sunX, 2, sunCol);
        wt_display_set_pixel_xy(sunX+1, 2, sunCol);
        if ((now/200)%2) {
            wt_display_set_pixel_xy(sunX-1, 2, wt_color(150,100,0));
            wt_display_set_pixel_xy(sunX+2, 3, wt_color(150,100,0));
        }
    }
    
    int16_t moonX = sunX - (cycleLen/2);
    if (moonX < -10) moonX += cycleLen;
    if (moonX >= -5 && moonX < WT_MATRIX_WIDTH + 5) {
        uint32_t moonCol = wt_color(200, 200, 255);
        wt_display_set_pixel_xy(moonX, 3, moonCol);
        wt_display_set_pixel_xy(moonX+1, 3, moonCol);
        wt_display_set_pixel_xy(moonX+1, 2, moonCol);
    }
    
    // Stars
    for(int x=0; x<WT_MATRIX_WIDTH; x++) {
        if ((x*7 + x)%5 == 0) wt_display_set_pixel_xy(x, (x*3)%7, wt_color(100,100,150));
    }

    // Temp overlay
    WeatherData current = weather_get_current();
    if (current.valid) {
        uint32_t tCol = tempToColor(current.temp);
        int8_t t = current.temp;
        bool neg = t < 0;
        if (neg) t = -t;
        if (t > 99) t = 99;
        
        uint8_t x = WT_MATRIX_WIDTH - (neg ? 12 : 8);
        if (neg) {
            wt_display_set_pixel_xy(x, 3, tCol); x+=3;
        }
        // Draw at Y=0 to avoid cutoff
        if (t>=10) { drawDigit(x, 0, t/10, tCol); x+=4; }
        drawDigit(x, 0, t%10, tCol);
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

    // 9 weather display presets
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

// Preset 15: Pacman
static void vu_render_pacman() {
    wt_display_clear();
    static float px = 0;
    static int frame = 0;
    
    float speed = 0.1f + g_audioHeight * 0.1f;
    px += speed;
    if (px >= WT_MATRIX_WIDTH) px = -5;
    
    frame++;
    bool open = (frame / 5) % 2;
    int x = (int)px;
    uint32_t yel = wt_color(255, 255, 0);
    
    // Draw Pacman
    //  ###
    // #####
    // #####
    //  ###
    
    // If open, cut out wedge
    for(int dy=0; dy<5; ++dy) {
        for(int dx=0; dx<5; ++dx) {
            // Circle approximation
            if ((dx==0||dx==4) && (dy==0||dy==4)) continue;
            
            if (open && dx > 2 && (dy == 2 || (dx > 3 && (dy==1||dy==3)))) continue;
            
            if (x+dx >= 0 && x+dx < WT_MATRIX_WIDTH)
                wt_display_set_pixel_xy(x+dx, 1+dy, yel);
        }
    }
    
    // Dots
    for(int i=0; i<WT_MATRIX_WIDTH; i+=4) {
        if (i > x+2) wt_display_set_pixel_xy(i, 3, wt_color(200, 150, 100));
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
        default: vu_render_spectrum(); break;
    }
    
    // Timeline shows beat with rainbow chase on beat if not handled by preset
    // (Some presets might want to own the timeline, but consistent beat logic is fine)
    if (g_vuPreset != 13) { // Heartbeat handles its own timeline? Actually overlay handles it.
        // Just use standard overlay
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
    // Read buttons for game control
    bool touch = wt_cap_touch_active();
    static bool lastTouch = false;
    static bool lastB1 = false;
    static bool lastB2 = false;
    
    bool b1 = wt_button1_pressed();
    bool b2 = wt_button2_pressed();
    
    g_gameBtn1 = b1;
    g_gameBtn2 = b2;
    g_gameTouchEdge = touch && !lastTouch;
    
    // When game over: BTN1 = next game, BTN2 = prev game, Touch = restart
    if (g_gameOver) {
        if (b1 && !lastB1) {
            // Next game
            g_gameMode = (g_gameMode + 1) % GAME_MODE_COUNT;
            game_start_current();
        } else if (b2 && !lastB2) {
            // Previous game
            g_gameMode = (g_gameMode + GAME_MODE_COUNT - 1) % GAME_MODE_COUNT;
            game_start_current();
        } else if (g_gameTouchEdge) {
            // Restart current game
            game_start_current();
        }
    }
    
    lastTouch = touch;
    lastB1 = b1;
    lastB2 = b2;
    
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
    
    // Spawn new sparkles
    uint8_t spawnChance = (g_sparkleMode == 0) ? 3 : (g_sparkleMode == 1) ? 2 : 4;
    if (random(10) < spawnChance)
    {
        // Find empty slot
        for (uint8_t i = 0; i < SPARKLE_MAX; ++i)
        {
            if (g_sparkles[i].life == 0)
            {
                if (g_sparkleMode == 0) // Random sparkle
                {
                    g_sparkles[i].x = random(WT_MATRIX_WIDTH);
                    g_sparkles[i].y = random(WT_MATRIX_HEIGHT);
                }
                else if (g_sparkleMode == 1) // Wave from bottom
                {
                    g_sparkles[i].x = random(WT_MATRIX_WIDTH);
                    g_sparkles[i].y = (now / 200) % WT_MATRIX_HEIGHT;
                }
                else // Rain from top
                {
                    g_sparkles[i].x = random(WT_MATRIX_WIDTH);
                    g_sparkles[i].y = WT_MATRIX_HEIGHT - 1;
                }
                
                g_sparkles[i].life = 8 + random(12);
                g_sparkles[i].hue = random(256);
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
// MQTT Card - Display notifications and Home Assistant data
// ============================================================================

static int16_t g_mqttScrollX = 0;
static uint32_t g_mqttLastScroll = 0;
static uint8_t g_mqttDisplayMode = 0;  // 0=message, 1=status

// Simple icon bitmaps (7x7)
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
