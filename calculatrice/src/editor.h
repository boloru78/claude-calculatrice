/* editor.h — Le texte en cours d'écriture, avec son curseur. */
#ifndef EDITOR_H
#define EDITOR_H

#include <stdbool.h>
#include <stddef.h>

/* Taille maximale d'un message, en octets (la mini-API accepte 1 000
 * caractères ; les lettres accentuées prennent 2 octets). */
#define EDITOR_MAX 400

typedef struct {
    char text[EDITOR_MAX + 1]; /* UTF-8, terminé par un octet nul */
    size_t len;                /* longueur en octets */
    size_t cursor;             /* position du curseur, en octets */
    size_t limit;              /* longueur maximale, en octets */
} editor_t;

/* Vide le texte ; la longueur maximale redevient EDITOR_MAX. */
void editor_clear(editor_t *e);

/* Limite le texte à [limit] octets (au plus EDITOR_MAX), par exemple
 * 32 pour un nom de réseau Wi-Fi. */
void editor_set_limit(editor_t *e, size_t limit);

/* Insère [s] (UTF-8) au curseur. Renvoie faux s'il n'y a plus la place. */
bool editor_insert(editor_t *e, char const *s);

/* Efface le caractère avant le curseur. */
void editor_backspace(editor_t *e);

/* Déplace le curseur d'un caractère. */
void editor_left(editor_t *e);
void editor_right(editor_t *e);

/* Vrai si le texte ne contient que des espaces. */
bool editor_blank(editor_t const *e);

#endif /* EDITOR_H */
