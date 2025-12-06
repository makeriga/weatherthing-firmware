#pragma once

#include <Arduino.h>

void cards_begin();
void cards_loop();
void cards_notify_wifi_connected(const char* ip);

// Card switching API
void cards_switch_to(uint8_t cardIndex);
void cards_set_preset(uint8_t preset);  // For weather/VU presets
uint8_t cards_get_current();
uint8_t cards_get_preset();
uint8_t cards_get_count();
