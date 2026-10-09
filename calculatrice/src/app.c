/* app.c — L'app : écran de conversation, clavier visuel et menu.
 *
 * Écran de conversation :          Écran du clavier :
 *
 *   12:34:56  Claude   ▂▄▆█          12:34:56  Claude   ▂▄▆█
 *   > C'est quoi pi ?                ┌Et e ?|──────────────┐
 *   Pi vaut environ 3,14...          q w e r t y u i o p
 *   ...                              a s d f g h j k l '
 *   ┌─────────────────────┐          ⇧ z x c v b n m ? ⌫
 *   │ Écris ton message...│          123 ◀ espace ▶ . ↵
 *   └─────────────────────┘
 */
#include "app.h"
#include "chat.h"
#include "editor.h"
#include "gfx.h"
#include "help.h"
#include "keyboard.h"
#include "platform.h"
#include "protocol.h"
#include "status.h"
#include "ui.h"
#include "wifi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mise en page de l'écran de conversation. */
#define CHAT_TOP STATUS_HEIGHT
#define LINE_HEIGHT 8
#define CHAT_LINES 5
#define CHAT_WIDTH (GFX_WIDTH - 4)   /* la colonne de droite : défilement */
#define INPUT_TOP 51                 /* cadre de la barre de texte */

/* Écran du clavier : la barre de texte, puis le clavier. */
#define KB_INPUT_TOP STATUS_HEIGHT
#define KB_TOP (GFX_HEIGHT - KB_HEIGHT)

/* Tailles des tampons, alloués sur le tas au démarrage. */
#define CHAT_SIZE 6000
#define REPLY_SIZE APP_BUFFER_SIZE

/* Délais d'attente de l'ESP32, en millisecondes. La mini-API abandonne une
 * question après 60 s. */
#define PING_TIMEOUT_MS 600
#define QUESTION_TIMEOUT_MS 70000
#define NEW_TIMEOUT_MS 10000

#define TITLE "Claude"

static chat_t chat;
static editor_t editor;
static keyboard_t keyboard;
static status_t status;
static char *raw;         /* lignes reçues de l'ESP32 */
static char *reply_text;  /* réponse de Claude, ou message d'erreur */

static int scroll;        /* première ligne affichée */
static bool follow = true;/* la conversation suit les nouveaux messages */
static bool waiting;      /* une question est en route */

//---
// Échanges avec l'ESP32
//---

void app_poll_status(void)
{
    char buffer[80], text[2];
    reply_t r = { 0 };
    bool replied = pf_link_exchange(PROTOCOL_PING, buffer, sizeof buffer,
        PING_TIMEOUT_MS);
    if (replied)
        r = protocol_parse(buffer, text, sizeof text);
    status_update(&status, replied, &r, pf_time_ms());
}

status_t const *app_status(void)
{
    return &status;
}

char *app_raw(void)
{
    return raw;
}

char *app_text(void)
{
    return reply_text;
}

void app_info(char const *text)
{
    chat_add(&chat, CHAT_INFO, text);
    follow = true;
}

static void add_info(char const *format, char const *detail)
{
    char text[160];
    snprintf(text, sizeof text, format, detail);
    chat_add(&chat, CHAT_INFO, text);
}

//---
// Dessin
//---

typedef struct {
    int first, count;  /* lignes à dessiner */
    int y;             /* ordonnée de la première */
} chat_view_t;

static void draw_chat_line(chat_line_t const *line, int index, void *context)
{
    chat_view_t const *v = context;
    if (index < v->first || index >= v->first + v->count)
        return;
    int y = v->y + (index - v->first) * LINE_HEIGHT;
    if (line->first)
        gfx_text(0, y, chat_prefix(line->role), GFX_BLACK);
    gfx_text_n(chat_indent(line->role), y, line->text, line->len, GFX_BLACK);
}

/* Conversation, avec une barre de défilement à droite. */
static void draw_chat(void)
{
    int total = chat_layout(&chat, CHAT_WIDTH, NULL, NULL);
    int max_scroll = total > CHAT_LINES ? total - CHAT_LINES : 0;
    if (follow || scroll > max_scroll)
        scroll = max_scroll;
    if (scroll < 0)
        scroll = 0;

    chat_view_t view = { scroll, CHAT_LINES, CHAT_TOP };
    chat_layout(&chat, CHAT_WIDTH, draw_chat_line, &view);

    if (total > CHAT_LINES) {
        int height = CHAT_LINES * LINE_HEIGHT - 1;
        int thumb = height * CHAT_LINES / total;
        if (thumb < 3)
            thumb = 3;
        int top = CHAT_TOP + (height - thumb) * scroll / max_scroll;
        gfx_rect(GFX_WIDTH - 1, CHAT_TOP, GFX_WIDTH - 1,
            CHAT_TOP + height, GFX_BLACK);
        gfx_rect(GFX_WIDTH - 2, top, GFX_WIDTH - 1, top + thumb, GFX_BLACK);
    }
}

