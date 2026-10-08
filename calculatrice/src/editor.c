/* editor.c — Le texte en cours d'écriture. */
#include "editor.h"
#include "gfx.h"
#include <string.h>

void editor_clear(editor_t *e)
{
    e->text[0] = 0;
    e->len = 0;
    e->cursor = 0;
}

bool editor_insert(editor_t *e, char const *s)
{
    size_t n = strlen(s);
    if (e->len + n > EDITOR_MAX)
        return false;
    memmove(e->text + e->cursor + n, e->text + e->cursor,
        e->len - e->cursor + 1);
    memcpy(e->text + e->cursor, s, n);
    e->len += n;
    e->cursor += n;
    return true;
}

/* Début du caractère UTF-8 qui précède [pos]. */
static size_t previous_char(editor_t const *e, size_t pos)
{
    if (pos == 0)
        return 0;
    pos--;
    while (pos > 0 && ((unsigned char)e->text[pos] & 0xc0) == 0x80)
        pos--;
    return pos;
}

void editor_backspace(editor_t *e)
{
    size_t start = previous_char(e, e->cursor);
    memmove(e->text + start, e->text + e->cursor, e->len - e->cursor + 1);
    e->len -= e->cursor - start;
    e->cursor = start;
}

void editor_left(editor_t *e)
{
    e->cursor = previous_char(e, e->cursor);
}

void editor_right(editor_t *e)
{
    if (e->cursor < e->len)
        e->cursor += gfx_utf8_length(e->text + e->cursor);
}

bool editor_blank(editor_t const *e)
{
    for (size_t i = 0; i < e->len; i++)
        if (e->text[i] != ' ')
            return false;
    return true;
}
