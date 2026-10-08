/* protocol.h — Dialogue avec l'ESP32 : des lignes de texte UTF-8.
 *
 *   Envoyé           Réponse de l'ESP32
 *   PING             PONG <rssi> <heure>   état : force du Wi-Fi en dBm et
 *                                          heure « hh:mm:ss » (« - » si
 *                                          inconnues, par ex. sans Wi-Fi)
 *   Q <question>     ATT, puis « R <ligne> » pour chaque ligne de la
 *                    réponse de Claude, puis FIN ; ou ERR <message>
 *   NOUV             OK (nouvelle conversation) ; ou ERR <message>
 *
 * Le même protocole est décrit dans le firmware de l'ESP32
 * (esp32-c5/claude_calculatrice/claude_calculatrice.ino). */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>

/* Requêtes sans paramètre. */
#define PROTOCOL_PING "PING"
#define PROTOCOL_NEW "NOUV"

typedef enum {
    REPLY_INVALID, /* rien de compréhensible */
    REPLY_ANSWER,  /* réponse de Claude, dans le texte */
    REPLY_OK,      /* commande effectuée */
    REPLY_STATUS,  /* état (PONG) */
    REPLY_ERROR,   /* erreur, message dans le texte */
} reply_kind_t;

typedef struct {
    reply_kind_t kind;
    /* Pour REPLY_STATUS : */
    bool wifi;          /* Wi-Fi connecté (rssi valable) */
    int rssi;           /* force du signal, en dBm (-30 excellent, -90 nul) */
    bool has_time;      /* heure valable */
    int hours, minutes, seconds;
} reply_t;

/* Vrai si [line] termine une réponse : FIN, OK, PONG… ou ERR…. */
bool protocol_is_final(char const *line);

/* Écrit dans [line] la requête d'une question : « Q » suivi du texte, sur
 * une seule ligne (les retours à la ligne deviennent des espaces), tronquée
 * proprement à [size] octets. */
void protocol_question(char *line, size_t size, char const *question);

/* Analyse les lignes reçues (séparées par « \n »). Le texte utile (réponse
 * de Claude ou message d'erreur) est écrit dans [text], tronqué à [size]
 * octets. */
reply_t protocol_parse(char const *raw, char *text, size_t size);

/* Nombre de barres (0 à 4) à afficher pour une force de signal en dBm. */
int protocol_wifi_bars(int rssi);

#endif /* PROTOCOL_H */
