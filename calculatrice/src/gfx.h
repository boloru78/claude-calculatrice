/* gfx.h — Dessin dans une image 128×64 en mémoire, puis affichage.
 *
 * Le jeu dessine dans gfx_vram, qui a exactement le format de la VRAM de
 * gint ; gfx_present() la transmet à la plateforme. Sur PC, le simulateur
 * peut ainsi enregistrer chaque écran en image. */
#ifndef GFX_H
#define GFX_H

#include <stddef.h>
#include <stdint.h>
#include "assets.h"

#define GFX_WIDTH 128
#define GFX_HEIGHT 64

/* Couleurs. */
enum { GFX_WHITE = 0, GFX_BLACK = 1, GFX_INVERT = 2 };

/* Alignements pour gfx_text_at(). */
enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };

/* Image courante : 4 mots de 32 bits par ligne, bit de poids fort à gauche. */
extern uint32_t gfx_vram[GFX_HEIGHT * 4];

/* Nombre de pixels de texte tombés hors de l'écran depuis le démarrage.
 * Le simulateur s'en sert pour détecter les textes trop longs. */
extern int gfx_text_clipped;

/* Efface l'écran (tout blanc). */
void gfx_clear(void);

/* Envoie l'image courante à l'écran. */
void gfx_present(void);

/* Primitives ; les coordonnées hors de l'écran sont ignorées. */
void gfx_pixel(int x, int y, int color);
int gfx_get_pixel(int x, int y);
void gfx_rect(int x1, int y1, int x2, int y2, int color);  /* plein */
void gfx_frame(int x1, int y1, int x2, int y2, int color); /* contour */

/* Dessine les pixels noirs d'une image avec la couleur indiquée. */
void gfx_sprite(int x, int y, sprite_t const *sprite, int color);

/* Texte UTF-8 dans la police de l'app (7 pixels de haut, 1 pixel entre deux
 * caractères). gfx_text() renvoie l'abscisse à la fin du texte. Les
 * variantes _n s'arrêtent après [len] octets (un morceau de texte). */
int gfx_text(int x, int y, char const *text, int color);
int gfx_text_width(char const *text);
int gfx_text_n(int x, int y, char const *text, size_t len, int color);
int gfx_text_width_n(char const *text, size_t len);

/* Longueur en octets du caractère UTF-8 qui commence en [text] (1 à 4). */
int gfx_utf8_length(char const *text);
void gfx_text_at(int x, int y, char const *text, int color, int align);

/* Copie [src] dans [dst] (de taille [size], au moins 4), raccourci avec
 * « … » s'il est plus large que [max_width] pixels ou trop long. */
void gfx_fit(char *dst, size_t size, char const *src, int max_width);

/* Texte en gras (dessiné deux fois, décalé d'un pixel). */
void gfx_text_bold_at(int x, int y, char const *text, int color, int align);

#endif /* GFX_H */
