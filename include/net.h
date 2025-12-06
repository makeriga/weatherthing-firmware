#pragma once

#include <Arduino.h>

void net_begin();
void net_loop();
bool net_is_ap_mode();
bool net_has_wifi_creds();
void net_factory_reset();
