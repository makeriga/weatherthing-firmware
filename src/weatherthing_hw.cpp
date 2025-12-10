#include <Adafruit_NeoPixel.h>
#include "weatherthing_hw.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include <string.h>

// ESP32-C3 GPIO matrix fix - after RMT transmission, force GPIO low
static void fixGpioMatrix(uint8_t pin) {
    // ESP-IDF 5.x uses esp_rom_gpio_connect_out_signal instead of gpio_matrix_out
    esp_rom_gpio_connect_out_signal((gpio_num_t)pin, SIG_GPIO_OUT_IDX, false, false);
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)pin, 0);
}

static Adafruit_NeoPixel matrixStrip(WT_MATRIX_PIXELS, WT_MATRIX_PIN, NEO_GRB + NEO_KHZ800);
static Adafruit_NeoPixel timelineStrip(WT_TIMELINE_PIXELS, WT_TIMELINE_PIN, NEO_GRB + NEO_KHZ800);

// Software buffer for pixel data (RGB format)
static uint8_t matrixBuffer[WT_MATRIX_PIXELS * 3];
static uint8_t timelineBuffer[WT_TIMELINE_PIXELS * 3];

uint32_t wt_color(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static const uint32_t WT_BUTTON_DEBOUNCE_MS = 30;

// Simple IIR smoothing parameters for sensors
static const uint8_t WT_MIC_AVG_SHIFT = 4;    // 1/16
static const uint8_t WT_MIC_LEVEL_SHIFT = 4;  // 1/16
static const uint8_t WT_LIGHT_LEVEL_SHIFT = 4; // 1/16

struct WtButtonState
{
    uint8_t pin;
    bool activeLow;
    bool stable;
    bool lastStable;
    uint32_t lastChangeMs;
};

static WtButtonState g_button1 = {WT_BUTTON1_PIN, true, true, true, 0};
static WtButtonState g_button2 = {WT_BUTTON2_PIN, true, true, true, 0};

static uint32_t g_micAvg = 2048;   // approximate mid-scale for biased mic
static uint32_t g_micLevel = 0;
static uint32_t g_lightLevel = 0;
static uint8_t g_brightness = 32;

static bool wt_read_button(WtButtonState &button)
{
    bool raw = digitalRead(button.pin) == (button.activeLow ? LOW : HIGH);
    uint32_t now = millis();

    if (raw != button.stable && (now - button.lastChangeMs) >= WT_BUTTON_DEBOUNCE_MS)
    {
        button.stable = raw;
        button.lastChangeMs = now;
    }

    return button.stable;
}

void wt_hw_begin()
{
    matrixStrip.begin();
    timelineStrip.begin();
    matrixStrip.setBrightness(g_brightness);
    timelineStrip.setBrightness(g_brightness);

    wt_display_clear();
    wt_timeline_clear();
    wt_leds_show();

    pinMode(WT_BUTTON1_PIN, INPUT_PULLUP);
    pinMode(WT_BUTTON2_PIN, INPUT_PULLUP);
    pinMode(WT_CAP_TOUCH_PIN, INPUT);

    g_button1.stable = wt_read_button(g_button1);
    g_button1.lastStable = g_button1.stable;
    g_button2.stable = wt_read_button(g_button2);
    g_button2.lastStable = g_button2.stable;
}

bool wt_button1_is_down()
{
    return g_button1.stable; // stable is true when button is pressed
}

bool wt_button2_is_down()
{
    return g_button2.stable; // stable is true when button is pressed
}

void wt_display_clear()
{
    matrixStrip.clear();
}

void wt_display_fill(uint32_t color)
{
    matrixStrip.fill(color);
}

void wt_display_set_pixel_raw(uint16_t index, uint32_t color)
{
    if (index >= WT_MATRIX_PIXELS)
    {
        return;
    }
    matrixStrip.setPixelColor(index, color);
}

static uint16_t xyToIndex(uint8_t x, uint8_t y)
{
    if (x >= WT_MATRIX_WIDTH || y >= WT_MATRIX_HEIGHT)
    {
        return 0xFFFF;
    }

    // Column-major serpentine starting at bottom-left corner.
    // Even columns go bottom->top, odd columns go top->bottom.
    uint8_t col = x;
    bool evenCol = (col % 2 == 0);
    uint16_t base = col * WT_MATRIX_HEIGHT;

    if (evenCol)
    {
        return base + y;
    }
    else
    {
        return base + (WT_MATRIX_HEIGHT - 1 - y);
    }
}

void wt_display_set_pixel_xy(uint8_t x, uint8_t y, uint32_t color)
{
    uint16_t idx = xyToIndex(x, y);
    if (idx == 0xFFFF)
    {
        return;
    }
    matrixStrip.setPixelColor(idx, color);
}

void wt_timeline_clear()
{
    timelineStrip.clear();
}

void wt_timeline_fill(uint32_t color)
{
    timelineStrip.fill(color);
}

void wt_timeline_set_pixel(uint8_t index, uint32_t color)
{
    if (index >= WT_TIMELINE_PIXELS)
    {
        return;
    }
    timelineStrip.setPixelColor(index, color);
}

void wt_leds_show()
{
    matrixStrip.show();
    fixGpioMatrix(WT_MATRIX_PIN);
    delayMicroseconds(100);
    timelineStrip.show();
    fixGpioMatrix(WT_TIMELINE_PIN);
}

void wt_set_brightness(uint8_t brightness)
{
    if (brightness > 80)
    {
        brightness = 80;
    }
    g_brightness = brightness;
    matrixStrip.setBrightness(g_brightness);
    timelineStrip.setBrightness(g_brightness);
}

bool wt_button1_pressed()
{
    wt_read_button(g_button1);
    bool pressed = g_button1.stable && !g_button1.lastStable;
    g_button1.lastStable = g_button1.stable;
    return pressed;
}

bool wt_button2_pressed()
{
    wt_read_button(g_button2);
    bool pressed = g_button2.stable && !g_button2.lastStable;
    g_button2.lastStable = g_button2.stable;
    return pressed;
}

bool wt_cap_touch_active()
{
    return digitalRead(WT_CAP_TOUCH_PIN) == HIGH;
}

uint16_t wt_mic_read_raw()
{
    return analogRead(WT_MIC_PIN);
}

uint16_t wt_light_read_raw()
{
    return analogRead(WT_LIGHT_PIN);
}

uint16_t wt_mic_level()
{
    uint16_t sample = analogRead(WT_MIC_PIN);

    // Update long-term average around the bias point
    g_micAvg += ((uint32_t)sample - g_micAvg) >> WT_MIC_AVG_SHIFT;

    uint32_t diff = (sample > g_micAvg) ? (sample - g_micAvg) : (g_micAvg - sample);
    g_micLevel += (diff - g_micLevel) >> WT_MIC_LEVEL_SHIFT;

    return (uint16_t)g_micLevel;
}

uint16_t wt_light_level()
{
    uint16_t sample = analogRead(WT_LIGHT_PIN);

    if (g_lightLevel == 0)
    {
        g_lightLevel = sample;
    }
    else
    {
        g_lightLevel += ((uint32_t)sample - g_lightLevel) >> WT_LIGHT_LEVEL_SHIFT;
    }

    return (uint16_t)g_lightLevel;
}

// Blanking state for LED-free light measurement
static bool g_blankingActive = false;
static uint32_t g_blankingStart = 0;
static uint16_t g_blankingReading = 0;
static bool g_blankingValid = false;

// Stable light level with heavy hysteresis
static uint32_t g_stableLightLevel = 2048;

void wt_update_brightness_auto(uint8_t minB, uint8_t maxB, uint8_t mode, uint8_t manual, bool useBlanking, uint8_t blankIntervalSecs)
{
    static uint32_t lastUpdate = 0;
    static uint8_t lastBrightness = 30;
    static uint32_t lastBlankingMs = 0;

    uint32_t now = millis();
    
    // Manual mode - just set fixed brightness
    if (mode == 1) {
        if (lastBrightness != manual) {
            lastBrightness = manual;
            wt_set_brightness(manual);
        }
        return;
    }
    
    // Blanking mode - periodically blank display to measure ambient light
    // Interval is configurable in seconds (10-120), blank for 10ms to read sensor
    uint32_t blankIntervalMs = (uint32_t)blankIntervalSecs * 1000UL;
    if (blankIntervalMs < 10000) blankIntervalMs = 10000; // Minimum 10 seconds
    
    if (useBlanking && !g_blankingActive && (now - lastBlankingMs > blankIntervalMs)) {
        // Start blanking - turn off all LEDs briefly
        g_blankingActive = true;
        g_blankingStart = now;
        matrixStrip.clear();
        timelineStrip.clear();
        matrixStrip.show();
        fixGpioMatrix(WT_MATRIX_PIN);
        timelineStrip.show();
        fixGpioMatrix(WT_TIMELINE_PIN);
        return;
    }
    
    if (g_blankingActive) {
        // Wait 10ms for capacitor to settle, then read
        if (now - g_blankingStart >= 10) {
            // Take reading with LEDs off
            g_blankingReading = analogRead(WT_LIGHT_PIN);
            g_blankingValid = true;
            g_blankingActive = false;
            lastBlankingMs = now;
            
            // Restore display by calling show (will apply current brightness)
            wt_leds_show();
        }
        return;
    }
    
    // Normal update rate
    if (now - lastUpdate < 200) {
        return;
    }
    lastUpdate = now;

    // Get light level - use blanking reading if available and enabled
    uint16_t level;
    if (useBlanking && g_blankingValid) {
        level = g_blankingReading;
    } else {
        level = wt_light_level();
    }
    
    // Very slow IIR filter for stable level (reduces hunting)
    g_stableLightLevel = (g_stableLightLevel * 31 + level) / 32;
    
    // Calculate target brightness
    uint8_t target = minB;
    if (g_stableLightLevel > 0) {
        target = minB + (uint8_t)((g_stableLightLevel * (maxB - minB)) / 4095U);
    }
    if (target > maxB) target = maxB;
    if (target < minB) target = minB;

    // Strong hysteresis - only change if difference is significant
    int16_t diff = (int16_t)target - (int16_t)lastBrightness;
    
    // Need at least 3 levels difference to trigger change
    if (abs(diff) < 3) {
        return;  // Stay at current brightness
    }
    
    // Slow ramping
    if (diff > 2) {
        diff = 2;
    } else if (diff < -2) {
        diff = -2;
    }

    uint8_t newB = (uint8_t)((int16_t)lastBrightness + diff);
    if (newB < minB) newB = minB;
    if (newB > maxB) newB = maxB;
    
    lastBrightness = newB;
    wt_set_brightness(newB);
}
