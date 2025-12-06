#pragma once

#include <Arduino.h>

// Sprite dimensions
#define SPRITE_DIGIT_W 3
#define SPRITE_DIGIT_H 7
#define SPRITE_ICON_W 20  // Allow full width icons
#define SPRITE_ICON_H 7

// Sprite types
enum SpriteType {
    SPRITE_DIGIT_0 = 0,
    SPRITE_DIGIT_1,
    SPRITE_DIGIT_2,
    SPRITE_DIGIT_3,
    SPRITE_DIGIT_4,
    SPRITE_DIGIT_5,
    SPRITE_DIGIT_6,
    SPRITE_DIGIT_7,
    SPRITE_DIGIT_8,
    SPRITE_DIGIT_9,
    SPRITE_ICON_SUN,
    SPRITE_ICON_CLOUD,
    SPRITE_ICON_RAIN,
    SPRITE_ICON_STORM,
    SPRITE_ICON_SNOW,
    SPRITE_ICON_WIND,
    SPRITE_ICON_WIFI,
    SPRITE_ICON_SAD,
    SPRITE_ICON_BITCOIN,
    SPRITE_COUNT
};

// Sprite data structure - 7 rows of bitmap data (uint32_t allows up to 32px width)
struct SpriteData {
    uint8_t width;
    uint8_t height;
    uint32_t rows[7];  // Max 7 rows, each row is a bitmask
};


// Initialize sprite system (load from flash)
void sprites_begin();

// Get sprite data
const SpriteData* sprites_get(SpriteType type);

// Save custom sprite to flash
bool sprites_save(SpriteType type, const SpriteData* data);

// Reset sprite to default
bool sprites_reset(SpriteType type);

// Reset all sprites to default
void sprites_reset_all();

// Get sprite as JSON string for web API
String sprites_to_json(SpriteType type);

// Parse sprite from JSON and save
bool sprites_from_json(SpriteType type, const String& json);

// Get all sprites as JSON
String sprites_all_to_json();

// Helper: Get digit sprite row bits (for rendering)
uint32_t sprites_get_digit_row(uint8_t digit, uint8_t row);

// Helper: Get icon sprite row bits (for rendering)
uint32_t sprites_get_icon_row(SpriteType icon, uint8_t row);
