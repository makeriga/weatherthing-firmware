#pragma once

#include <Arduino.h>

// LED matrix: 7x20 WS2812B
static const uint8_t WT_MATRIX_PIN = 3;
static const uint16_t WT_MATRIX_WIDTH = 20;
static const uint16_t WT_MATRIX_HEIGHT = 7;
static const uint16_t WT_MATRIX_PIXELS = WT_MATRIX_WIDTH * WT_MATRIX_HEIGHT;

// Timeline bar: 12 WS2812B
static const uint8_t WT_TIMELINE_PIN = 1;
static const uint16_t WT_TIMELINE_PIXELS = 12;

// Inputs
static const uint8_t WT_BUTTON1_PIN = 6;     // KEY1
static const uint8_t WT_BUTTON2_PIN = 2;     // KEY2
static const uint8_t WT_MIC_PIN = 4;         // ADC
static const uint8_t WT_CAP_TOUCH_PIN = 5;   // AF223 output (digital)
static const uint8_t WT_LIGHT_PIN = 0;       // Photoresistor ADC

void wt_hw_begin();

// LED helpers
void wt_display_clear();
void wt_display_fill(uint32_t color);
void wt_display_set_pixel_raw(uint16_t index, uint32_t color);
void wt_display_set_pixel_xy(uint8_t x, uint8_t y, uint32_t color);

void wt_timeline_clear();
void wt_timeline_fill(uint32_t color);
void wt_timeline_set_pixel(uint8_t index, uint32_t color);

void wt_leds_show();
uint32_t wt_color(uint8_t r, uint8_t g, uint8_t b);
uint32_t wt_color_hsv(uint8_t h, uint8_t s, uint8_t v);  // HSV to RGB
void wt_set_brightness(uint8_t brightness);
void wt_update_brightness_auto(uint8_t minB, uint8_t maxB, uint8_t mode, uint8_t manual, bool useBlanking, uint8_t blankIntervalSecs);

// Input helpers
bool wt_button1_pressed();
bool wt_button2_pressed();
bool wt_button1_is_down();
bool wt_button2_is_down();
bool wt_cap_touch_active();
uint16_t wt_mic_read_raw();
uint16_t wt_mic_peak_to_peak();        // DMA-based peak-to-peak amplitude
uint16_t wt_mic_samples_available();   // Samples in ring buffer
uint16_t wt_mic_read_samples(uint16_t* buffer, uint16_t maxCount);  // Read from ring buffer
uint16_t wt_mic_level();
uint16_t wt_light_read_raw();
uint16_t wt_light_level();
