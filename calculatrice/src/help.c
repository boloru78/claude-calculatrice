/* help.c — Pages d'aide. */
#include "help.h"
#include "app.h"
#include "gfx.h"
#include "platform.h"
#include "ui.h"
#include <string.h>

help_page_t const HELP_PAGES[] = {
    { "Utilisation", {
        "EXE\tÉcrire",
        "▲ ▼\tDéfiler",
        "EXIT\tMenu",
        "MENU\tMenu Casio",
        "SHIFT AC/ON\tÉteindre",
    } },
    { "Clavier", {
        "Flèches\tChoisir",
        "EXE\tTaper",
        "SHIFT\tMajuscule",
        "DEL\tEffacer",
        "EXIT\tFermer",
    } },
    { "Pages du clavier", {
        "Bas à gauche : la page",
        "123 : chiffres, symboles",
        "éà : accents",
        "abc : lettres",
        "◀ ▶ : bouger le curseur",
        "Bas à droite : envoyer",
    } },
    { "Wi-Fi", {
        "EXIT, puis Wi-Fi :",
        "chercher les réseaux,",
        "puis EXE sur le tien.",
        "* : mot de passe connu",
        "Silhouette : nom",
        "d'utilisateur demandé",
    } },
    { "En haut de l'écran", {
        "À gauche : l'heure,",
        "réglée par Internet.",
        "À droite : le Wi-Fi",
        "de l'ESP32 (4 barres).",
        "Une croix : l'ESP32",
        "ne répond pas (câble ?).",
    } },
    { "Règles", {
        "Jamais en contrôle",
        "ni en examen : c'est",
        "de la triche.",
        "Les questions comptent",
        "dans l'abonnement",
        "Claude du Mac.",
    } },
    { "À propos", {
        "Claude " APP_VERSION,
        "par boloru78",
        "Licence MIT",
        "Créé avec gint/fxSDK",
        "Calculatrice, ESP32-C5",
        "et Mac : voir le README",
    } },
};

int const HELP_PAGE_COUNT = sizeof HELP_PAGES / sizeof *HELP_PAGES;

/* Dessine une ligne, avec une éventuelle seconde colonne après « \t ». */
static void draw_line(int y, char const *line)
{
    char left[32];
    char const *tab = strchr(line, '\t');
    if (!tab) {
        gfx_text(2, y, line, GFX_BLACK);
        return;
    }
    size_t n = tab - line;
    if (n >= sizeof left)
        n = sizeof left - 1;
    memcpy(left, line, n);
    left[n] = 0;
    gfx_text(2, y, left, GFX_BLACK);
    gfx_text(HELP_COLUMN_X, y, tab + 1, GFX_BLACK);
}

void help_show(void)
{
    int page = 0;
    for (;;) {
        help_page_t const *p = &HELP_PAGES[page];
        gfx_clear();
        ui_title_bar(p->title);
        if (page > 0)
            gfx_text(1, 1, "◀", GFX_WHITE);
        if (page < HELP_PAGE_COUNT - 1)
            gfx_text_at(GFX_WIDTH - 2, 1, "▶", GFX_WHITE, ALIGN_RIGHT);
        for (int i = 0; i < HELP_LINES && p->lines[i]; i++)
            draw_line(11 + i * UI_ITEM_HEIGHT, p->lines[i]);
        gfx_present();

        switch (pf_getkey(false)) {
        case IN_LEFT:
        case IN_UP:
            if (page > 0)
                page--;
            break;
        case IN_RIGHT:
        case IN_DOWN:
            if (page < HELP_PAGE_COUNT - 1)
                page++;
            break;
        case IN_EXE:
        case IN_SHIFT:
            if (page == HELP_PAGE_COUNT - 1)
                return;
            page++;
            break;
        case IN_EXIT:
            return;
        default:
            break;
        }
    }
}
