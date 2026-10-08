/* platform.h — Interface entre l'app et la machine qui l'exécute.
 *
 * Tout le code de l'app est du C portable. Les seules fonctions qui dépendent
 * de la machine sont déclarées ici et implémentées deux fois :
 *   - src/platform_gint.c : la vraie calculatrice (gint / fxSDK) ;
 *   - sim/platform_sim.c  : le simulateur PC (tests, captures d'écran). */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Touches reconnues par l'app (indépendantes des codes gint). On écrit avec
 * le clavier visuel : seules les touches de navigation servent. */
typedef enum {
    IN_NONE = 0, /* aucune touche : délai écoulé (voir pf_getkey) */
    IN_RESUME,   /* retour du menu Casio ou rallumage : il faut redessiner */
    IN_UP,
    IN_DOWN,
    IN_LEFT,
    IN_RIGHT,
    IN_EXE,
    IN_SHIFT,
    IN_EXIT,
    IN_DEL,
    IN_AC,
    IN_F1,
    IN_F6,
    IN_OTHER,    /* n'importe quelle autre touche */
} input_t;

/* Durée d'un tic d'horloge, en millisecondes. */
#define PF_TICK_MS 100

/* Initialise la plateforme (minuterie, clavier). */
void pf_init(void);

/* Libère les ressources avant de quitter (ferme aussi la liaison). */
void pf_quit(void);

/* Affiche à l'écran une image 128×64 au format de gint : 4 mots de 32 bits
 * par ligne, bit de poids fort = pixel le plus à gauche, 1 = noir. */
void pf_present(uint32_t const *vram);

/* Attend une touche ; les flèches maintenues se répètent.
 * Si [timeout] est vrai, la fonction rend IN_NONE au prochain tic quand
 * aucune touche n'est pressée.
 * MENU, SHIFT+AC/ON et la mise en veille automatique sont gérées ici : la
 * fonction rend IN_RESUME quand le joueur revient dans l'app. */
input_t pf_getkey(bool timeout);

/* Temps écoulé depuis pf_init(), en millisecondes (précision : un tic). */
uint32_t pf_time_ms(void);

//---
// Liaison avec l'ESP32 (port 3 broches de la calculatrice)
//---

/* Vitesse de la liaison, la même que CALC_VITESSE dans le firmware de
 * l'ESP32. */
#define PF_LINK_BAUD 9600

/* Envoie la ligne [request] (sans « \n ») puis reçoit les lignes de la
 * réponse jusqu'à une ligne finale (voir protocol_is_final()). Les lignes
 * reçues sont copiées dans [response], séparées par « \n » et tronquées à
 * [size] octets.
 * Renvoie vrai si une ligne finale est arrivée avant [timeout_ms] ms ;
 * faux si l'ESP32 n'a pas répondu à temps (câble débranché, ESP32 éteint)
 * ou si le port série n'a pas pu s'ouvrir. L'écran n'est pas mis à jour
 * pendant l'échange : l'appelant affiche d'abord un message d'attente. */
bool pf_link_exchange(char const *request, char *response, size_t size,
    uint32_t timeout_ms);

#endif /* PLATFORM_H */
