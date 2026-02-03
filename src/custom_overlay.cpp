#include "custom_overlay.h"

#include <string.h>

#include "weatherthing_hw.h"
#include "sprites.h"

static const uint32_t DEFAULT_TIMEOUT_MS = 10000;
static const uint16_t TEXT_MAX_LEN = 96;

static uint8_t g_matrixSet[WT_MATRIX_PIXELS];
static uint32_t g_matrixColor[WT_MATRIX_PIXELS];
static uint32_t g_matrixExpiry = 0;
static bool g_matrixClearUnder = false;

static uint8_t g_timelineSet[WT_TIMELINE_PIXELS];
static uint32_t g_timelineColor[WT_TIMELINE_PIXELS];
static uint32_t g_timelineExpiry = 0;
static bool g_timelineClearUnder = false;

static char g_text[TEXT_MAX_LEN + 1];
static uint32_t g_textColors[TEXT_MAX_LEN];
static size_t g_textColorsLen = 0;
static int16_t g_textX = 0;
static int16_t g_textY = 0;
static uint32_t g_textColor = 0;
static bool g_textScroll = false;
static uint16_t g_textScrollSpeedMs = 50;
static uint32_t g_textStart = 0;
static uint32_t g_textExpiry = 0;

uint32_t custom_overlay_default_timeout_ms()
{
    return DEFAULT_TIMEOUT_MS;
}

size_t custom_overlay_text_max_len()
{
    return TEXT_MAX_LEN;
}

static bool remaining_positive(uint32_t expiry, uint32_t now)
{
    if (expiry == 0) return false;
    return (int32_t)(expiry - now) > 0;
}

static uint32_t remaining_ms(uint32_t expiry, uint32_t now)
{
    if (!remaining_positive(expiry, now)) return 0;
    return (uint32_t)(expiry - now);
}

void custom_overlay_clear_matrix()
{
    memset(g_matrixSet, 0, sizeof(g_matrixSet));
    g_matrixExpiry = 0;
    g_matrixClearUnder = false;
}

void custom_overlay_clear_timeline()
{
    memset(g_timelineSet, 0, sizeof(g_timelineSet));
    g_timelineExpiry = 0;
    g_timelineClearUnder = false;
}

void custom_overlay_clear_text()
{
    g_text[0] = '\0';
    g_textColorsLen = 0;
    g_textExpiry = 0;
}

void custom_overlay_clear_all()
{
    custom_overlay_clear_matrix();
    custom_overlay_clear_timeline();
    custom_overlay_clear_text();
}

void custom_overlay_set_matrix_timeout_ms(uint32_t now, uint32_t timeout_ms)
{
    if (timeout_ms == 0) timeout_ms = DEFAULT_TIMEOUT_MS;
    g_matrixExpiry = now + timeout_ms;
}

void custom_overlay_set_timeline_timeout_ms(uint32_t now, uint32_t timeout_ms)
{
    if (timeout_ms == 0) timeout_ms = DEFAULT_TIMEOUT_MS;
    g_timelineExpiry = now + timeout_ms;
}

void custom_overlay_set_matrix_pixel(uint8_t x, uint8_t y, uint32_t color)
{
    if (x >= WT_MATRIX_WIDTH || y >= WT_MATRIX_HEIGHT) return;
    uint16_t idx = (uint16_t)y * WT_MATRIX_WIDTH + x;
    g_matrixSet[idx] = 1;
    g_matrixColor[idx] = color;
}

void custom_overlay_set_timeline_pixel(uint8_t index, uint32_t color)
{
    if (index >= WT_TIMELINE_PIXELS) return;
    g_timelineSet[index] = 1;
    g_timelineColor[index] = color;
}

void custom_overlay_set_matrix_clear_under(bool enable)
{
    g_matrixClearUnder = enable;
}

void custom_overlay_set_timeline_clear_under(bool enable)
{
    g_timelineClearUnder = enable;
}

static uint8_t char_width(char c)
{
    if (c >= '0' && c <= '9') return 4;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return 6;
    if (c == ':') return 2;
    if (c == '.') return 2;
    if (c == '-') return 4;
    if (c == ' ') return 3;
    return 4;
}

uint16_t custom_overlay_text_width(const char* text)
{
    if (!text) return 0;
    size_t len = strlen(text);
    if (len > TEXT_MAX_LEN) len = TEXT_MAX_LEN;
    uint16_t width = 0;
    for (size_t i = 0; i < len; ++i) {
        width = (uint16_t)(width + char_width(text[i]));
    }
    return width;
}

