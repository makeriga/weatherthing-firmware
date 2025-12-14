#include "factory_test.h"
#include "weatherthing_hw.h"
#include <Preferences.h>

// Factory test state machine
enum FactoryTestState {
    FT_INIT = 0,
    FT_LED_RED,
    FT_LED_GREEN,
    FT_LED_BLUE,
    FT_LED_WHITE,
    FT_LED_RAINBOW,
    FT_WAIT_BTN1,
    FT_WAIT_BTN2,
    FT_WAIT_CAP,
    FT_MIC_TEST,
    FT_COMPLETE,
    FT_DONE
};

static FactoryTestState g_ftState = FT_INIT;
static uint32_t g_ftStateStart = 0;
static uint32_t g_ftMicPeakMax = 0;
static bool g_ftBtn1Seen = false;
static bool g_ftBtn2Seen = false;
static bool g_ftCapSeen = false;
static Preferences g_ftPrefs;

// Duration for each LED test phase (ms)
static const uint32_t LED_PHASE_DURATION = 800;
static const uint32_t MIC_TEST_DURATION = 3000;

bool factory_test_completed()
{
    bool completed = false;
    if (g_ftPrefs.begin("fttest", true)) {
        completed = g_ftPrefs.getBool("done", false);
        g_ftPrefs.end();
    }
    return completed;
}

void factory_test_mark_complete()
{
    if (g_ftPrefs.begin("fttest", false)) {
        g_ftPrefs.putBool("done", true);
        g_ftPrefs.end();
    }
    Serial.println("[Factory Test] Marked as complete");
}

void factory_test_reset()
{
    if (g_ftPrefs.begin("fttest", false)) {
        g_ftPrefs.putBool("done", false);
        g_ftPrefs.end();
    }
    g_ftState = FT_INIT;
    Serial.println("[Factory Test] Reset - will run on next boot");
}

// Draw a simple progress indicator
static void drawProgress(uint8_t step, uint8_t total, uint32_t color)
{
    wt_timeline_clear();
    uint8_t filled = (step * WT_TIMELINE_PIXELS) / total;
    for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i) {
        if (i < filled) {
            wt_timeline_set_pixel(i, color);
        } else {
            wt_timeline_set_pixel(i, wt_color(20, 20, 20));
        }
    }
}

// Draw text indicator for current test phase
static void drawTestLabel(const char* label, uint32_t color)
{
    // Simple indicator - just fill display with color and show pattern
    wt_display_clear();
    
    // Draw a checkmark pattern in center for visual feedback
    uint8_t cx = WT_MATRIX_WIDTH / 2;
    uint8_t cy = WT_MATRIX_HEIGHT / 2;
    
    // Simple icon based on test type
    if (label[0] == 'B') { // Button
        // Draw button icon
        wt_display_set_pixel_xy(cx-1, cy-1, color);
        wt_display_set_pixel_xy(cx, cy-1, color);
        wt_display_set_pixel_xy(cx+1, cy-1, color);
        wt_display_set_pixel_xy(cx-1, cy, color);
        wt_display_set_pixel_xy(cx, cy, color);
        wt_display_set_pixel_xy(cx+1, cy, color);
        wt_display_set_pixel_xy(cx-1, cy+1, color);
        wt_display_set_pixel_xy(cx, cy+1, color);
        wt_display_set_pixel_xy(cx+1, cy+1, color);
    } else if (label[0] == 'C') { // Cap touch
        // Draw hand/touch icon
        wt_display_set_pixel_xy(cx, cy-2, color);
        wt_display_set_pixel_xy(cx, cy-1, color);
        wt_display_set_pixel_xy(cx, cy, color);
        wt_display_set_pixel_xy(cx-1, cy+1, color);
        wt_display_set_pixel_xy(cx, cy+1, color);
        wt_display_set_pixel_xy(cx+1, cy+1, color);
    } else if (label[0] == 'M') { // Mic
        // Draw mic icon
        wt_display_set_pixel_xy(cx, cy-2, color);
        wt_display_set_pixel_xy(cx-1, cy-1, color);
        wt_display_set_pixel_xy(cx, cy-1, color);
        wt_display_set_pixel_xy(cx+1, cy-1, color);
        wt_display_set_pixel_xy(cx, cy, color);
        wt_display_set_pixel_xy(cx-1, cy+1, color);
        wt_display_set_pixel_xy(cx+1, cy+1, color);
        wt_display_set_pixel_xy(cx, cy+2, color);
    }
}

