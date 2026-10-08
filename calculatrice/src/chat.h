/* chat.h — La conversation : messages et découpage en lignes d'écran.
 *
 * Style terminal : tes messages commencent par « > », ceux de Claude sont
 * affichés tels quels, les messages de l'app (erreurs, aide) par « ! ». Les
 * lignes trop longues sont coupées entre deux mots, au pixel près, avec la
 * police de l'app. */
#ifndef CHAT_H
#define CHAT_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    CHAT_USER,   /* message envoyé à Claude */
    CHAT_CLAUDE, /* réponse de Claude */
    CHAT_INFO,   /* message de l'app : erreur, bienvenue… */
} chat_role_t;

/* Les messages se suivent dans un seul tampon : un octet de rôle, le texte
 * UTF-8, puis un octet nul. Quand il est plein, les plus anciens
 * disparaissent. Le tampon est alloué sur le tas (la mémoire statique d'un
 * add-in est limitée à quelques Ko). */
typedef struct {
    char *buffer;
    size_t size, used;
} chat_t;

/* Une ligne d'écran. */
typedef struct {
    chat_role_t role;
    bool first;       /* première ligne du message (porte « > » ou « ! ») */
    char const *text; /* début du texte de la ligne, dans le tampon */
    size_t len;       /* longueur en octets */
} chat_line_t;

typedef void (*chat_visit_t)(chat_line_t const *line, int index,
    void *context);

/* Alloue le tampon. Renvoie faux si la mémoire manque. */
bool chat_init(chat_t *c, size_t size);
void chat_free(chat_t *c);

/* Efface tous les messages. */
void chat_clear(chat_t *c);

/* Ajoute un message (tronqué s'il est plus grand que tout le tampon). */
void chat_add(chat_t *c, chat_role_t role, char const *text);

/* Nombre de messages. */
int chat_count(chat_t const *c);

/* Préfixe affiché devant la première ligne d'un message (« > », « ! » ou
 * rien) et décalage en pixels du texte de toutes ses lignes. */
char const *chat_prefix(chat_role_t role);
int chat_indent(chat_role_t role);

/* Découpe la conversation en lignes d'au plus [width] pixels (décalage
 * compris) et appelle [visit] pour chacune, dans l'ordre (s'il n'est pas
 * NULL). Renvoie le nombre de lignes. */
int chat_layout(chat_t const *c, int width, chat_visit_t visit,
    void *context);

#endif /* CHAT_H */
