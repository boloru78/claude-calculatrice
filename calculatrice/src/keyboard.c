/* keyboard.c — Le clavier visuel. */
#include "keyboard.h"
#include "gfx.h"
#include <string.h>

/* Abscisse de la première colonne : le clavier est centré (120 pixels). */
#define KB_X ((GFX_WIDTH - KB_UNITS * KB_KEY_WIDTH) / 2)

/* Touche caractère sans majuscule particulière, et avec. */
#define K(t)       { KBK_CHAR, t, t, 0, 1 }
#define KU(t, u)   { KBK_CHAR, t, u, 0, 1 }
#define SPECIAL(k) { k, NULL, NULL, 0, 1 }

/* Les trois premières rangées de chaque page (10 touches chacune). */
static kb_key_t const PAGES[KB_PAGE_COUNT][3][KB_UNITS] = {
    [KB_LETTERS] = {
        { KU("q", "Q"), KU("w", "W"), KU("e", "E"), KU("r", "R"),
          KU("t", "T"), KU("y", "Y"), KU("u", "U"), KU("i", "I"),
          KU("o", "O"), KU("p", "P") },
        { KU("a", "A"), KU("s", "S"), KU("d", "D"), KU("f", "F"),
          KU("g", "G"), KU("h", "H"), KU("j", "J"), KU("k", "K"),
          KU("l", "L"), K("'") },
        { SPECIAL(KBK_SHIFT), KU("z", "Z"), KU("x", "X"), KU("c", "C"),
          KU("v", "V"), KU("b", "B"), KU("n", "N"), KU("m", "M"), K("?"),
          SPECIAL(KBK_BACKSPACE) },
    },
    [KB_SYMBOLS] = {
        { K("1"), K("2"), K("3"), K("4"), K("5"), K("6"), K("7"), K("8"),
          K("9"), K("0") },
        { K("+"), K("-"), K("*"), K("/"), K("="), K("("), K(")"), K("^"),
          K("<"), K(">") },
        { K(","), K(";"), K(":"), K("!"), K("%"), K("\""), K("#"), K("@"),
          K("&"), SPECIAL(KBK_BACKSPACE) },
    },
    [KB_ACCENTS] = {
        { KU("é", "É"), K("è"), K("ê"), K("ë"), KU("à", "À"), K("â"),
          K("ù"), K("û"), K("ô"), K("î") },
        { K("ï"), K("ç"), K("«"), K("»"), K("…"), K("_"), K("["), K("]"),
          K("{"), K("}") },
        /* Les derniers symboles ASCII, utiles pour les mots de passe. */
        { SPECIAL(KBK_SHIFT), K("$"), K("~"), K("|"), K("\\"), K("`"),
          K("'"), K("?"), K(","), SPECIAL(KBK_BACKSPACE) },
    },
};

/* Dernière rangée, la même sur toutes les pages. */
static kb_key_t const LAST_ROW[] = {
    { KBK_PAGE, NULL, NULL, 0, 2 },
    { KBK_LEFT, NULL, NULL, 0, 1 },
    { KBK_SPACE, NULL, NULL, 0, 4 },
    { KBK_RIGHT, NULL, NULL, 0, 1 },
    K("."),
    { KBK_ENTER, NULL, NULL, 0, 1 },
};

/* Nom de la page suivante, affiché sur la touche de changement de page. */
static char const *const NEXT_PAGE_LABEL[KB_PAGE_COUNT] = {
    [KB_LETTERS] = "123", [KB_SYMBOLS] = "éà", [KB_ACCENTS] = "abc",
};

int keyboard_row(kb_page_t page, int row, kb_key_t keys[KB_UNITS])
{
    int count;
    if (row < 3) {
        memcpy(keys, PAGES[page][row], sizeof PAGES[page][row]);
        count = KB_UNITS;
    } else {
        count = sizeof LAST_ROW / sizeof *LAST_ROW;
        memcpy(keys, LAST_ROW, sizeof LAST_ROW);
    }
    int unit = 0;
    for (int i = 0; i < count; i++) {
        keys[i].unit = unit;
        unit += keys[i].span;
    }
    return count;
}

void keyboard_reset(keyboard_t *kb)
{
    *kb = (keyboard_t){ KB_LETTERS, 0, 0, 0, false, false };
}

/* Indice, dans sa rangée, de la touche qui couvre la colonne [unit]. */
static int key_at(kb_key_t const *keys, int count, int unit)
{
    for (int i = 0; i < count; i++)
        if (unit >= keys[i].unit && unit < keys[i].unit + keys[i].span)
            return i;
    return 0;
}

