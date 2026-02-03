#include "sprites.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

// Default digit sprites (3x7) - clean blocky style
static const SpriteData DEFAULT_DIGITS[10] PROGMEM = {
    {3, 7, {0b111, 0b101, 0b101, 0b101, 0b101, 0b101, 0b111}}, // 0 - solid rectangle with hole
    {3, 7, {0b010, 0b110, 0b010, 0b010, 0b010, 0b010, 0b111}}, // 1 - centered stem with base
    {3, 7, {0b111, 0b001, 0b001, 0b111, 0b100, 0b100, 0b111}}, // 2 - zigzag down
    {3, 7, {0b111, 0b001, 0b001, 0b111, 0b001, 0b001, 0b111}}, // 3 - three bars right
    {3, 7, {0b101, 0b101, 0b101, 0b111, 0b001, 0b001, 0b001}}, // 4 - L with descender
    {3, 7, {0b111, 0b100, 0b100, 0b111, 0b001, 0b001, 0b111}}, // 5 - zigzag opposite of 2
    {3, 7, {0b111, 0b100, 0b100, 0b111, 0b101, 0b101, 0b111}}, // 6 - C with bottom filled
    {3, 7, {0b111, 0b001, 0b001, 0b001, 0b001, 0b001, 0b001}}, // 7 - clean L shape (no diagonal)
    {3, 7, {0b111, 0b101, 0b101, 0b111, 0b101, 0b101, 0b111}}, // 8 - stacked rectangles
    {3, 7, {0b111, 0b101, 0b101, 0b111, 0b001, 0b001, 0b111}}  // 9 - reverse of 6
};

// Default icon sprites (variable width x 7) - BOLD and BIG
static const SpriteData DEFAULT_ICONS[] PROGMEM = {
    // Sun - 11x7 big radiator (Flipped & Shifted Left)
    {11, 7, {
        0b10010010000,
        0b01000100000,
        0b00011100010,
        0b01111101000,
        0b00011100010,
        0b01000100000,
        0b10010010000
    }},
    // Cloud - 13x7 wide fluffy (Flipped)
    {13, 7, {
        0b0111111111110,
        0b1111111111111, // Bottom (now top)
        0b0111111111110, // Main body
        0b0011111111100, // Side bumps
        0b0000111111000,
        0b0000001110000, // Top bump
        0b0000000000000
    }},
    // Rain - 13x7 cloud + rain (Flipped)
    {13, 7, {
        0b0010010010000, // Drops 3 (now top)
        0b0001001001000, // Drops 2
        0b0010010010000, // Drops 1
        0b0000000000000, // Gap
        0b0111111111110, // Cloud bottom
        0b0011111111100,
        0b0000111110000  // Cloud top
    }},
    // Storm - 13x7 cloud + bolt (Flipped)
    {13, 7, {
        0b0000000100000, // Tip (now top)
        0b0000001100000, // Angle
        0b0000011000000, // Angle
        0b0000001100000, // Bolt top
        0b0111111111110,
        0b0011111111100,
        0b0000111110000
    }},
    // Snow - 13x7 cloud + flakes (Flipped)
    {13, 7, {
        0b0010100010100,
        0b0001000001000,
        0b0010100010100, // Flakes
        0b0000000000000,
        0b0111111111110,
        0b0011111111100,
        0b0000111110000
    }},
    // Wind - 13x7 fast lines (Flipped)
    {13, 7, {
        0b0000011111111, // Bot line (now top)
        0b0100000000000,
        0b0011111111110, // Mid line
        0b0000000010000,
        0b1111111100000, // Top line
        0b0000000000000,
        0b0000000000000
    }},
    // WiFi - 11x7 (Flipped)
    {11, 7, {
        0b00000000000,
        0b00000100000, // Dot
        0b00000100000, // Inner
        0b00010001000,
        0b00001110000, // Mid
        0b00100000100,
        0b00011111000  // Outer
    }},
    // Sad face - 9x7 (Flipped)
    {9, 7, {
        0b001111100,
        0b010000010,
        0b100111001, // Frown
        0b100000001,
        0b101000101, // Eyes
        0b010000010,
        0b001111100
    }},
    // Bitcoin - 9x7 (Flipped)
    {9, 7, {
        0b001010100,
        0b011111000,
        0b010000100,
        0b011111000,
        0b010000100,
        0b011111000,
        0b001010100
    }}
};

// Runtime sprite storage
static SpriteData g_sprites[SPRITE_COUNT];
static bool g_initialized = false;

static const char* getSpriteFilename(SpriteType type) {
    static char filename[24];
    snprintf(filename, sizeof(filename), "/sprites/%d.bin", (int)type);
    return filename;
}

static void loadDefaults() {
    // Load digit defaults
    for (int i = 0; i < 10; i++) {
        memcpy_P(&g_sprites[i], &DEFAULT_DIGITS[i], sizeof(SpriteData));
    }
    // Load icon defaults
    for (int i = 0; i < 9; i++) {
        memcpy_P(&g_sprites[SPRITE_ICON_SUN + i], &DEFAULT_ICONS[i], sizeof(SpriteData));
    }
}

static bool loadFromFlash(SpriteType type) {
    const char* filename = getSpriteFilename(type);
    if (!LittleFS.exists(filename)) {
        return false;
    }
    
    File f = LittleFS.open(filename, "r");
    if (!f) {
        return false;
    }
    
    SpriteData data;
    size_t read = f.read((uint8_t*)&data, sizeof(SpriteData));
    f.close();
    
    if (read == sizeof(SpriteData)) {
        g_sprites[type] = data;
        return true;
    }
    return false;
}

