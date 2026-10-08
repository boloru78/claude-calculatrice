/* status.h — Barre d'état : heure (réglée par l'ESP32) et force du Wi-Fi.
 *
 * L'app demande l'état à l'ESP32 toutes les STATUS_PERIOD_MS (PING). Entre
 * deux réponses, l'heure avance avec l'horloge de la calculatrice. */
#ifndef STATUS_H
#define STATUS_H

#include <stdbool.h>
#include <stdint.h>
#include "protocol.h"

#define STATUS_PERIOD_MS 10000
#define STATUS_HEIGHT 9  /* hauteur de la barre, ligne de séparation comprise */

typedef struct {
    bool polled;           /* au moins une demande d'état a été faite */
    bool esp_ok;           /* l'ESP32 a répondu à la dernière demande */
    bool wifi;             /* l'ESP32 est connecté au Wi-Fi */
    int rssi;              /* force du signal, en dBm */
    bool has_time;         /* l'heure a été reçue au moins une fois */
    uint32_t time_ref_ms;  /* moment de la réception de l'heure… */
    uint32_t seconds_ref;  /* …et heure reçue, en secondes depuis minuit */
    uint32_t last_poll_ms;
} status_t;

void status_init(status_t *s);

/* Vrai s'il est temps de redemander l'état à l'ESP32. */
bool status_due(status_t const *s, uint32_t now_ms);

/* Enregistre le résultat d'une demande d'état ([replied] faux si l'ESP32
 * n'a pas répondu). */
void status_update(status_t *s, bool replied, reply_t const *r,
    uint32_t now_ms);

/* Heure actuelle « hh:mm:ss », ou « --:--:-- » si elle n'est pas connue.
 * [buffer] fait au moins 9 octets. */
void status_time(status_t const *s, uint32_t now_ms, char *buffer);

/* Dessine la barre d'état (heure, titre, icône Wi-Fi) en haut de l'écran. */
void status_draw(status_t const *s, uint32_t now_ms, char const *title);

#endif /* STATUS_H */