kb_key_t keyboard_selected(keyboard_t const *kb)
{
    kb_key_t keys[KB_UNITS];
    int count = keyboard_row(kb->page, kb->row, keys);
    return keys[key_at(keys, count, kb->unit)];
}

void keyboard_move(keyboard_t *kb, int dx, int dy)
{
    kb_key_t keys[KB_UNITS];
    if (dy) {
        kb->row = (kb->row + dy + KB_ROWS) % KB_ROWS;
        int count = keyboard_row(kb->page, kb->row, keys);
        kb->unit = keys[key_at(keys, count, kb->want_unit)].unit;
    }
    if (dx) {
        int count = keyboard_row(kb->page, kb->row, keys);
        int i = (key_at(keys, count, kb->unit) + dx + count) % count;
        kb->unit = kb->want_unit = keys[i].unit;
    }
}

char const *keyboard_text(keyboard_t const *kb, kb_key_t const *key)
{
    return (kb->shift || kb->caps) ? key->upper : key->lower;
}

kb_result_t keyboard_press(keyboard_t *kb, editor_t *e)
{
    kb_key_t key = keyboard_selected(kb);
    switch (key.kind) {
    case KBK_CHAR:
        editor_insert(e, keyboard_text(kb, &key));
        kb->shift = false;
        break;
    case KBK_SHIFT:
        /* Majuscule, puis verrouillage, puis retour aux minuscules. */
        if (kb->caps) {
            kb->caps = false;
        } else if (kb->shift) {
            kb->shift = false;
            kb->caps = true;
        } else {
            kb->shift = true;
        }
        break;
    case KBK_BACKSPACE:
        editor_backspace(e);
        break;
    case KBK_PAGE:
        kb->page = (kb->page + 1) % KB_PAGE_COUNT;
        kb->shift = kb->caps = false;
        break;
    case KBK_LEFT:
        editor_left(e);
        break;
    case KBK_SPACE:
        editor_insert(e, " ");
        break;
    case KBK_RIGHT:
        editor_right(e);
        break;
    case KBK_ENTER:
        return KB_SEND;
    }
    return KB_NOTHING;
}

//---
// Dessin
//---

static void draw_key(keyboard_t const *kb, kb_key_t const *key, int x, int y)
{
    int w = key->span * KB_KEY_WIDTH - 1;
    int cx = x + w / 2;
    switch (key->kind) {
    case KBK_CHAR:
        gfx_text_at(cx, y + 2, keyboard_text(kb, key), GFX_BLACK,
            ALIGN_CENTER);
        break;
    case KBK_SHIFT:
        gfx_sprite(cx - SPR_KEY_SHIFT.w / 2, y + 2, &SPR_KEY_SHIFT,
            GFX_BLACK);
        /* Souligné quand les majuscules sont actives, double si
         * verrouillées. */
        if (kb->shift || kb->caps)
            gfx_rect(cx - 3, y + 9, cx + 3, y + 9, GFX_BLACK);
        if (kb->caps)
            gfx_rect(cx - 3, y + 1, cx + 3, y + 1, GFX_BLACK);
        break;
    case KBK_BACKSPACE:
        gfx_sprite(cx - SPR_KEY_BACKSPACE.w / 2, y + 2, &SPR_KEY_BACKSPACE,
            GFX_BLACK);
        break;
    case KBK_ENTER:
        gfx_sprite(cx - SPR_KEY_ENTER.w / 2, y + 2, &SPR_KEY_ENTER,
            GFX_BLACK);
        break;
    case KBK_PAGE:
        gfx_text_at(cx, y + 2, NEXT_PAGE_LABEL[kb->page], GFX_BLACK,
            ALIGN_CENTER);
        break;
    case KBK_LEFT:
        gfx_text_at(cx, y + 2, "◀", GFX_BLACK, ALIGN_CENTER);
        break;
    case KBK_RIGHT:
        gfx_text_at(cx, y + 2, "▶", GFX_BLACK, ALIGN_CENTER);
        break;
    case KBK_SPACE:
        gfx_text_at(cx, y + 2, "espace", GFX_BLACK, ALIGN_CENTER);
        break;
    }
    /* Les touches spéciales de la dernière rangée ont un cadre. */
    if (key->kind != KBK_CHAR && key->kind != KBK_SHIFT
            && key->kind != KBK_BACKSPACE)
        gfx_frame(x, y, x + w - 1, y + KB_ROW_HEIGHT - 2, GFX_BLACK);
}

