/* protocol.h — Dialogue avec l'ESP32 : des lignes de texte UTF-8.
 *
 *   Envoyé           Réponse de l'ESP32
 *   PING             PONG <rssi> <heure> <réseau>   état : force du Wi-Fi en
 *                                          dBm, heure « hh:mm:ss » (« - » si
 *                                          inconnues, par ex. sans Wi-Fi) et
 *                                          nom du réseau (absent sans Wi-Fi)
 *   Q <question>     ATT, puis « R <ligne> » pour chaque ligne de la
 *                    réponse de Claude, puis FIN ; ou ERR <message>
 *   NOUV             OK (nouvelle conversation) ; ou ERR <message>
 *   SCAN             ATT, puis « W <n> <rssi> <sécurité>[*] <nom> » par
 *                    réseau Wi-Fi trouvé, puis FIN ; ou ERR <message>
 *   WIFI <n>[⇥<utilisateur>][⇥<mot de passe>]
 *                    connexion au réseau n de la dernière recherche : ATT,
 *                    puis des lignes « R » (compte rendu) et FIN ; ou ERR
 *   WIFIC <sécurité>⇥<nom>[⇥<utilisateur>][⇥<mot de passe>]
 *                    pareil pour un réseau caché
 *
 * ⇥ est une tabulation. Sécurité : O ouvert, W WEP, P mot de passe (WPA,
 * WPA2, WPA3), E nom d'utilisateur et mot de passe (Entreprise), X non pris
 * en charge ; « * » : l'ESP32 connaît déjà le mot de passe.
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
#define PROTOCOL_SCAN "SCAN"

/* Longueurs maximales, en octets : nom d'un réseau Wi-Fi, puis nom
 * d'utilisateur ou mot de passe. */
#define WIFI_SSID_MAX 32
#define WIFI_SECRET_MAX 64

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
    char ssid[WIFI_SSID_MAX + 1]; /* réseau, vide s'il n'est pas connu */
} reply_t;

/* Sécurité d'un réseau Wi-Fi. */
typedef enum {
    WIFI_OPEN,        /* O : ouvert */
    WIFI_WEP,         /* W : ancien chiffrement, mot de passe */
    WIFI_PASSWORD,    /* P : WPA, WPA2, WPA3, mot de passe */
    WIFI_ENTERPRISE,  /* E : nom d'utilisateur et mot de passe */
    WIFI_UNSUPPORTED, /* X : certificat demandé, etc. */
} wifi_security_t;

/* Un réseau trouvé par SCAN. */
typedef struct {
    int number;        /* numéro à rappeler dans WIFI <n> */
    int rssi;          /* force du signal, en dBm */
    wifi_security_t security;
    bool saved;        /* l'ESP32 connaît déjà le mot de passe */
    char ssid[WIFI_SSID_MAX + 1];
} wifi_network_t;

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

/* Lit les réseaux (lignes « W ») d'une réponse à SCAN. Renvoie leur nombre
 * (au plus [max]). */
int protocol_networks(char const *raw, wifi_network_t *list, int max);

/* Requête de connexion au réseau [number] de la dernière recherche. [user]
 * et [password] peuvent valoir NULL (champ absent). Renvoie faux si [line]
 * est trop petite : rien n'est tronqué. */
bool protocol_wifi(char *line, size_t size, int number, char const *user,
    char const *password);

/* Pareil pour un réseau caché, de nom [ssid]. */
bool protocol_wifi_hidden(char *line, size_t size, wifi_security_t security,
    char const *ssid, char const *user, char const *password);

#endif /* PROTOCOL_H */