/* Texte de [e] entre [x1] et [x2], curseur visible. */
static void draw_editor(editor_t const *e, int x1, int x2, int y,
    bool cursor)
{
    int width = x2 - x1 - 2;
    /* Premier caractère affiché : le curseur doit rester visible. */
    size_t start = 0;
    while (start < e->cursor && gfx_text_width_n(e->text + start,
            e->cursor - start) > width)
        start += gfx_utf8_length(e->text + start);

    /* On n'affiche que ce qui tient. */
    size_t end = start;
    while (end < e->len) {
        size_t n = gfx_utf8_length(e->text + end);
        if (gfx_text_width_n(e->text + start, end + n - start) > width)
            break;
        end += n;
    }
    gfx_text_n(x1, y, e->text + start, end - start, GFX_BLACK);

    if (cursor) {
        int cx = x1 + gfx_text_width_n(e->text + start, e->cursor - start)
            + (e->cursor > start ? 1 : 0);
        gfx_rect(cx, y - 1, cx, y + FONT_HEIGHT, GFX_BLACK);
    }
}

/* Barre de texte en bas de l'écran de conversation. */
static void draw_input_bar(void)
{
    gfx_frame(0, INPUT_TOP, GFX_WIDTH - 1, GFX_HEIGHT - 1, GFX_BLACK);
    int y = INPUT_TOP + 3;
    if (waiting)
        gfx_text(3, y, "Claude réfléchit…", GFX_BLACK);
    else if (editor.len == 0)
        gfx_text(3, y, "Écris ton message…", GFX_BLACK);
    else
        draw_editor(&editor, 3, GFX_WIDTH - 3, y, false);
}

static void draw_main(void)
{
    gfx_clear();
    status_draw(&status, pf_time_ms(), TITLE);
    draw_chat();
    draw_input_bar();
}

void app_background(void const *context)
{
    (void)context;
    status_draw(&status, pf_time_ms(), TITLE);
    draw_chat();
    draw_input_bar();
}

static void draw_keyboard_screen(editor_t const *e, char const *title)
{
    gfx_clear();
    if (title)
        ui_title_bar(title);
    else
        status_draw(&status, pf_time_ms(), TITLE);
    gfx_frame(0, KB_INPUT_TOP, GFX_WIDTH - 1, KB_TOP - 2, GFX_BLACK);
    draw_editor(e, 2, GFX_WIDTH - 2, KB_INPUT_TOP + 2, true);
    keyboard_draw(&keyboard, KB_TOP);
}

//---
// Actions
//---

static void send_message(void)
{
    if (editor_blank(&editor))
        return;

    char line[EDITOR_MAX + 8];
    protocol_question(line, sizeof line, editor.text);
    chat_add(&chat, CHAT_USER, editor.text);
    editor_clear(&editor);
    follow = true;

    /* L'écran reste figé pendant l'échange : on affiche l'attente avant. */
    waiting = true;
    draw_main();
    gfx_present();
    bool replied = pf_link_exchange(line, raw, REPLY_SIZE,
        QUESTION_TIMEOUT_MS);
    waiting = false;

    if (!replied) {
        chat_add(&chat, CHAT_INFO,
            "L'ESP32 ne répond pas. Vérifie le câble et son alimentation.");
        return;
    }
    reply_t r = protocol_parse(raw, reply_text, REPLY_SIZE);
    if (r.kind == REPLY_ANSWER)
        chat_add(&chat, CHAT_CLAUDE, *reply_text ? reply_text : "(vide)");
    else if (r.kind == REPLY_ERROR)
        add_info("Erreur : %s", reply_text);
    else
        chat_add(&chat, CHAT_INFO, "Réponse incompréhensible de l'ESP32.");
}