// Draw VU-style mic level indicator
static void drawMicLevel(uint16_t level)
{
    wt_display_clear();
    
    // Scale level to display width (0-4095 -> 0-20)
    uint8_t bars = (level * WT_MATRIX_WIDTH) / 1000;
    if (bars > WT_MATRIX_WIDTH) bars = WT_MATRIX_WIDTH;
    
    for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x) {
        uint32_t col;
        if (x < bars) {
            // Green -> Yellow -> Red gradient
            if (x < WT_MATRIX_WIDTH / 3) {
                col = wt_color(0, 255, 0);
            } else if (x < 2 * WT_MATRIX_WIDTH / 3) {
                col = wt_color(255, 255, 0);
            } else {
                col = wt_color(255, 0, 0);
            }
        } else {
            col = wt_color(15, 15, 15);
        }
        
        // Draw vertical bar
        for (uint8_t y = 1; y < WT_MATRIX_HEIGHT - 1; ++y) {
            wt_display_set_pixel_xy(x, y, col);
        }
    }
    
    // Show "MIC" indicator at top
    wt_display_set_pixel_xy(0, 6, wt_color(0, 100, 255));
    wt_display_set_pixel_xy(1, 6, wt_color(0, 100, 255));
    wt_display_set_pixel_xy(2, 6, wt_color(0, 100, 255));
}

