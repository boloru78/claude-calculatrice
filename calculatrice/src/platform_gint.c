/* platform_gint.c — Implémentation de platform.h pour la calculatrice.
 *
 * C'est le seul fichier (avec main.c et link.S) qui dépend de gint. */
#include "platform.h"
#include "protocol.h"
#include <gint/display.h>
#include <gint/drivers/keydev.h>
#include <gint/gint.h>
#include <gint/keyboard.h>
#include <gint/timer.h>
#include <string.h>

/* Extinction automatique après 10 minutes sans toucher au clavier, comme le
 * système de la calculatrice (gint ne le fait pas tout seul). */
#define AUTO_POWEROFF_MS (10 * 60 * 1000)

/* Répétition des flèches maintenues : délai initial, puis intervalle. */
#define REPEAT_FIRST_US (300 * 1000)
#define REPEAT_NEXT_US (80 * 1000)

static volatile uint32_t ticks;   /* tics écoulés depuis pf_init() */
static volatile int tick_flag;    /* passe à 1 à chaque tic */
static uint32_t last_activity;    /* tic de la dernière touche pressée */
static int timer = -1;
static input_t pending = IN_NONE; /* touche mise de côté (voir SHIFT) */

/* Appelée par la minuterie toutes les PF_TICK_MS ms, sous interruption. */
static int on_tick(void)
{
    ticks++;
    tick_flag = 1;
    return TIMER_CONTINUE;
}

void pf_init(void)
{
    timer = timer_configure(TIMER_ANY, PF_TICK_MS * 1000, GINT_CALL(on_tick));
    if (timer >= 0)
        timer_start(timer);
    keydev_set_standard_repeats(keydev_std(), REPEAT_FIRST_US,
        REPEAT_NEXT_US);
}

void pf_present(uint32_t const *vram)
{
    /* Notre image a exactement le format de la VRAM de gint. */
    memcpy(gint_vram, vram, 128 * 64 / 8);
    dupdate();
}

uint32_t pf_time_ms(void)
{
    return ticks * PF_TICK_MS;
}

static void power_off(void)
{
    gint_poweroff(true);
    last_activity = ticks;
}

static input_t translate(int key)
{
    switch (key) {
    case KEY_UP:    return IN_UP;
    case KEY_DOWN:  return IN_DOWN;
    case KEY_LEFT:  return IN_LEFT;
    case KEY_RIGHT: return IN_RIGHT;
    case KEY_EXE:   return IN_EXE;
    case KEY_SHIFT: return IN_SHIFT;
    case KEY_EXIT:  return IN_EXIT;
    case KEY_DEL:   return IN_DEL;
    case KEY_ACON:  return IN_AC;
    case KEY_F1:    return IN_F1;
    case KEY_F6:    return IN_F6;
    default:        return IN_OTHER;
    }
}

/* SHIFT sert de raccourci pour les majuscules, mais SHIFT puis AC/ON doit
 * éteindre la calculatrice comme d'habitude. On attend donc de savoir ce
 * qui suit :
 *   - SHIFT relâchée seule : c'est bien un appui sur SHIFT ;
 *   - AC/ON pendant que SHIFT est enfoncée : extinction ;
 *   - autre touche : on rend SHIFT et on garde l'autre touche pour après. */
static input_t resolve_shift(void)
{
    for (;;) {
        tick_flag = 0;
        key_event_t e = waitevent(&tick_flag);
        if (e.type == KEYEV_UP && e.key == KEY_SHIFT)
            return IN_SHIFT;
        if (e.type != KEYEV_DOWN)
            continue;
        if (e.key == KEY_ACON) {
            power_off();
            return IN_RESUME;
        }
        pending = translate(e.key);
        return IN_SHIFT;
    }
}

input_t pf_getkey(bool timeout)
{
    if (pending != IN_NONE) {
        input_t key = pending;
        pending = IN_NONE;
        return key;
    }

    for (;;) {
        tick_flag = 0;
        key_event_t e = getkey_opt(GETKEY_REP_ARROWS, &tick_flag);

        if (e.type == KEYEV_NONE) {
            if ((ticks - last_activity) * PF_TICK_MS >= AUTO_POWEROFF_MS) {
                power_off();
                return IN_RESUME;
            }
            if (timeout)
                return IN_NONE;
            continue;
        }

        last_activity = ticks;
        switch (e.key) {
        case KEY_MENU:
            gint_osmenu();
            last_activity = ticks;
            return IN_RESUME;
        case KEY_SHIFT:
            return resolve_shift();
        default:
            return translate(e.key);
        }
    }
}