static void draw_digit_3x7(int16_t x, int16_t yTop, uint8_t digit, uint32_t color)
{
    if (digit > 9) return;

    for (uint8_t rowTop = 0; rowTop < 7; ++rowTop) {
        uint32_t bits = sprites_get_digit_row(digit, rowTop);
        for (uint8_t col = 0; col < 3; ++col) {
            if (bits & (1U << (2 - col))) {
                int16_t px = x + col;
                int16_t py = (int16_t)(WT_MATRIX_HEIGHT - 1) - (yTop + (int16_t)rowTop);
                if (px >= 0 && px < (int16_t)WT_MATRIX_WIDTH && py >= 0 && py < (int16_t)WT_MATRIX_HEIGHT) {
                    wt_display_set_pixel_xy((uint8_t)px, (uint8_t)py, color);
                }
            }
        }
    }
}

static void draw_letter_5x7(int16_t x, int16_t yTop, uint8_t letter, uint32_t color)
{
    if (letter >= 26) return;

    for (uint8_t rowTop = 0; rowTop < 7; ++rowTop) {
        uint8_t bits = sprites_get_letter_row(letter, rowTop);
        for (uint8_t col = 0; col < 5; ++col) {
            if (bits & (1U << (4 - col))) {
                int16_t px = x + col;
                int16_t py = (int16_t)(WT_MATRIX_HEIGHT - 1) - (yTop + (int16_t)rowTop);
                if (px >= 0 && px < (int16_t)WT_MATRIX_WIDTH && py >= 0 && py < (int16_t)WT_MATRIX_HEIGHT) {
                    wt_display_set_pixel_xy((uint8_t)px, (uint8_t)py, color);
                }
            }
        }
    }
}

static void draw_punct(int16_t x, int16_t yTop, char c, uint32_t color)
{
    if (c == ':') {
        int16_t py1 = (int16_t)(WT_MATRIX_HEIGHT - 1) - (yTop + 2);
        int16_t py2 = (int16_t)(WT_MATRIX_HEIGHT - 1) - (yTop + 4);
        if (x >= 0 && x < (int16_t)WT_MATRIX_WIDTH) {
            if (py1 >= 0 && py1 < (int16_t)WT_MATRIX_HEIGHT) wt_display_set_pixel_xy((uint8_t)x, (uint8_t)py1, color);
            if (py2 >= 0 && py2 < (int16_t)WT_MATRIX_HEIGHT) wt_display_set_pixel_xy((uint8_t)x, (uint8_t)py2, color);
        }
        return;
    }

    if (c == '.') {
        int16_t py = (int16_t)(WT_MATRIX_HEIGHT - 1) - (yTop + 6);
        if (x >= 0 && x < (int16_t)WT_MATRIX_WIDTH && py >= 0 && py < (int16_t)WT_MATRIX_HEIGHT) {
            wt_display_set_pixel_xy((uint8_t)x, (uint8_t)py, color);
        }
        return;
    }

    if (c == '-') {
        int16_t py = (int16_t)(WT_MATRIX_HEIGHT - 1) - (yTop + 3);
        for (int i = 0; i < 3; ++i) {
            int16_t px = x + i;
            if (px >= 0 && px < (int16_t)WT_MATRIX_WIDTH && py >= 0 && py < (int16_t)WT_MATRIX_HEIGHT) {
                wt_display_set_pixel_xy((uint8_t)px, (uint8_t)py, color);
            }
        }
        return;
    }
}

static void draw_char(int16_t x, int16_t yTop, char c, uint32_t color)
{
    if (c >= '0' && c <= '9') {
        draw_digit_3x7(x, yTop, (uint8_t)(c - '0'), color);
        return;
    }

    if (c >= 'A' && c <= 'Z') {
        draw_letter_5x7(x, yTop, (uint8_t)(c - 'A'), color);
        return;
    }

    if (c >= 'a' && c <= 'z') {
        draw_letter_5x7(x, yTop, (uint8_t)(c - 'a'), color);
        return;
    }

    draw_punct(x, yTop, c, color);
}