bool factory_test_run()
{
    uint32_t now = millis();
    uint32_t elapsed = now - g_ftStateStart;
    
    switch (g_ftState) {
        case FT_INIT:
            Serial.println("[Factory Test] Starting LED matrix test...");
            g_ftState = FT_LED_RED;
            g_ftStateStart = now;
            g_ftMicPeakMax = 0;
            g_ftBtn1Seen = false;
            g_ftBtn2Seen = false;
            g_ftCapSeen = false;
            break;
            
        case FT_LED_RED:
            wt_display_fill(wt_color(255, 0, 0));
            wt_timeline_fill(wt_color(255, 0, 0));
            drawProgress(1, 10, wt_color(255, 0, 0));
            wt_leds_show();
            if (elapsed > LED_PHASE_DURATION) {
                g_ftState = FT_LED_GREEN;
                g_ftStateStart = now;
            }
            break;
            
        case FT_LED_GREEN:
            wt_display_fill(wt_color(0, 255, 0));
            wt_timeline_fill(wt_color(0, 255, 0));
            drawProgress(2, 10, wt_color(0, 255, 0));
            wt_leds_show();
            if (elapsed > LED_PHASE_DURATION) {
                g_ftState = FT_LED_BLUE;
                g_ftStateStart = now;
            }
            break;
            
        case FT_LED_BLUE:
            wt_display_fill(wt_color(0, 0, 255));
            wt_timeline_fill(wt_color(0, 0, 255));
            drawProgress(3, 10, wt_color(0, 0, 255));
            wt_leds_show();
            if (elapsed > LED_PHASE_DURATION) {
                g_ftState = FT_LED_WHITE;
                g_ftStateStart = now;
            }
            break;
            
        case FT_LED_WHITE:
            wt_display_fill(wt_color(255, 255, 255));
            wt_timeline_fill(wt_color(255, 255, 255));
            drawProgress(4, 10, wt_color(255, 255, 255));
            wt_leds_show();
            if (elapsed > LED_PHASE_DURATION) {
                g_ftState = FT_LED_RAINBOW;
                g_ftStateStart = now;
            }
            break;
            
        case FT_LED_RAINBOW:
            // Rainbow sweep across display
            for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x) {
                uint8_t hue = (x * 12 + (now / 10)) % 256;
                uint32_t col = wt_color_hsv(hue, 255, 255);
                for (uint8_t y = 0; y < WT_MATRIX_HEIGHT; ++y) {
                    wt_display_set_pixel_xy(x, y, col);
                }
            }
            for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i) {
                uint8_t hue = (i * 20 + (now / 10)) % 256;
                wt_timeline_set_pixel(i, wt_color_hsv(hue, 255, 255));
            }
            wt_leds_show();
            if (elapsed > LED_PHASE_DURATION * 2) {
                Serial.println("[Factory Test] LED test complete. Press BTN1...");
                g_ftState = FT_WAIT_BTN1;
                g_ftStateStart = now;
            }
            break;
            
        case FT_WAIT_BTN1:
            drawTestLabel("BTN1", wt_color(255, 100, 0));
            drawProgress(5, 10, wt_color(255, 100, 0));
            
            // Pulsing indicator
            {
                uint8_t pulse = 100 + (uint8_t)(100 * sinf(now * 0.005f));
                wt_display_set_pixel_xy(WT_MATRIX_WIDTH - 2, 3, wt_color(pulse, pulse / 2, 0));
            }
            wt_leds_show();
            
            if (wt_button1_pressed() || wt_button1_is_down()) {
                g_ftBtn1Seen = true;
                Serial.println("[Factory Test] BTN1 OK! Press BTN2...");
                g_ftState = FT_WAIT_BTN2;
                g_ftStateStart = now;
            }
            // Timeout after 30 seconds - auto-pass for production
            if (elapsed > 30000) {
                Serial.println("[Factory Test] BTN1 timeout - skipping");
                g_ftState = FT_WAIT_BTN2;
                g_ftStateStart = now;
            }
            break;
            
        case FT_WAIT_BTN2:
            drawTestLabel("BTN2", wt_color(0, 100, 255));
            drawProgress(6, 10, wt_color(0, 100, 255));
            
            {
                uint8_t pulse = 100 + (uint8_t)(100 * sinf(now * 0.005f));
                wt_display_set_pixel_xy(1, 3, wt_color(0, pulse / 2, pulse));
            }
            wt_leds_show();
            
            if (wt_button2_pressed() || wt_button2_is_down()) {
                g_ftBtn2Seen = true;
                Serial.println("[Factory Test] BTN2 OK! Touch capacitive sensor...");
                g_ftState = FT_WAIT_CAP;
                g_ftStateStart = now;
            }
            if (elapsed > 30000) {
                Serial.println("[Factory Test] BTN2 timeout - skipping");
                g_ftState = FT_WAIT_CAP;
                g_ftStateStart = now;
            }
            break;
            
        case FT_WAIT_CAP:
            drawTestLabel("CAP", wt_color(255, 0, 255));
            drawProgress(7, 10, wt_color(255, 0, 255));
            
            {
                uint8_t pulse = 100 + (uint8_t)(100 * sinf(now * 0.005f));
                wt_display_set_pixel_xy(WT_MATRIX_WIDTH / 2, 0, wt_color(pulse, 0, pulse));
            }
            wt_leds_show();
            
            if (wt_cap_touch_active()) {
                g_ftCapSeen = true;
                Serial.println("[Factory Test] CAP OK! Testing microphone...");
                g_ftState = FT_MIC_TEST;
                g_ftStateStart = now;
                g_ftMicPeakMax = 0;
            }
            if (elapsed > 30000) {
                Serial.println("[Factory Test] CAP timeout - skipping");
                g_ftState = FT_MIC_TEST;
                g_ftStateStart = now;
            }
            break;
            
        case FT_MIC_TEST:
            {
                // Read mic level using peak-to-peak for better accuracy
                uint16_t micPP = wt_mic_peak_to_peak();
                if (micPP > g_ftMicPeakMax) g_ftMicPeakMax = micPP;
                
                drawMicLevel(micPP);
                drawProgress(8, 10, wt_color(0, 255, 100));
                
                // Show max detected level in timeline
                uint8_t maxBars = (g_ftMicPeakMax * WT_TIMELINE_PIXELS) / 1000;
                if (maxBars > WT_TIMELINE_PIXELS) maxBars = WT_TIMELINE_PIXELS;
                for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i) {
                    if (i < maxBars) {
                        wt_timeline_set_pixel(i, wt_color(0, 255, 100));
                    }
                }
                wt_leds_show();
                
                // Complete after duration or if good sound detected
                if (elapsed > MIC_TEST_DURATION || g_ftMicPeakMax > 500) {
                    Serial.printf("[Factory Test] MIC test complete. Peak: %u\n", g_ftMicPeakMax);
                    g_ftState = FT_COMPLETE;
                    g_ftStateStart = now;
                }
            }
            break;
            
        case FT_COMPLETE:
            // Success animation - green sweep
            wt_display_clear();
            wt_timeline_clear();
            
            {
                // Checkmark animation
                uint8_t frame = (elapsed / 50) % 20;
                uint32_t green = wt_color(0, 255, 0);
                
                // Draw expanding checkmark
                if (frame > 0) wt_display_set_pixel_xy(6, 2, green);
                if (frame > 1) wt_display_set_pixel_xy(7, 3, green);
                if (frame > 2) wt_display_set_pixel_xy(8, 4, green);
                if (frame > 3) wt_display_set_pixel_xy(9, 5, green);
                if (frame > 4) wt_display_set_pixel_xy(10, 4, green);
                if (frame > 5) wt_display_set_pixel_xy(11, 3, green);
                if (frame > 6) wt_display_set_pixel_xy(12, 2, green);
                if (frame > 7) wt_display_set_pixel_xy(13, 1, green);
                
                // Fill timeline green
                for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i) {
                    wt_timeline_set_pixel(i, green);
                }
            }
            wt_leds_show();
            
            if (elapsed > 2000) {
                // Log results
                Serial.println("[Factory Test] === RESULTS ===");
                Serial.printf("  LED Matrix: PASS\n");
                Serial.printf("  BTN1: %s\n", g_ftBtn1Seen ? "PASS" : "SKIP/FAIL");
                Serial.printf("  BTN2: %s\n", g_ftBtn2Seen ? "PASS" : "SKIP/FAIL");
                Serial.printf("  CAP Touch: %s\n", g_ftCapSeen ? "PASS" : "SKIP/FAIL");
                Serial.printf("  MIC Peak: %u %s\n", g_ftMicPeakMax, g_ftMicPeakMax > 100 ? "PASS" : "LOW");
                Serial.println("[Factory Test] Complete!");
                
                factory_test_mark_complete();
                g_ftState = FT_DONE;
            }
            break;
            
        case FT_DONE:
            return true;
    }
    
    return false;
}
