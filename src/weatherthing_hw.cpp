#include <Adafruit_NeoPixel.h>
#include "weatherthing_hw.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include <string.h>

// ADC Continuous mode for high-speed mic sampling
#include "esp_adc/adc_continuous.h"
#include "esp_log.h"

// ESP32-C3 GPIO matrix fix - after RMT transmission, force GPIO low
static void fixGpioMatrix(uint8_t pin) {
    // ESP-IDF 5.x uses esp_rom_gpio_connect_out_signal instead of gpio_matrix_out
    esp_rom_gpio_connect_out_signal((gpio_num_t)pin, SIG_GPIO_OUT_IDX, false, false);
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)pin, 0);
}

static Adafruit_NeoPixel matrixStrip(WT_MATRIX_PIXELS, WT_MATRIX_PIN, NEO_GRB + NEO_KHZ800);
static Adafruit_NeoPixel timelineStrip(WT_TIMELINE_PIXELS, WT_TIMELINE_PIN, NEO_GRB + NEO_KHZ800);

uint32_t wt_color(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

// HSV to RGB conversion (h=0-255, s=0-255, v=0-255)
uint32_t wt_color_hsv(uint8_t h, uint8_t s, uint8_t v)
{
    if (s == 0) return wt_color(v, v, v);
    
    uint8_t region = h / 43;
    uint8_t remainder = (h - (region * 43)) * 6;
    
    uint8_t p = (v * (255 - s)) >> 8;
    uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;
    
    switch (region) {
        case 0:  return wt_color(v, t, p);
        case 1:  return wt_color(q, v, p);
        case 2:  return wt_color(p, v, t);
        case 3:  return wt_color(p, q, v);
        case 4:  return wt_color(t, p, v);
        default: return wt_color(v, p, q);
    }
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

// ADC Continuous mode for microphone
static const char* TAG = "WT_MIC";
static adc_continuous_handle_t g_adcHandle = NULL;
static const uint32_t ADC_SAMPLE_FREQ_HZ = 20000;  // 20kHz sample rate
static const uint32_t ADC_FRAME_SIZE = 256;        // Samples per DMA frame
static const uint32_t ADC_BUFFER_SIZE = 2048;      // Total DMA buffer (increased)

// Ring buffer for processed samples
static const uint16_t MIC_RING_SIZE = 512;
static uint16_t g_micRing[MIC_RING_SIZE];
static volatile uint16_t g_micRingHead = 0;
static volatile uint16_t g_micRingTail = 0;
static volatile uint16_t g_micLatestSample = 2048;
static volatile uint16_t g_micPeakToPeak = 0;      // Peak-to-peak in recent window
static volatile uint16_t g_micRunningMin = 2048;   // Running min over window
static volatile uint16_t g_micRunningMax = 2048;   // Running max over window
static volatile bool g_adcRunning = false;

// ADC continuous mode callback - called from ISR context when DMA buffer ready
static bool IRAM_ATTR adc_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data)
{
    return true;  // Signal task to process (we poll instead)
}

// Initialize ADC continuous mode for microphone
static void wt_mic_adc_init()
{
    // ESP32-C3 GPIO4 = ADC1 Channel 4
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = ADC_BUFFER_SIZE,
        .conv_frame_size = ADC_FRAME_SIZE,
    };
    
    esp_err_t ret = adc_continuous_new_handle(&adc_config, &g_adcHandle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ADC handle: %d", ret);
        return;
    }
    
    // Configure the ADC pattern for our mic channel
    adc_digi_pattern_config_t adc_pattern = {
        .atten = ADC_ATTEN_DB_12,      // Full range 0-3.3V (renamed from DB_11 in IDF 5.x)
        .channel = ADC_CHANNEL_4,       // GPIO4 on ESP32-C3
        .unit = ADC_UNIT_1,
        .bit_width = ADC_BITWIDTH_12,
    };
    
    adc_continuous_config_t dig_cfg = {
        .pattern_num = 1,
        .adc_pattern = &adc_pattern,
        .sample_freq_hz = ADC_SAMPLE_FREQ_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,  // ESP32-C3 requires TYPE2
    };
    
    ret = adc_continuous_config(g_adcHandle, &dig_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC: %d", ret);
        adc_continuous_deinit(g_adcHandle);
        g_adcHandle = NULL;
        return;
    }
    
    // Register callback (optional, we use polling)
    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = adc_conv_done_cb,
    };
    adc_continuous_register_event_callbacks(g_adcHandle, &cbs, NULL);
    
    // Start continuous conversion
    ret = adc_continuous_start(g_adcHandle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start ADC: %d", ret);
        adc_continuous_deinit(g_adcHandle);
        g_adcHandle = NULL;
        return;
    }
    
    g_adcRunning = true;
    ESP_LOGI(TAG, "ADC continuous mode started at %lu Hz", ADC_SAMPLE_FREQ_HZ);
}