void custom_overlay_set_text(uint32_t now, const char* text, int16_t x, int16_t y, uint32_t color, uint32_t timeout_ms, bool scroll, uint16_t scroll_speed_ms, const uint32_t* per_char_colors, size_t per_char_colors_len)
{
    if (!text) {
        custom_overlay_clear_text();
        return;
    }

    strncpy(g_text, text, TEXT_MAX_LEN);
    g_text[TEXT_MAX_LEN] = '\0';

    size_t len = strlen(g_text);

    g_textColorsLen = 0;
    if (per_char_colors && per_char_colors_len > 0) {
        size_t n = per_char_colors_len;
        if (n > len) n = len;
        if (n > TEXT_MAX_LEN) n = TEXT_MAX_LEN;
        for (size_t i = 0; i < n; ++i) {
            g_textColors[i] = per_char_colors[i];
        }
        g_textColorsLen = n;
    }

    g_textX = x;
    g_textY = y;
    g_textColor = color;
    g_textScroll = scroll;
    g_textScrollSpeedMs = (scroll_speed_ms == 0) ? 50 : scroll_speed_ms;
    g_textStart = now;

    if (timeout_ms == 0) timeout_ms = DEFAULT_TIMEOUT_MS;
    g_textExpiry = now + timeout_ms;
}

bool custom_overlay_matrix_active(uint32_t now)
{
    return remaining_positive(g_matrixExpiry, now);
}

bool custom_overlay_timeline_active(uint32_t now)
{
    return remaining_positive(g_timelineExpiry, now);
}

bool custom_overlay_text_active(uint32_t now)
{
    return remaining_positive(g_textExpiry, now) && g_text[0] != '\0';
}

uint32_t custom_overlay_matrix_remaining_ms(uint32_t now)
{
    return remaining_ms(g_matrixExpiry, now);
}

uint32_t custom_overlay_timeline_remaining_ms(uint32_t now)
{
    return remaining_ms(g_timelineExpiry, now);
}

uint32_t custom_overlay_text_remaining_ms(uint32_t now)
{
    return remaining_ms(g_textExpiry, now);
}

void custom_overlay_apply(uint32_t now)
{
    if (g_matrixExpiry != 0 && !remaining_positive(g_matrixExpiry, now)) {
        custom_overlay_clear_matrix();
    }
    if (g_timelineExpiry != 0 && !remaining_positive(g_timelineExpiry, now)) {
        custom_overlay_clear_timeline();
    }
    if (g_textExpiry != 0 && !remaining_positive(g_textExpiry, now)) {
        custom_overlay_clear_text();
    }

    if (!custom_overlay_matrix_active(now) && !custom_overlay_text_active(now)) {
        g_matrixClearUnder = false;
    }
    if (!custom_overlay_timeline_active(now)) {
        g_timelineClearUnder = false;
    }

    if (g_matrixClearUnder && (custom_overlay_matrix_active(now) || custom_overlay_text_active(now))) {
        wt_display_clear();
    }
    if (g_timelineClearUnder && custom_overlay_timeline_active(now)) {
        wt_timeline_clear();
    }

    if (remaining_positive(g_matrixExpiry, now)) {
        for (uint8_t y = 0; y < WT_MATRIX_HEIGHT; ++y) {
            for (uint8_t x = 0; x < WT_MATRIX_WIDTH; ++x) {
                uint16_t idx = (uint16_t)y * WT_MATRIX_WIDTH + x;
                if (g_matrixSet[idx]) {
                    wt_display_set_pixel_xy(x, y, g_matrixColor[idx]);
                }
            }
        }
    }

    if (remaining_positive(g_timelineExpiry, now)) {
        for (uint8_t i = 0; i < WT_TIMELINE_PIXELS; ++i) {
            if (g_timelineSet[i]) {
                wt_timeline_set_pixel(i, g_timelineColor[i]);
            }
        }
    }

    if (custom_overlay_text_active(now)) {
        int32_t dx = 0;
        if (g_textScroll) {
            dx = (int32_t)((now - g_textStart) / (uint32_t)g_textScrollSpeedMs);
        }
        int16_t x = g_textX - (int16_t)dx;

        size_t len = strlen(g_text);
        for (size_t i = 0; i < len; ++i) {
            char c = g_text[i];
            uint32_t col = g_textColor;
            if (i < g_textColorsLen) {
                col = g_textColors[i];
            }

            draw_char(x, g_textY, c, col);
            x += (int16_t)char_width(c);

            if (x >= (int16_t)WT_MATRIX_WIDTH + 6) {
                break;
            }
        }
    }
}
