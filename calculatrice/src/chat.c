/* chat.c — La conversation. */
#include "chat.h"
#include "gfx.h"
#include <stdlib.h>
#include <string.h>

bool chat_init(chat_t *c, size_t size)
{
    c->buffer = malloc(size);
    c->size = c->buffer ? size : 0;
    c->used = 0;
    return c->buffer != NULL;
}

void chat_free(chat_t *c)
{
    free(c->buffer);
    c->buffer = NULL;
    c->size = c->used = 0;
}

void chat_clear(chat_t *c)
{
    c->used = 0;
}

/* Taille du premier message stocké (rôle, texte et octet nul). */
static size_t first_message_size(chat_t const *c)
{
    return 1 + strlen(c->buffer + 1) + 1;
}

void chat_add(chat_t *c, chat_role_t role, char const *text)
{
    if (c->size < 3)
        return;

    /* Un texte plus grand que tout le tampon est tronqué, sans couper un
     * caractère UTF-8 en deux. */
    size_t len = strlen(text), max = c->size - 2;
    if (len > max) {
        len = 0;
        while (text[len]) {
            size_t n = gfx_utf8_length(text + len);
            if (len + n > max)
                break;
            len += n;
        }
    }

    /* Les plus anciens messages laissent la place. */
    while (c->used + len + 2 > c->size) {
        size_t n = first_message_size(c);
        memmove(c->buffer, c->buffer + n, c->used - n);
        c->used -= n;
    }

    c->buffer[c->used] = (char)role;
    memcpy(c->buffer + c->used + 1, text, len);
    c->buffer[c->used + 1 + len] = 0;
    c->used += len + 2;
}

int chat_count(chat_t const *c)
{
    int count = 0;
    for (size_t i = 0; i < c->used; i += 1 + strlen(c->buffer + i + 1) + 1)
        count++;
    return count;
}

char const *chat_prefix(chat_role_t role)
{
    if (role == CHAT_USER)
        return "> ";
    if (role == CHAT_INFO)
        return "! ";
    return "";
}

int chat_indent(chat_role_t role)
{
    char const *prefix = chat_prefix(role);
    return *prefix ? gfx_text_width(prefix) + 1 : 0;
}

/* Découpe un message. Renvoie le nombre de lignes produites. */
static int layout_message(chat_role_t role, char const *text, int width,
    chat_visit_t visit, void *context, int index)
{
    int available = width - chat_indent(role);
    int count = 0;
    chat_line_t line = { role, true, text, 0 };

    char const *p = text;
    for (;;) {
        /* Cherche la fin de la ligne qui commence en [p], en mesurant la
         * largeur au fur et à mesure (un pixel entre deux caractères). */
        char const *q = p, *last_space = NULL;
        int w = 0;
        while (*q && *q != '\n') {
            int n = gfx_utf8_length(q);
            int next_w = w + (q > p ? 1 : 0) + gfx_text_width_n(q, n);
            if (next_w > available && q > p)
                break;
            if (*q == ' ')
                last_space = q;
            w = next_w;
            q += n;
        }

        char const *end = q, *next = q;
        if (*q && *q != '\n' && last_space) {
            /* Trop long : on coupe au dernier espace. */
            end = last_space;
            next = last_space + 1;
        } else if (*q == '\n') {
            next = q + 1;
        }

        line.text = p;
        line.len = (size_t)(end - p);
        if (visit)
            visit(&line, index + count, context);
        count++;
        line.first = false;

        if (!*q && next == q)
            break;
        p = next;
        if (!*p)
            break;
    }
    return count;
}

int chat_layout(chat_t const *c, int width, chat_visit_t visit,
    void *context)
{
    int count = 0;
    size_t i = 0;
    while (i < c->used) {
        chat_role_t role = (chat_role_t)c->buffer[i];
        char const *text = c->buffer + i + 1;
        count += layout_message(role, text, width, visit, context, count);
        i += 1 + strlen(text) + 1;
    }
    return count;
}