//---
// Liaison avec l'ESP32
//
// gint ne pilote pas le port série : gint_world_switch() rend la main au
// système le temps d'un échange complet (envoi de la requête, réception de
// la réponse), et link_exchange() utilise ses fonctions (link.S). L'écran
// reste figé pendant ce temps.
//---

int Serial_Open(unsigned char *mode);
int Serial_Close(int mode);
int Serial_ReadOneByte(unsigned char *byte);
int Serial_BufferedTransmitOneByte(unsigned char byte);
int Serial_ClearReceiveBuffer(void);
int RTC_GetTicks(void);

/* Code de vitesse de Serial_Open() : 0 = 300 bauds… 9 = 115200 bauds. */
static unsigned char baud_code(int baud)
{
    static int const RATES[] = { 300, 600, 1200, 2400, 4800, 9600, 19200,
        38400, 57600, 115200 };
    for (int i = 0; i < 10; i++)
        if (RATES[i] == baud)
            return i;
    return 5; /* 9600 */
}

/* Paramètres de l'échange en cours. */
static struct {
    char const *request;
    char *response;
    size_t size;
    uint32_t timeout_ms;
} link;

/* Temps écoulé depuis [start], en millisecondes. On utilise l'horloge du
 * système (tics de 1/128 s) : la minuterie de gint ne tourne pas pendant
 * l'échange. */
static uint32_t elapsed_ms(int start)
{
    return (uint32_t)(RTC_GetTicks() - start) * 1000 / 128;
}

/* Exécutée hors de gint. Renvoie 1 si une ligne finale est arrivée. */
static int link_exchange(void)
{
    /* 8 bits, sans parité, 1 bit de stop. Déjà ouvert : code 3. */
    unsigned char mode[6] = { 0, baud_code(PF_LINK_BAUD), 0, 0, 0, 0 };
    int opened = Serial_Open(mode);
    if (opened != 0 && opened != 3)
        return 0;
    Serial_ClearReceiveBuffer();
    int start = RTC_GetTicks();

    /* Envoi de la requête, puis fin de ligne. */
    for (char const *c = link.request; ; c++) {
        unsigned char byte = *c ? (unsigned char)*c : '\n';
        while (Serial_BufferedTransmitOneByte(byte) != 0) {
            if (elapsed_ms(start) > link.timeout_ms)
                return 0;
        }
        if (!*c)
            break;
    }

    /* Réception, ligne par ligne, jusqu'à une ligne finale. Le début de
     * la ligne en cours est aussi gardé à part : même si la réponse
     * dépasse le tampon, on reconnaît la ligne finale. */
    size_t used = 0;
    bool truncated = false;
    char line[24];
    size_t line_len = 0;
    while (elapsed_ms(start) <= link.timeout_ms) {
        unsigned char byte;
        if (Serial_ReadOneByte(&byte) != 0 || byte == '\r')
            continue;
        if (byte == '\n') {
            line[line_len] = 0;
            line_len = 0;
            if (protocol_is_final(line)) {
                /* Réponse tronquée : on remet la ligne finale à la fin du
                 * tampon pour que protocol_parse() la trouve. */
                size_t n = strlen(line);
                if (truncated && link.size >= n + 2) {
                    size_t pos = link.size - n - 2;
                    link.response[pos] = '\n';
                    memcpy(link.response + pos + 1, line, n + 1);
                }
                return 1;
            }
            byte = '\n';
        } else if (line_len + 1 < sizeof line) {
            line[line_len++] = byte;
        }
        if (used + 1 < link.size)
            link.response[used++] = byte;
        else
            truncated = true;
        link.response[used] = 0;
    }
    return 0;
}

bool pf_link_exchange(char const *request, char *response, size_t size,
    uint32_t timeout_ms)
{
    if (size == 0)
        return false;
    link.request = request;
    link.response = response;
    link.size = size;
    link.timeout_ms = timeout_ms;
    response[0] = 0;
    bool ok = gint_world_switch(GINT_CALL(link_exchange)) == 1;
    last_activity = ticks;
    return ok;
}

static int link_close(void)
{
    Serial_Close(1);
    return 0;
}

void pf_quit(void)
{
    gint_world_switch(GINT_CALL(link_close));
    if (timer >= 0)
        timer_stop(timer);
    timer = -1;
}
