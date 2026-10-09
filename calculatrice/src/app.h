/* app.h — L'app « Claude sur la calculatrice ». */
#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include "editor.h"
#include "status.h"

#define APP_VERSION "1.1.0"

/* Boucle principale : conversation, clavier visuel et menu, jusqu'à ce que
 * le joueur choisisse « Quitter ». */
void app_run(void);

//---
// Services de l'app pour ses autres écrans (wifi.c)
//---

/* Taille de chacun des tampons des échanges avec l'ESP32. */
#define APP_BUFFER_SIZE 3000

/* Lignes reçues de l'ESP32, et texte utile (réponse, message d'erreur). */
char *app_raw(void);
char *app_text(void);

/* Écran du clavier visuel pour écrire dans [e], sous une barre de titre
 * [title] (NULL : la barre d'état). Renvoie vrai si ↵ valide un texte non
 * vide, faux avec EXIT. */
bool app_edit(editor_t *e, char const *title);

/* Redemande l'état (Wi-Fi, heure) à l'ESP32, et le renvoie. */
void app_poll_status(void);
status_t const *app_status(void);

/* Ajoute un message de l'app (« ! ») à la conversation. */
void app_info(char const *text);

/* Fond des boîtes de dialogue : l'écran de conversation. */
void app_background(void const *context);

#endif /* APP_H */
