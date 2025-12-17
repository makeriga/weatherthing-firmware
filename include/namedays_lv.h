#pragma once

#include <stdint.h>

// Get all namedays for a specific date (comma-separated string)
const char* namedays_get(uint8_t month, uint8_t day);

// Get first nameday only (for small displays)
const char* namedays_get_first(uint8_t month, uint8_t day);
