/* Fichier généré par tools/gen_assets.py à partir du dossier assets/.
 * Ne pas le modifier à la main : éditer les .txt puis lancer make assets. */
#ifndef ASSETS_H
#define ASSETS_H

#include <stdint.h>

/* Hauteur d'une ligne de texte (les jambages débordent d'un pixel). */
#define FONT_HEIGHT 7
#define FONT_ROWS 8

/* Caractère de la police : chaque ligne est un octet, pixel de gauche
 * sur le bit de poids fort. */
typedef struct {
    uint32_t code;  /* point de code Unicode */
    uint8_t width;  /* largeur en pixels */
    uint8_t rows[FONT_ROWS];
} glyph_t;

/* Image : chaque ligne est un mot de 32 bits, pixel de gauche sur le
 * bit de poids fort. */
typedef struct {
    uint8_t w, h;
    uint32_t const *rows;
} sprite_t;

/* Les 95 premiers caractères sont l'ASCII imprimable (32 à 126) dans
 * l'ordre ; les suivants sont triés par point de code. */
extern glyph_t const FONT_GLYPHS[];
extern int const FONT_GLYPH_COUNT;

extern sprite_t const SPR_KEY_SHIFT;
extern sprite_t const SPR_KEY_BACKSPACE;
extern sprite_t const SPR_KEY_ENTER;
extern sprite_t const SPR_WIFI_LOCK;
extern sprite_t const SPR_WIFI_USER;

#endif /* ASSETS_H */
