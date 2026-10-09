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

/* Copie [src] dans [dst] (de taille [size]) sans couper un caractère. */
static void copy(char *dst, size_t size, char const *src, size_t len)
{
    dst[0] = 0;
    append(dst, size, src, len);
}

/* « PONG <rssi> <hh:mm:ss> <réseau> », chaque valeur pouvant valoir « - »,
 * le réseau pouvant manquer. [len] : longueur de la ligne. */
static void parse_status(char const *raw, size_t len, reply_t *r)
{
    char line[80];
    snprintf(line, sizeof line, "%.*s", (int)len, raw);
    char rssi[16] = "", time[16] = "";
    int end = 0;
    sscanf(line, "PONG %15s %15s%n", rssi, time, &end);

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

    /* Le nom du réseau, après une espace (il peut en contenir). */
    if (end > 0 && line[end] == ' ' && r->wifi)
        copy(r->ssid, sizeof r->ssid, line + end + 1, strlen(line + end + 1));
}

reply_t protocol_parse(char const *raw, char *text, size_t size)
{
    reply_t r = { 0 };
    r.kind = REPLY_INVALID;
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
            parse_status(raw, len, &r);
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

//---
// Wi-Fi
//---

static wifi_security_t security_from(char c)
{
    switch (c) {
    case 'O': return WIFI_OPEN;
    case 'W': return WIFI_WEP;
    case 'P': return WIFI_PASSWORD;
    case 'E': return WIFI_ENTERPRISE;
    default:  return WIFI_UNSUPPORTED;
    }
}

/* « W <n> <rssi> <sécurité>[*] <nom> ». [len] : longueur de la ligne. */
static bool parse_network(char const *raw, size_t len, wifi_network_t *n)
{
    char line[96];
    snprintf(line, sizeof line, "%.*s", (int)len, raw);
    char security[4];
    int end = 0;
    if (sscanf(line, "W %d %d %3s%n", &n->number, &n->rssi, security, &end)
            != 3 || end == 0 || n->number < 1)
        return false;
    n->security = security_from(security[0]);
    n->saved = security[1] == '*';
    /* Le nom suit une seule espace : il peut commencer par des espaces. */
    char const *ssid = line[end] == ' ' ? line + end + 1 : line + end;
    copy(n->ssid, sizeof n->ssid, ssid, strlen(ssid));
    return true;
}

int protocol_networks(char const *raw, wifi_network_t *list, int max)
{
    int count = 0;
    while (*raw && count < max) {
        char const *end = strchr(raw, '\n');
        size_t len = end ? (size_t)(end - raw) : strlen(raw);
        if (len >= 2 && raw[0] == 'W' && raw[1] == ' '
                && parse_network(raw, len, &list[count]))
            count++;
        raw += len + (end ? 1 : 0);
    }
    return count;
}

/* Ajoute [text] en entier à [line], ou renvoie faux s'il ne tient pas.
 * Pour un champ ([field] vrai), une tabulation le précède, et celles qu'il
 * contient deviennent des espaces, comme les retours à la ligne : elles
 * casseraient la requête. */
static bool add(char *line, size_t size, char const *text, bool field)
{
    size_t used = strlen(line), n = strlen(text) + (field ? 1 : 0);
    if (used + n + 1 > size)
        return false;
    if (field)
        line[used++] = '\t';
    for (; *text; text++) {
        char c = *text;
        bool bad = field && (c == '\t' || c == '\n' || c == '\r');
        line[used++] = bad ? ' ' : c;
    }
    line[used] = 0;
    return true;
}

/* Ajoute les champs non NULL. */
static bool add_credentials(char *line, size_t size, char const *user,
    char const *password)
{
    return (!user || add(line, size, user, true))
        && (!password || add(line, size, password, true));
}

bool protocol_wifi(char *line, size_t size, int number, char const *user,
    char const *password)
{
    if (size == 0)
        return false;
    char start[24];
    snprintf(start, sizeof start, "WIFI %d", number);
    line[0] = 0;
    return add(line, size, start, false)
        && add_credentials(line, size, user, password);
}

bool protocol_wifi_hidden(char *line, size_t size, wifi_security_t security,
    char const *ssid, char const *user, char const *password)
{
    static char const LETTERS[] = "OWPEX";
    if (size == 0)
        return false;
    char start[] = "WIFIC ?";
    start[6] = LETTERS[security];
    line[0] = 0;
    return add(line, size, start, false) && add(line, size, ssid, true)
        && add_credentials(line, size, user, password);
}
