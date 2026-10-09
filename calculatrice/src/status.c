/* status.c — Barre d'état. */
#include "status.h"
#include "gfx.h"
#include <stdio.h>
#include <string.h>

#define SECONDS_PER_DAY 86400u

void status_init(status_t *s)
{
    *s = (status_t){ 0 };
}

bool status_due(status_t const *s, uint32_t now_ms)
{
    return !s->polled || now_ms - s->last_poll_ms >= STATUS_PERIOD_MS;
}

void status_update(status_t *s, bool replied, reply_t const *r,
    uint32_t now_ms)
{
    s->polled = true;
    s->last_poll_ms = now_ms;
    s->esp_ok = replied && r->kind == REPLY_STATUS;
    if (!s->esp_ok) {
        s->wifi = false;
        s->ssid[0] = 0;
        return;
    }
    s->wifi = r->wifi;
    s->rssi = r->rssi;
    memcpy(s->ssid, r->ssid, sizeof s->ssid);
    s->ssid[sizeof s->ssid - 1] = 0;
    if (r->has_time) {
        s->has_time = true;
        s->time_ref_ms = now_ms;
        s->seconds_ref = r->hours * 3600u + r->minutes * 60u + r->seconds;
    }
}

void status_time(status_t const *s, uint32_t now_ms, char *buffer)
{
    if (!s->has_time) {
        snprintf(buffer, 9, "--:--:--");
        return;
    }
    uint32_t t = (s->seconds_ref + (now_ms - s->time_ref_ms) / 1000)
        % SECONDS_PER_DAY;
    snprintf(buffer, 9, "%02u:%02u:%02u", (unsigned)(t / 3600),
        (unsigned)(t / 60 % 60), (unsigned)(t % 60));
}

void status_draw_bars(int x, int y, int bars)
{
    static int const HEIGHTS[4] = { 2, 3, 5, 7 };
    for (int i = 0; i < 4; i++) {
        int bx = x + i * 3;
        int top = i < bars ? 7 - HEIGHTS[i] : 6;
        gfx_rect(bx, y + top, bx + 1, y + 6, GFX_BLACK);
    }
}

/* Icône Wi-Fi : les barres pleines selon la force du signal. Sans Wi-Fi,
 * seulement leur pied ; sans réponse de l'ESP32, une croix. */
static void draw_wifi(status_t const *s, int x)
{
    if (s->polled && !s->esp_ok) {
        gfx_text_at(x + 10, 0, "×", GFX_BLACK, ALIGN_RIGHT);
        return;
    }
    status_draw_bars(x, 0, s->wifi ? protocol_wifi_bars(s->rssi) : 0);
}

void status_draw(status_t const *s, uint32_t now_ms, char const *title)
{
    char time[9];
    status_time(s, now_ms, time);
    gfx_text(0, 0, time, GFX_BLACK);
    if (title)
        gfx_text_at(GFX_WIDTH / 2 + 4, 0, title, GFX_BLACK, ALIGN_CENTER);
    draw_wifi(s, GFX_WIDTH - 11);
    gfx_rect(0, STATUS_HEIGHT - 2, GFX_WIDTH - 1, STATUS_HEIGHT - 2,
        GFX_BLACK);
}
