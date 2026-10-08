/* help.h — Pages d'aide affichées depuis le menu principal ou la pause. */
#ifndef HELP_H
#define HELP_H

/* Nombre maximal de lignes par page (l'écran en affiche 6 sous le titre). */
#define HELP_LINES 6

/* Une ligne peut contenir une tabulation : ce qui suit est aligné dans une
 * seconde colonne (utile pour la liste des touches). */
typedef struct {
    char const *title;
    char const *lines[HELP_LINES];
} help_page_t;

extern help_page_t const HELP_PAGES[];
extern int const HELP_PAGE_COUNT;

/* Abscisse de la seconde colonne. */
#define HELP_COLUMN_X 68

/* Affiche l'aide jusqu'à ce que le joueur appuie sur EXIT. */
void help_show(void);

#endif /* HELP_H */
