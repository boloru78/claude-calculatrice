/* test_chat.c — Conversation (chat.c) et barre de texte (editor.c). */
#include "test.h"
#include "chat.h"
#include "editor.h"
#include "gfx.h"
#include <string.h>

typedef struct {
    int count;
    char lines[64][64];
    chat_role_t roles[64];
    bool first[64];
    int widest;
} collect_t;

static void collect(chat_line_t const *line, int index, void *context)
{
    collect_t *c = context;
    if (index < 64) {
        snprintf(c->lines[index], 64, "%.*s", (int)line->len, line->text);
        c->roles[index] = line->role;
        c->first[index] = line->first;
        int w = chat_indent(line->role)
            + gfx_text_width_n(line->text, line->len);
        if (w > c->widest)
            c->widest = w;
    }
    c->count++;
}

static void check_layout(void)
{
    chat_t chat;
    CHECK(chat_init(&chat, 1000));
    chat_add(&chat, CHAT_USER, "C'est quoi pi ?");
    chat_add(&chat, CHAT_CLAUDE, "Pi vaut environ 3,14. C'est le rapport "
        "entre le tour d'un cercle et son diamètre.\nVoilà.");
    CHECK(chat_count(&chat) == 2);

    collect_t c = { 0 };
    int total = chat_layout(&chat, 124, collect, &c);
    CHECK(total == c.count);
    CHECK(total >= 4);
    /* Aucune ligne ne dépasse la largeur, décalage compris. */
    CHECK(c.widest <= 124);
    /* Premier message : une ligne, avec « > ». */
    CHECK(c.roles[0] == CHAT_USER && c.first[0]);
    CHECK(!strcmp(c.lines[0], "C'est quoi pi ?"));
    /* On coupe entre deux mots, et le retour à la ligne est respecté. */
    CHECK(c.roles[1] == CHAT_CLAUDE && c.first[1] && !c.first[2]);
    CHECK(!strcmp(c.lines[total - 1], "Voilà."));
    for (int i = 1; i < total; i++) {
        size_t n = strlen(c.lines[i]);
        CHECK(n == 0 || (c.lines[i][0] != ' ' && c.lines[i][n - 1] != ' '));
    }

    /* Un mot plus large que l'écran est coupé au milieu. */
    chat_clear(&chat);
    chat_add(&chat, CHAT_CLAUDE, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    collect_t d = { 0 };
    CHECK(chat_layout(&chat, 124, collect, &d) >= 2);
    CHECK(d.widest <= 124);
    CHECK(strlen(d.lines[0]) + strlen(d.lines[1]) <= 42);

    /* Message vide : une ligne vide. */
    chat_clear(&chat);
    chat_add(&chat, CHAT_INFO, "");
    CHECK(chat_layout(&chat, 124, NULL, NULL) == 1);
    chat_free(&chat);
}

static void check_eviction(void)
{
    chat_t chat;
    CHECK(chat_init(&chat, 64));
    for (int i = 0; i < 20; i++) {
        char text[16];
        snprintf(text, sizeof text, "message %d", i);
        chat_add(&chat, CHAT_USER, text);
    }
    /* Les plus anciens sont partis, le dernier est là. */
    CHECK(chat.used <= chat.size);
    CHECK(chat_count(&chat) < 20);
    collect_t c = { 0 };
    chat_layout(&chat, 124, collect, &c);
    CHECK(!strcmp(c.lines[c.count - 1], "message 19"));

    /* Un texte plus grand que le tampon est tronqué sans couper « é ». */
    chat_add(&chat, CHAT_CLAUDE, "éééééééééééééééééééééééééééééééééééééééé"
        "éééééééééééééééé");
    CHECK(chat_count(&chat) == 1);
    CHECK(chat.used <= chat.size);
    CHECK(strlen(chat.buffer + 1) % 2 == 0);
    chat_free(&chat);
}

static void check_editor(void)
{
    editor_t e;
    editor_clear(&e);
    CHECK(editor_blank(&e));
    CHECK(editor_insert(&e, "ca"));
    CHECK(editor_insert(&e, "fé"));
    CHECK(!strcmp(e.text, "café") && e.cursor == e.len);

    /* Curseur et effacement, caractère par caractère (« é » = 2 octets). */
    editor_left(&e);
    CHECK(e.cursor == 3);
    editor_insert(&e, "!");
    CHECK(!strcmp(e.text, "caf!é"));
    editor_right(&e);
    CHECK(e.cursor == e.len);
    editor_backspace(&e);
    CHECK(!strcmp(e.text, "caf!"));
    editor_left(&e);
    editor_left(&e);
    editor_left(&e);
    editor_left(&e);
    editor_left(&e);
    CHECK(e.cursor == 0);
    editor_backspace(&e);
    CHECK(!strcmp(e.text, "caf!"));

    /* Pas plus de EDITOR_MAX octets. */
    editor_clear(&e);
    int n = 0;
    while (editor_insert(&e, "ab"))
        n++;
    CHECK(e.len <= EDITOR_MAX && n == EDITOR_MAX / 2);

    editor_clear(&e);
    editor_insert(&e, "   ");
    CHECK(editor_blank(&e));
}

void test_chat(void)
{
    check_layout();
    check_eviction();
    check_editor();
}
