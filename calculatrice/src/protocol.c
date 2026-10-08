/* protocol.c — Dialogue avec l'ESP32. */
#include "protocol.h"
#include "gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool starts_with(char const *s, char const *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

bool protocol_is_final(char const *line)
{
    return !strcmp(line, "FIN") || !strcmp(line, "OK")
        || starts_with(line, "PONG") || starts_with(line, "ERR");
}

/* Copie [len] octets à la fin de [dst] sans dépasser [size] ni couper un
 * caractère UTF-8 en deux. */
static void append(char *dst, size_t size, char const *src, size_t len)
{
    size_t used = strlen(dst);
    while (len > 0 && used < size - 1) {
        size_t n = gfx_utf8_length(src);
        if (n > len || used + n > size - 1)
            break;
        memcpy(dst + used, src, n);
        used += n;
        src += n;
        len -= n;
    }
    dst[used] = 0;
}

void protocol_question(char *line, size_t size, char const *question)
{
    if (size == 0)
        return;
    line[0] = 0;
    append(line, size, "Q ", 2);
    append(line, size, question, strlen(question));
    for (char *c = line; *c; c++) {
        if (*c == '\n' || *c == '\r')
            *c = ' ';
    }
}

/* « PONG <rssi> <hh:mm:ss> », chaque valeur pouvant valoir « - ». */
static void parse_status(char const *line, reply_t *r)
{
    char rssi[16] = "", time[16] = "";
    sscanf(line, "PONG %15s %15s", rssi, time);

    if (rssi[0] && strcmp(rssi, "-") != 0) {
        char *end;
        long v = strtol(rssi, &end, 10);
        if (*end == 0 && v <= 0 && v >= -127) {
            r->wifi = true;
            r->rssi = (int)v;
        }
    }

    int h, m, s;
    char extra;
    if (sscanf(time, "%d:%d:%d%c", &h, &m, &s, &extra) == 3 && h >= 0
            && h < 24 && m >= 0 && m < 60 && s >= 0 && s < 60) {
        r->has_time = true;
        r->hours = h;
        r->minutes = m;
        r->seconds = s;
    }
}

reply_t protocol_parse(char const *raw, char *text, size_t size)
{
    reply_t r = { REPLY_INVALID, false, 0, false, 0, 0, 0 };
    if (size > 0)
        text[0] = 0;
    bool first_line = true;

    while (*raw) {
        char const *end = strchr(raw, '\n');
        size_t len = end ? (size_t)(end - raw) : strlen(raw);
        char line[24];
        snprintf(line, sizeof line, "%.*s", (int)(len < 23 ? len : 23), raw);

        if (len >= 2 && raw[0] == 'R' && raw[1] == ' ') {
            /* Une ligne de la réponse de Claude. */
            if (!first_line)
                append(text, size, "\n", 1);
            append(text, size, raw + 2, len - 2);
            first_line = false;
        } else if (len == 3 && !strncmp(raw, "FIN", 3)) {
            r.kind = REPLY_ANSWER;
            return r;
        } else if (len == 2 && !strncmp(raw, "OK", 2)) {
            r.kind = REPLY_OK;
            return r;
        } else if (starts_with(line, "PONG")) {
            r.kind = REPLY_STATUS;
            parse_status(line, &r);
            return r;
        } else if (starts_with(line, "ERR")) {
            r.kind = REPLY_ERROR;
            if (size > 0)
                text[0] = 0;
            size_t skip = (len > 3 && raw[3] == ' ') ? 4 : 3;
            append(text, size, raw + skip, len - skip);
            return r;
        }
        /* « ATT » et les lignes inconnues sont ignorées. */
        raw += len + (end ? 1 : 0);
    }
    return r;
}

int protocol_wifi_bars(int rssi)
{
    if (rssi >= -55) return 4;
    if (rssi >= -65) return 3;
    if (rssi >= -75) return 2;
    if (rssi >= -85) return 1;
    return 0;
}