void sprites_begin() {
    if (g_initialized) return;
    
    // Initialize LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed, formatting...");
        LittleFS.format();
        LittleFS.begin(true);
    }
    
    // Create sprites directory
    if (!LittleFS.exists("/sprites")) {
        LittleFS.mkdir("/sprites");
    }
    
    // Load defaults first
    loadDefaults();
    
    // Try to load custom sprites from flash
    for (int i = 0; i < SPRITE_COUNT; i++) {
        loadFromFlash((SpriteType)i);
    }
    
    g_initialized = true;
    Serial.println("Sprites system initialized");
}

const SpriteData* sprites_get(SpriteType type) {
    if (type >= SPRITE_COUNT) return nullptr;
    return &g_sprites[type];
}

bool sprites_save(SpriteType type, const SpriteData* data) {
    if (type >= SPRITE_COUNT || !data) return false;
    
    const char* filename = getSpriteFilename(type);
    File f = LittleFS.open(filename, "w");
    if (!f) {
        Serial.printf("Failed to open %s for writing\n", filename);
        return false;
    }
    
    size_t written = f.write((uint8_t*)data, sizeof(SpriteData));
    f.close();
    
    if (written == sizeof(SpriteData)) {
        g_sprites[type] = *data;
        Serial.printf("Saved sprite %d\n", type);
        return true;
    }
    return false;
}

bool sprites_reset(SpriteType type) {
    if (type >= SPRITE_COUNT) return false;
    
    // Delete custom file
    const char* filename = getSpriteFilename(type);
    if (LittleFS.exists(filename)) {
        LittleFS.remove(filename);
    }
    
    // Reload default
    if (type < 10) {
        memcpy_P(&g_sprites[type], &DEFAULT_DIGITS[type], sizeof(SpriteData));
    } else {
        int iconIdx = type - SPRITE_ICON_SUN;
        if (iconIdx >= 0 && iconIdx < 9) {
            memcpy_P(&g_sprites[type], &DEFAULT_ICONS[iconIdx], sizeof(SpriteData));
        }
    }
    
    return true;
}

void sprites_reset_all() {
    // Remove all custom sprite files
    File root = LittleFS.open("/sprites");
    if (root && root.isDirectory()) {
        File file = root.openNextFile();
        while (file) {
            String path = String("/sprites/") + file.name();
            file.close();
            LittleFS.remove(path);
            file = root.openNextFile();
        }
        root.close();
    }
    
    // Reload defaults
    loadDefaults();
}

String sprites_to_json(SpriteType type) {
    if (type >= SPRITE_COUNT) return "{}";
    
    const SpriteData* s = &g_sprites[type];
    JsonDocument doc;
    doc["ok"] = true;
    doc["type"] = (int)type;
    doc["width"] = s->width;
    doc["height"] = s->height;
    
    JsonArray rows = doc["rows"].to<JsonArray>();
    for (int i = 0; i < s->height; i++) {
        rows.add(s->rows[i]);
    }
    
    String output;
    serializeJson(doc, output);
    return output;
}

bool sprites_from_json(SpriteType type, const String& json) {
    if (type >= SPRITE_COUNT) return false;
    
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("JSON parse error: %s\n", err.c_str());
        return false;
    }
    
    SpriteData data;
    data.width = doc["width"] | 7;
    data.height = doc["height"] | 7;
    
    JsonArray rows = doc["rows"];
    for (int i = 0; i < 7 && i < data.height; i++) {
        data.rows[i] = rows[i].as<uint32_t>();
    }
    
    return sprites_save(type, &data);
}

String sprites_all_to_json() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    
    for (int i = 0; i < SPRITE_COUNT; i++) {
        JsonObject obj = arr.add<JsonObject>();
        const SpriteData* s = &g_sprites[i];
        obj["type"] = i;
        obj["width"] = s->width;
        obj["height"] = s->height;
        
        JsonArray rows = obj["rows"].to<JsonArray>();
        for (int j = 0; j < s->height; j++) {
            rows.add(s->rows[j]);
        }
    }
    
    String output;
    serializeJson(doc, output);
    return output;
}

uint32_t sprites_get_digit_row(uint8_t digit, uint8_t row) {
    if (digit > 9 || row >= 7) return 0;
    return g_sprites[SPRITE_DIGIT_0 + digit].rows[row];
}

uint32_t sprites_get_icon_row(SpriteType icon, uint8_t row) {
    if (icon < SPRITE_ICON_SUN || icon >= SPRITE_COUNT || row >= 7) return 0;
    return g_sprites[icon].rows[row];
}

// 5x7 letter font (A-Z), stored as 7 rows per letter, MSB first
static const uint8_t LETTER_FONT[26][7] PROGMEM = {
    {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}, // A
    {0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110}, // B
    {0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110}, // C
    {0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110}, // D
    {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111}, // E
    {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000}, // F
    {0b01110, 0b10001, 0b10000, 0b10000, 0b10011, 0b10001, 0b01110}, // G
    {0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}, // H
    {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b11111}, // I
    {0b00111, 0b00010, 0b00010, 0b00010, 0b00010, 0b10010, 0b01100}, // J
    {0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001}, // K
    {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111}, // L
    {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001}, // M
    {0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001}, // N
    {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}, // O
    {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000}, // P
    {0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101}, // Q
    {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001}, // R
    {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110}, // S
    {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100}, // T
    {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}, // U
    {0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b01010, 0b00100}, // V
    {0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b11011, 0b10001}, // W
    {0b10001, 0b01010, 0b01010, 0b00100, 0b01010, 0b01010, 0b10001}, // X
    {0b10001, 0b01010, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100}, // Y
    {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111}, // Z
};

uint8_t sprites_get_letter_row(uint8_t letter, uint8_t row) {
    if (letter >= 26 || row >= 7) return 0;
    return pgm_read_byte(&LETTER_FONT[letter][row]);
}
