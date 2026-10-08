/* keyboard.h — Le clavier visuel QWERTY, piloté avec les flèches.
 *
 * Trois pages de 4 rangées, chacune large de 10 colonnes :
 *
 *   lettres          chiffres          accents
 *   q w e r t y u i o p   1 2 3 4 5 6 7 8 9 0   é è ê ë à â ù û ô î
 *   a s d f g h j k l '   + - * / = ( ) ^ < >   ï ç « » … _ [ ] { }
 *   ⇧ z x c v b n m ? ⌫   , ; : ! % " # @ & ⌫   ⇧ ( ) ' " - ! ? , ⌫
 *   123 ◀ espace ▶ . ↵    éà ◀ espace ▶ . ↵     abc ◀ espace ▶ . ↵
 *
 * ⇧ met la lettre suivante en majuscule ; deux appuis verrouillent les
 * majuscules. La première touche de la dernière rangée passe à la page
 * suivante. ◀ et ▶ déplacent le curseur dans le texte, ↵ envoie. */
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdbool.h>
#include "editor.h"
#include "platform.h"

#define KB_ROWS 4
#define KB_UNITS 10       /* colonnes par rangée */
#define KB_KEY_WIDTH 12   /* largeur d'une colonne, en pixels */
#define KB_ROW_HEIGHT 11  /* hauteur d'une rangée, en pixels */
#define KB_HEIGHT (KB_ROWS * KB_ROW_HEIGHT)

typedef enum { KB_LETTERS, KB_SYMBOLS, KB_ACCENTS, KB_PAGE_COUNT } kb_page_t;

typedef enum {
    KBK_CHAR,      /* tape un caractère */
    KBK_SHIFT,     /* majuscule */
    KBK_BACKSPACE, /* efface le caractère avant le curseur */
    KBK_PAGE,      /* page suivante */
    KBK_LEFT,      /* curseur à gauche */
    KBK_SPACE,     /* espace */
    KBK_RIGHT,     /* curseur à droite */
    KBK_ENTER,     /* envoie le message */
} kb_kind_t;

typedef struct {
    kb_kind_t kind;
    char const *lower, *upper; /* texte tapé (KBK_CHAR), sans et avec ⇧ */
    int unit, span;            /* première colonne et largeur, en colonnes */
} kb_key_t;

typedef struct {
    kb_page_t page;
    int row, unit;   /* touche sélectionnée : rangée et première colonne */
    int want_unit;   /* colonne visée lors des déplacements verticaux */
    bool shift;      /* majuscule pour le prochain caractère */
    bool caps;       /* majuscules verrouillées */
} keyboard_t;

typedef enum { KB_NOTHING, KB_SEND } kb_result_t;

/* Page des lettres, sélection sur « q », sans majuscule. */
void keyboard_reset(keyboard_t *kb);

/* Touches d'une rangée ; renvoie leur nombre. */
int keyboard_row(kb_page_t page, int row, kb_key_t keys[KB_UNITS]);

/* Touche sélectionnée. */
kb_key_t keyboard_selected(keyboard_t const *kb);

/* Déplace la sélection (les bords bouclent). */
void keyboard_move(keyboard_t *kb, int dx, int dy);

/* Appuie sur la touche sélectionnée. Renvoie KB_SEND pour ↵. */
kb_result_t keyboard_press(keyboard_t *kb, editor_t *e);

/* Texte affiché ou tapé par une touche caractère, selon ⇧. */
char const *keyboard_text(keyboard_t const *kb, kb_key_t const *key);

/* Dessine le clavier, rangée du haut à l'ordonnée [y]. */
void keyboard_draw(keyboard_t const *kb, int y);

/* Pour le simulateur et les tests : écrit dans [keys] les touches de la
 * calculatrice (flèches et EXE) qui tapent [text] depuis keyboard_reset() ;
 * « \n » appuie sur ↵ (envoi).
 * Renvoie leur nombre, ou -1 si un caractère n'existe pas sur le clavier ou
 * si [max] est trop petit. */
int keyboard_plan(char const *text, input_t *keys, int max);

#endif /* KEYBOARD_H */