// Process samples from ADC DMA buffer - call this periodically
static void wt_mic_process_dma()
{
    if (!g_adcRunning || g_adcHandle == NULL) return;
    
    uint8_t result[ADC_FRAME_SIZE * SOC_ADC_DIGI_RESULT_BYTES];
    uint32_t ret_num = 0;
    
    // Read all available data from DMA
    esp_err_t ret = adc_continuous_read(g_adcHandle, result, sizeof(result), &ret_num, 0);
    if (ret != ESP_OK || ret_num == 0) return;
    
    uint16_t frameMin = 4095, frameMax = 0;
    uint32_t numSamples = ret_num / SOC_ADC_DIGI_RESULT_BYTES;
    
    for (uint32_t i = 0; i < numSamples; i++) {
        // ESP32-C3 TYPE2 format: 12-bit data in bits 0-11
        uint16_t val = result[i * SOC_ADC_DIGI_RESULT_BYTES] | 
                       ((result[i * SOC_ADC_DIGI_RESULT_BYTES + 1] & 0x0F) << 8);
        
        // Track min/max for this frame
        if (val < frameMin) frameMin = val;
        if (val > frameMax) frameMax = val;
        
        // Store latest sample
        g_micLatestSample = val;
        
        // Store in ring buffer
        uint16_t nextHead = (g_micRingHead + 1) % MIC_RING_SIZE;
        if (nextHead != g_micRingTail) {
            g_micRing[g_micRingHead] = val;
            g_micRingHead = nextHead;
        }
    }
    
    // Direct frame peak-to-peak - no slow envelope tracking
    uint16_t framePP = (frameMax > frameMin) ? (frameMax - frameMin) : 0;
    
    // Fast attack, moderate decay for responsive feel
    if (framePP > g_micPeakToPeak) {
        g_micPeakToPeak = framePP;  // Instant attack
    } else {
        // Decay ~6% per frame for snappy response
        g_micPeakToPeak = (g_micPeakToPeak * 15) / 16;
    }
}

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
    
    // Initialize ADC continuous mode for microphone
    wt_mic_adc_init();
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
    // No artificial limit - let user control brightness up to 255
    // Hardware: 152 LEDs * 36mA max = 5.5A at full white
    // FET rated 2A continuous, 3A burst - typical content uses ~30-50% of max
    // At brightness 255 with average content: ~1.5-2A
    // Full white at 255 would overdraw - but that's rare in practice
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
    // Process any pending DMA data first
    wt_mic_process_dma();
    
    // Return latest sample from DMA, fallback to analogRead if not running
    if (g_adcRunning) {
        return g_micLatestSample;
    }
    return analogRead(WT_MIC_PIN);
}

// Get peak-to-peak amplitude from recent DMA samples (more accurate than single reads)
uint16_t wt_mic_peak_to_peak()
{
    wt_mic_process_dma();
    return g_micPeakToPeak;
}

// Get number of samples available in ring buffer
uint16_t wt_mic_samples_available()
{
    wt_mic_process_dma();
    if (g_micRingHead >= g_micRingTail) {
        return g_micRingHead - g_micRingTail;
    }
    return MIC_RING_SIZE - g_micRingTail + g_micRingHead;
}

// Read samples from ring buffer (returns actual count read)
uint16_t wt_mic_read_samples(uint16_t* buffer, uint16_t maxCount)
{
    wt_mic_process_dma();
    uint16_t count = 0;
    while (count < maxCount && g_micRingTail != g_micRingHead) {
        buffer[count++] = g_micRing[g_micRingTail];
        g_micRingTail = (g_micRingTail + 1) % MIC_RING_SIZE;
    }
    return count;
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