void keyboard_draw(keyboard_t const *kb, int y)
{
    kb_key_t keys[KB_UNITS];
    kb_key_t selected = keyboard_selected(kb);
    for (int row = 0; row < KB_ROWS; row++) {
        int count = keyboard_row(kb->page, row, keys);
        int ry = y + row * KB_ROW_HEIGHT;
        for (int i = 0; i < count; i++) {
            int x = KB_X + keys[i].unit * KB_KEY_WIDTH;
            draw_key(kb, &keys[i], x, ry);
            if (row == kb->row && keys[i].unit == selected.unit) {
                int w = keys[i].span * KB_KEY_WIDTH - 1;
                gfx_rect(x, ry, x + w - 1, ry + KB_ROW_HEIGHT - 2,
                    GFX_INVERT);
            }
        }
    }
}

//---
// Préparation des touches à presser (simulateur, tests)
//---

typedef struct {
    input_t *keys;
    int count, max;
    bool overflow;
} plan_t;

static void plan_key(plan_t *p, input_t key)
{
    if (p->count < p->max)
        p->keys[p->count++] = key;
    else
        p->overflow = true;
}

/* Amène la sélection sur la touche (row, unit) par le plus court chemin. */
static void plan_move(plan_t *p, keyboard_t *kb, int row, int unit)
{
    int down = (row - kb->row + KB_ROWS) % KB_ROWS;
    int dy = down <= KB_ROWS / 2 ? 1 : -1;
    while (kb->row != row) {
        keyboard_move(kb, 0, dy);
        plan_key(p, dy > 0 ? IN_DOWN : IN_UP);
    }
    kb_key_t keys[KB_UNITS];
    int count = keyboard_row(kb->page, row, keys);
    int from = key_at(keys, count, kb->unit), to = key_at(keys, count, unit);
    int right = (to - from + count) % count;
    int dx = right <= count / 2 ? 1 : -1;
    while (kb->unit != keys[to].unit) {
        keyboard_move(kb, dx, 0);
        plan_key(p, dx > 0 ? IN_RIGHT : IN_LEFT);
    }
}

static void plan_press(plan_t *p, keyboard_t *kb, int row, int unit,
    editor_t *e)
{
    plan_move(p, kb, row, unit);
    keyboard_press(kb, e);
    plan_key(p, IN_EXE);
}

/* Cherche une touche de type [kind] (et de texte [c] pour KBK_CHAR) sur la
 * page [page]. */
static bool find_key(kb_page_t page, kb_kind_t kind, char const *c,
    int *row, int *unit, bool *upper)
{
    kb_key_t keys[KB_UNITS];
    for (int r = 0; r < KB_ROWS; r++) {
        int count = keyboard_row(page, r, keys);
        for (int i = 0; i < count; i++) {
            if (keys[i].kind != kind)
                continue;
            if (kind == KBK_CHAR && strcmp(keys[i].lower, c)
                    && strcmp(keys[i].upper, c))
                continue;
            *row = r;
            *unit = keys[i].unit;
            if (upper)
                *upper = kind == KBK_CHAR && strcmp(keys[i].lower, c) != 0;
            return true;
        }
    }
    return false;
}

int keyboard_plan(char const *text, input_t *keys, int max)
{
    plan_t p = { keys, 0, max, false };
    keyboard_t kb;
    keyboard_reset(&kb);
    editor_t e;
    editor_clear(&e);

    while (*text) {
        int n = gfx_utf8_length(text);
        char c[5] = { 0 };
        memcpy(c, text, n);
        text += n;

        int row, unit;
        bool upper = false;
        if (!strcmp(c, " ") || !strcmp(c, "\n")) {
            /* Espace, ou « \n » : la touche ↵ qui envoie le message. */
            find_key(kb.page, *c == ' ' ? KBK_SPACE : KBK_ENTER, NULL, &row,
                &unit, NULL);
            plan_press(&p, &kb, row, unit, &e);
            continue;
        }
        /* Page où se trouve le caractère, en commençant par l'actuelle. */
        kb_page_t page = kb.page;
        bool found = false;
        for (int i = 0; i < KB_PAGE_COUNT && !found; i++) {
            page = (kb.page + i) % KB_PAGE_COUNT;
            found = find_key(page, KBK_CHAR, c, &row, &unit, &upper);
        }
        if (!found)
            return -1;

        int page_row, page_unit;
        find_key(kb.page, KBK_PAGE, NULL, &page_row, &page_unit, NULL);
        while (kb.page != page)
            plan_press(&p, &kb, page_row, page_unit, &e);

        if (upper != (kb.shift || kb.caps)) {
            int shift_row, shift_unit;
            if (!find_key(kb.page, KBK_SHIFT, NULL, &shift_row, &shift_unit,
                    NULL))
                return -1;
            plan_press(&p, &kb, shift_row, shift_unit, &e);
        }
        plan_press(&p, &kb, row, unit, &e);
    }
    return p.overflow ? -1 : p.count;
}
