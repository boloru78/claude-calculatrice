/* test_gfx.c — Dessin (gfx.c) et textes de l'aide (help.c). */
#include "test.h"
#include "gfx.h"
#include "help.h"
#include <string.h>

/* Les textes de l'aide doivent tenir dans l'écran. */
static void check_help_fits(void)
{
    for (int p = 0; p < HELP_PAGE_COUNT; p++) {
        help_page_t const *page = &HELP_PAGES[p];
        /* Titre centré entre les flèches ◀ et ▶. */
        CHECK(gfx_text_width(page->title) + 1 <= GFX_WIDTH - 2 * 8);

        for (int i = 0; i < HELP_LINES && page->lines[i]; i++) {
            char line[64];
            strncpy(line, page->lines[i], sizeof line - 1);
            line[sizeof line - 1] = 0;
            char *tab = strchr(line, '\t');
            if (tab) {
                *tab = 0;
                CHECK(2 + gfx_text_width(line) < HELP_COLUMN_X - 2);
                CHECK(HELP_COLUMN_X + gfx_text_width(tab + 1) <= GFX_WIDTH);
            } else {
                if (2 + gfx_text_width(line) > GFX_WIDTH)
                    fprintf(stderr, "ligne trop longue : %s\n", line);
                CHECK(2 + gfx_text_width(line) <= GFX_WIDTH);
            }
        }
    }
}

void test_gfx(void)
{
    /* Largeur du texte : caractères + 1 pixel d'espacement. */
    CHECK(gfx_text_width("") == 0);
    CHECK(gfx_text_width("A") == 5);
    CHECK(gfx_text_width("AB") == 11);
    CHECK(gfx_text_width("i") == 3);
    /* Les lettres accentuées sont décodées depuis l'UTF-8. */
    CHECK(gfx_text_width("é") == 5);
    CHECK(gfx_text_width("Été") == gfx_text_width("Ete"));
    /* Un caractère absent de la police est remplacé par « ? ». */
    CHECK(gfx_text_width("€") == gfx_text_width("?"));
    CHECK(gfx_text_width("\xff") == gfx_text_width("?"));

    /* Pixels : les coordonnées hors de l'écran sont ignorées. */
    gfx_clear();
    gfx_pixel(-1, 0, GFX_BLACK);
    gfx_pixel(GFX_WIDTH, 0, GFX_BLACK);
    gfx_pixel(0, GFX_HEIGHT, GFX_BLACK);
    for (int i = 0; i < GFX_HEIGHT * 4; i++)
        CHECK(gfx_vram[i] == 0);

    /* Format de la VRAM de gint : bit de poids fort = pixel de gauche. */
    gfx_pixel(0, 0, GFX_BLACK);
    CHECK(gfx_vram[0] == 0x80000000u);
    gfx_pixel(127, 63, GFX_BLACK);
    CHECK(gfx_vram[63 * 4 + 3] == 0x00000001u);
    gfx_pixel(0, 0, GFX_INVERT);
    CHECK(gfx_get_pixel(0, 0) == GFX_WHITE);

    gfx_clear();
    gfx_rect(-5, -5, 200, 200, GFX_BLACK);
    for (int i = 0; i < GFX_HEIGHT * 4; i++)
        CHECK(gfx_vram[i] == 0xffffffffu);

    /* Un texte qui dépasse est compté. */
    gfx_clear();
    int before = gfx_text_clipped;
    gfx_text(GFX_WIDTH - 2, 0, "W", GFX_BLACK);
    CHECK(gfx_text_clipped > before);
    gfx_text_clipped = before;

    /* Texte raccourci avec « … » (3 octets en UTF-8). */
    char out[16];
    gfx_fit(out, sizeof out, "court", 100);
    CHECK(!strcmp(out, "court"));
    gfx_fit(out, sizeof out, "un nom beaucoup trop long", 40);
    size_t n = strlen(out);
    CHECK(gfx_text_width(out) <= 40 && n >= 3 && !strcmp(out + n - 3, "…"));
    gfx_fit(out, 8, "abcdefghij", 200);
    CHECK(!strcmp(out, "abcd…"));
    /* 9 « é » font 18 octets : 6, puis « … », tiennent dans 16. */
    gfx_fit(out, sizeof out, "ééééééééé", 200);
    CHECK(!strcmp(out, "éééééé…"));

    check_help_fits();
}
