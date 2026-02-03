#pragma once

#include <Arduino.h>

void custom_overlay_clear_all();
void custom_overlay_clear_matrix();
void custom_overlay_clear_timeline();
void custom_overlay_clear_text();

void custom_overlay_set_matrix_pixel(uint8_t x, uint8_t y, uint32_t color);
void custom_overlay_set_timeline_pixel(uint8_t index, uint32_t color);

void custom_overlay_set_matrix_clear_under(bool enable);
void custom_overlay_set_timeline_clear_under(bool enable);

void custom_overlay_set_matrix_timeout_ms(uint32_t now, uint32_t timeout_ms);
void custom_overlay_set_timeline_timeout_ms(uint32_t now, uint32_t timeout_ms);

uint32_t custom_overlay_default_timeout_ms();
size_t custom_overlay_text_max_len();
uint16_t custom_overlay_text_width(const char* text);

void custom_overlay_set_text(uint32_t now, const char* text, int16_t x, int16_t y, uint32_t color, uint32_t timeout_ms, bool scroll, uint16_t scroll_speed_ms, const uint32_t* per_char_colors, size_t per_char_colors_len);

void custom_overlay_apply(uint32_t now);

bool custom_overlay_matrix_active(uint32_t now);
bool custom_overlay_timeline_active(uint32_t now);
bool custom_overlay_text_active(uint32_t now);

uint32_t custom_overlay_matrix_remaining_ms(uint32_t now);
uint32_t custom_overlay_timeline_remaining_ms(uint32_t now);
uint32_t custom_overlay_text_remaining_ms(uint32_t now);
