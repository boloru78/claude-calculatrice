/* test_keyboard.c — Clavier visuel (keyboard.c) et barre d'état (status.c). */
#include "test.h"
#include "keyboard.h"
#include "status.h"
#include <string.h>

/* Rejoue des touches sur un clavier tout juste ouvert, comme l'app. */
static void replay(input_t const *keys, int count, editor_t *e)
{
    keyboard_t kb;
    keyboard_reset(&kb);
    editor_clear(e);
    for (int i = 0; i < count; i++) {
        switch (keys[i]) {
        case IN_UP:    keyboard_move(&kb, 0, -1); break;
        case IN_DOWN:  keyboard_move(&kb, 0, 1); break;
        case IN_LEFT:  keyboard_move(&kb, -1, 0); break;
        case IN_RIGHT: keyboard_move(&kb, 1, 0); break;
        case IN_EXE:   keyboard_press(&kb, e); break;
        default: break;
        }
    }
}

static void check_layout(void)
{
    /* Chaque rangée couvre exactement les 10 colonnes. */
    for (int page = 0; page < KB_PAGE_COUNT; page++) {
        for (int row = 0; row < KB_ROWS; row++) {
            kb_key_t keys[KB_UNITS];
            int count = keyboard_row(page, row, keys);
            int units = 0;
            for (int i = 0; i < count; i++)
                units += keys[i].span;
            CHECK(units == KB_UNITS);
        }
    }
    /* Le clavier tient sous la barre d'état et la barre de texte. */
    CHECK(STATUS_HEIGHT + 10 + KB_HEIGHT <= 64);
}

static void check_navigation(void)
{
    keyboard_t kb;
    keyboard_reset(&kb);
    CHECK(!strcmp(keyboard_selected(&kb).lower, "q"));

    /* Les bords bouclent. */
    keyboard_move(&kb, -1, 0);
    CHECK(!strcmp(keyboard_selected(&kb).lower, "p"));
    keyboard_move(&kb, 1, 0);
    keyboard_move(&kb, 0, -1);
    CHECK(keyboard_selected(&kb).kind == KBK_PAGE);

    /* En descendant de « b » sur la barre d'espace, puis en remontant, on
     * retrouve « b » (la colonne visée est gardée). */
    keyboard_reset(&kb);
    keyboard_move(&kb, 0, 2);
    for (int i = 0; i < 5; i++)
        keyboard_move(&kb, 1, 0);
    CHECK(!strcmp(keyboard_selected(&kb).lower, "b"));
    keyboard_move(&kb, 0, 1);
    CHECK(keyboard_selected(&kb).kind == KBK_SPACE);
    keyboard_move(&kb, 0, -1);
    CHECK(!strcmp(keyboard_selected(&kb).lower, "b"));
}

static void check_press(void)
{
    keyboard_t kb;
    editor_t e;
    keyboard_reset(&kb);
    editor_clear(&e);

    /* ⇧ : une majuscule, puis verrouillage, puis minuscules. */
    keyboard_move(&kb, 0, 2);  /* ⇧ */
    keyboard_press(&kb, &e);
    CHECK(kb.shift);
    keyboard_move(&kb, 1, 0);  /* z */
    keyboard_press(&kb, &e);
    keyboard_press(&kb, &e);
    CHECK(!strcmp(e.text, "Zz"));
    keyboard_move(&kb, -1, 0);
    keyboard_press(&kb, &e);
    keyboard_press(&kb, &e);
    CHECK(kb.caps);
    keyboard_move(&kb, 1, 0);
    keyboard_press(&kb, &e);
    keyboard_press(&kb, &e);
    CHECK(!strcmp(e.text, "ZzZZ"));

    /* ⌫ efface, ↵ envoie. */
    keyboard_move(&kb, -2, 0); /* ⌫ (boucle par la gauche) */
    CHECK(keyboard_selected(&kb).kind == KBK_BACKSPACE);
    CHECK(keyboard_press(&kb, &e) == KB_NOTHING);
    CHECK(!strcmp(e.text, "ZzZ"));
    keyboard_move(&kb, 0, 1);
    CHECK(keyboard_selected(&kb).kind == KBK_ENTER);
    CHECK(keyboard_press(&kb, &e) == KB_SEND);

    /* La touche de page fait le tour des 3 pages. */
    keyboard_reset(&kb);
    keyboard_move(&kb, 0, -1);
    for (int i = 0; i < KB_PAGE_COUNT; i++) {
        CHECK((int)kb.page == i);
        keyboard_press(&kb, &e);
    }
    CHECK(kb.page == KB_LETTERS);
}

/* Tout ce qu'on doit pouvoir écrire est atteignable aux flèches. */
static void check_plan(void)
{
    static input_t keys[4096];
    char const *texts[] = {
        "Salut, c'est quoi pi ?",
        "Calcule 3*(4+5)^2 = ?",
        "Où est l'élève ? À côté « ici »… {ok} [1] #2 @3 & 50 %",
        "QWERTY azerty 0123456789 +-*/=()^<> ,;:!\"_",
        "éèêëàâùûôîïç É À",
    };
    for (size_t t = 0; t < sizeof texts / sizeof *texts; t++) {
        int count = keyboard_plan(texts[t], keys, 4096);
        CHECK(count > 0);
        editor_t e;
        replay(keys, count, &e);
        if (strcmp(e.text, texts[t]))
            fprintf(stderr, "tapé « %s » au lieu de « %s »\n", e.text,
                texts[t]);
        CHECK(!strcmp(e.text, texts[t]));
    }
    /* Caractère absent du clavier. */
    CHECK(keyboard_plan("€", keys, 4096) == -1);
    /* Trop de touches pour le tableau. */
    CHECK(keyboard_plan("pppppppppp", keys, 3) == -1);
}

static void check_status(void)
{
    status_t s;
    status_init(&s);
    char time[9];
    status_time(&s, 0, time);
    CHECK(!strcmp(time, "--:--:--"));
    CHECK(status_due(&s, 0));

    reply_t r = { REPLY_STATUS, true, -60, true, 23, 59, 58 };
    status_update(&s, true, &r, 1000);
    CHECK(s.esp_ok && s.wifi && !status_due(&s, 5000));
    CHECK(status_due(&s, 1000 + STATUS_PERIOD_MS));
    status_time(&s, 1000, time);
    CHECK(!strcmp(time, "23:59:58"));
    /* L'heure avance seule, et passe minuit. */
    status_time(&s, 4500, time);
    CHECK(!strcmp(time, "00:00:01"));

    /* L'ESP32 ne répond plus : croix, mais l'heure continue. */
    status_update(&s, false, &r, 20000);
    CHECK(!s.esp_ok && !s.wifi && s.has_time);
    status_time(&s, 20000, time);
    CHECK(!strcmp(time, "00:00:17"));
}

void test_keyboard(void)
{
    check_layout();
    check_navigation();
    check_press();
    check_plan();
    check_status();
}