bool app_edit(editor_t *e, char const *title)
{
    keyboard_reset(&keyboard);
    uint32_t drawn_second = UINT32_MAX;

    for (;;) {
        uint32_t now = pf_time_ms();
        if (now / 1000 != drawn_second) {
            draw_keyboard_screen(e, title);
            gfx_present();
            drawn_second = now / 1000;
        }

        input_t key = pf_getkey(true);
        switch (key) {
        case IN_NONE:
            /* La barre d'état reste à jour (sauf sous un titre). */
            if (!title && status_due(&status, pf_time_ms()))
                app_poll_status();
            continue;
        case IN_UP:    keyboard_move(&keyboard, 0, -1); break;
        case IN_DOWN:  keyboard_move(&keyboard, 0, 1); break;
        case IN_LEFT:  keyboard_move(&keyboard, -1, 0); break;
        case IN_RIGHT: keyboard_move(&keyboard, 1, 0); break;
        case IN_EXE:
            if (keyboard_press(&keyboard, e) == KB_SEND && !editor_blank(e))
                return true;
            break;
        case IN_SHIFT:
            /* Raccourci : comme la touche ⇧ du clavier visuel. */
            keyboard.shift = !keyboard.shift;
            break;
        case IN_DEL:
            editor_backspace(e);
            break;
        case IN_AC:
            /* editor_clear() remettrait la longueur maximale à zéro. */
            while (e->len > 0) {
                e->cursor = e->len;
                editor_backspace(e);
            }
            break;
        case IN_EXIT:
            return false;
        default:
            break;
        }
        drawn_second = UINT32_MAX; /* redessiner */
    }
}

/* Nouvelle conversation : Claude oublie tout, l'écran aussi. */
static void new_conversation(void)
{
    char text[80];
    reply_t r = { 0 };
    bool replied = pf_link_exchange(PROTOCOL_NEW, raw, REPLY_SIZE,
        NEW_TIMEOUT_MS);
    if (replied)
        r = protocol_parse(raw, text, sizeof text);

    if (replied && r.kind == REPLY_OK) {
        chat_clear(&chat);
        chat_add(&chat, CHAT_INFO, "Nouvelle conversation.");
        follow = true;
        return;
    }
    if (replied && r.kind == REPLY_ERROR) {
        add_info("Erreur : %s", text);
        follow = true;
        return;
    }
    char const *lines[] = { "L'ESP32 ne répond pas." };
    char const *items[] = { "OK" };
    ui_dialog("Impossible", lines, 1, items, 1, 0, app_background, NULL);
}

/* Menu (touche EXIT). Renvoie faux pour quitter l'app. */
static bool menu(void)
{
    char const *items[] = { "Retour", "Nouvelle conversation", "Wi-Fi",
        "Aide", "Quitter" };
    switch (ui_dialog("Menu", NULL, 0, items, 5, 0, app_background, NULL)) {
    case 1: new_conversation(); break;
    case 2: wifi_menu(); break;
    case 3: help_show(); break;
    case 4: return false;
    default: break;
    }
    return true;
}

//---
// Boucle principale
//---

/* Réserve les tampons ; en cas de mémoire limitée, on se contente de
 * moins. */
static bool allocate(void)
{
    raw = malloc(REPLY_SIZE);
    reply_text = malloc(REPLY_SIZE);
    if (!raw || !reply_text)
        return false;
    return chat_init(&chat, CHAT_SIZE) || chat_init(&chat, CHAT_SIZE / 4);
}

static void release(void)
{
    chat_free(&chat);
    free(raw);
    free(reply_text);
    raw = reply_text = NULL;
}

void app_run(void)
{
    if (!allocate()) {
        release();
        char const *lines[] = { "Mémoire insuffisante." };
        char const *items[] = { "Quitter" };
        ui_dialog("Erreur", lines, 1, items, 1, 0, NULL, NULL);
        return;
    }
    editor_clear(&editor);
    status_init(&status);
    chat_add(&chat, CHAT_INFO,
        "Bienvenue ! EXE : écrire, ▲▼ : défiler, EXIT : menu.");
    app_poll_status();

    uint32_t drawn_second = UINT32_MAX;
    for (;;) {
        uint32_t now = pf_time_ms();
        if (now / 1000 != drawn_second) {
            draw_main();
            gfx_present();
            drawn_second = now / 1000;
        }

        input_t key = pf_getkey(true);
        switch (key) {
        case IN_NONE:
            if (status_due(&status, pf_time_ms()))
                app_poll_status();
            continue;
        case IN_UP:
            follow = false;
            if (scroll > 0)
                scroll--;
            break;
        case IN_DOWN:
            scroll++;
            /* draw_chat() ramène [scroll] dans les limites ; au bout, on
             * suit de nouveau les messages. */
            if (scroll >= chat_layout(&chat, CHAT_WIDTH, NULL, NULL)
                    - CHAT_LINES)
                follow = true;
            break;
        case IN_EXE:
            if (app_edit(&editor, NULL))
                send_message();
            break;
        case IN_EXIT:
            if (!menu()) {
                release();
                return;
            }
            break;
        default:
            break;
        }
        drawn_second = UINT32_MAX; /* redessiner */
    }
}
