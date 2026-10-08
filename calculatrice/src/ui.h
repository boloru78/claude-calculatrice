/* ui.h — Éléments d'interface : barre de titre, listes, boîtes de dialogue. */
#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include "platform.h"

/* Hauteur d'une ligne de liste, en pixels. */
#define UI_ITEM_HEIGHT 9

/* Barre de titre inversée en haut de l'écran (9 pixels de haut). */
void ui_title_bar(char const *title);

/* Lignes d'une liste à partir de l'ordonnée [y], entre les abscisses x1 et
 * x2 ; l'élément [selected] est surligné. */
void ui_items(int x1, int x2, int y, char const *const *items, int count,
    int selected);

/* Réaction d'une liste à une touche. */
typedef enum {
    UI_NONE,   /* rien à faire (ou sélection déplacée) */
    UI_CHOOSE, /* EXE ou SHIFT : l'élément sélectionné est choisi */
    UI_CANCEL, /* EXIT : retour */
} ui_action_t;

ui_action_t ui_list_input(input_t key, int *selected, int count);

/* Fond dessiné derrière une boîte de dialogue (NULL pour un écran vide). */
typedef void (*ui_background_t)(void const *context);

/* Boîte de dialogue modale : un titre, quelques lignes de texte et une liste
 * de choix. Renvoie l'indice choisi, ou -1 si le joueur appuie sur EXIT. */
int ui_dialog(char const *title, char const *const *lines, int line_count,
    char const *const *items, int item_count, int selected,
    ui_background_t background, void const *context);

/* Demande une confirmation. Renvoie vrai si le joueur accepte. */
bool ui_confirm(char const *title, char const *line1, char const *line2,
    char const *yes, char const *no, ui_background_t background,
    void const *context);

#endif /* UI_H */
