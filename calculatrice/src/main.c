/* main.c — Point d'entrée de l'add-in sur la calculatrice. */
#include <gint/gint.h>
#include "app.h"
#include "platform.h"

int main(void)
{
    pf_init();
    app_run();
    pf_quit();

    /* Sans cela, le système refuse de relancer l'add-in juste après qu'on
     * l'a quitté : gint simule un redémarrage propre à la place. */
    gint_setrestart(1);
    return 1;
}
