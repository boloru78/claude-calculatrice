/* gfx.c — Dessin dans une image 128×64 en mémoire. */
#include "gfx.h"
#include "platform.h"
#include <stdbool.h>
#include <string.h>

uint32_t gfx_vram[GFX_HEIGHT * 4];
int gfx_text_clipped = 0;

static inline int on_screen(int x, int y)
{
    return x >= 0 && y >= 0 && x < GFX_WIDTH && y < GFX_HEIGHT;
}

void gfx_clear(void)
{
    memset(gfx_vram, 0, sizeof gfx_vram);
}

void gfx_present(void)
{
    pf_present(gfx_vram);
}

void gfx_pixel(int x, int y, int color)
{
    if (!on_screen(x, y))
        return;
    uint32_t *word = &gfx_vram[(y << 2) + (x >> 5)];
    uint32_t mask = 0x80000000u >> (x & 31);

    if (color == GFX_BLACK)
        *word |= mask;
    else if (color == GFX_WHITE)
        *word &= ~mask;
    else
        *word ^= mask;
}

int gfx_get_pixel(int x, int y)
{
    if (!on_screen(x, y))
        return GFX_WHITE;
    uint32_t mask = 0x80000000u >> (x & 31);
    return (gfx_vram[(y << 2) + (x >> 5)] & mask) ? GFX_BLACK : GFX_WHITE;
}

void gfx_rect(int x1, int y1, int x2, int y2, int color)
{
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= GFX_WIDTH) x2 = GFX_WIDTH - 1;
    if (y2 >= GFX_HEIGHT) y2 = GFX_HEIGHT - 1;
    for (int y = y1; y <= y2; y++)
        for (int x = x1; x <= x2; x++)
            gfx_pixel(x, y, color);
}

void gfx_frame(int x1, int y1, int x2, int y2, int color)
{
    for (int x = x1; x <= x2; x++) {
        gfx_pixel(x, y1, color);
        if (y2 != y1)
            gfx_pixel(x, y2, color);
    }
    for (int y = y1 + 1; y < y2; y++) {
        gfx_pixel(x1, y, color);
        if (x2 != x1)
            gfx_pixel(x2, y, color);
    }
}

void gfx_sprite(int x, int y, sprite_t const *sprite, int color)
{
    for (int dy = 0; dy < sprite->h; dy++) {
        uint32_t row = sprite->rows[dy];
        for (int dx = 0; dx < sprite->w; dx++) {
            if (row & (0x80000000u >> dx))
                gfx_pixel(x + dx, y + dy, color);
        }
    }
}

//---
// Texte
//---

/* Lit le prochain point de code d'une chaîne UTF-8 et avance le pointeur.
 * Une séquence invalide donne U+FFFD, affiché comme « ? ». */
static uint32_t utf8_next(char const **text)
{
    uint8_t const *s = (uint8_t const *)*text;
    uint32_t code;
    int extra;

    if (s[0] < 0x80) { code = s[0]; extra = 0; }
    else if ((s[0] & 0xe0) == 0xc0) { code = s[0] & 0x1f; extra = 1; }
    else if ((s[0] & 0xf0) == 0xe0) { code = s[0] & 0x0f; extra = 2; }
    else if ((s[0] & 0xf8) == 0xf0) { code = s[0] & 0x07; extra = 3; }
    else { *text += 1; return 0xfffd; }

    for (int i = 1; i <= extra; i++) {
        if ((s[i] & 0xc0) != 0x80) {
            *text += i;
            return 0xfffd;
        }
        code = (code << 6) | (s[i] & 0x3f);
    }
    *text += 1 + extra;
    return code;
}

/* Trouve le caractère de la police correspondant à un point de code. */
static glyph_t const *find_glyph(uint32_t code)
{
    if (code >= 32 && code <= 126)
        return &FONT_GLYPHS[code - 32];
    /* Les caractères non ASCII sont peu nombreux : recherche simple. */
    for (int i = 95; i < FONT_GLYPH_COUNT; i++) {
        if (FONT_GLYPHS[i].code == code)
            return &FONT_GLYPHS[i];
    }
    return &FONT_GLYPHS['?' - 32];
}

static void draw_glyph(int x, int y, glyph_t const *g, int color)
{
    for (int dy = 0; dy < FONT_ROWS; dy++) {
        uint8_t row = g->rows[dy];
        for (int dx = 0; dx < g->width; dx++) {
            if (!(row & (0x80 >> dx)))
                continue;
            if (on_screen(x + dx, y + dy))
                gfx_pixel(x + dx, y + dy, color);
            else
                gfx_text_clipped++;
        }
    }
}

int gfx_utf8_length(char const *text)
{
    uint8_t c = (uint8_t)*text;
    if ((c & 0xe0) == 0xc0) return 2;
    if ((c & 0xf0) == 0xe0) return 3;
    if ((c & 0xf8) == 0xf0) return 4;
    return 1;
}

int gfx_text_n(int x, int y, char const *text, size_t len, int color)
{
    char const *end = text + len;
    bool first = true;
    while (text < end && *text) {
        glyph_t const *g = find_glyph(utf8_next(&text));
        if (!first)
            x++;
        draw_glyph(x, y, g, color);
        x += g->width;
        first = false;
    }
    return x;
}

int gfx_text_width_n(char const *text, size_t len)
{
    char const *end = text + len;
    int width = 0;
    bool first = true;
    while (text < end && *text) {
        glyph_t const *g = find_glyph(utf8_next(&text));
        width += g->width + (first ? 0 : 1);
        first = false;
    }
    return width;
}

int gfx_text(int x, int y, char const *text, int color)
{
    return gfx_text_n(x, y, text, strlen(text), color);
}

int gfx_text_width(char const *text)
{
    return gfx_text_width_n(text, strlen(text));
}

static int aligned_x(int x, char const *text, int align)
{
    if (align == ALIGN_CENTER)
        return x - gfx_text_width(text) / 2;
    if (align == ALIGN_RIGHT)
        return x - gfx_text_width(text) + 1;
    return x;
}

void gfx_text_at(int x, int y, char const *text, int color, int align)
{
    gfx_text(aligned_x(x, text, align), y, text, color);
}

void gfx_text_bold_at(int x, int y, char const *text, int color, int align)
{
    x = aligned_x(x, text, align);
    gfx_text(x, y, text, color);
    gfx_text(x + 1, y, text, color);
}
