/* main_sim.c — Point d'entrée du simulateur PC.
 *
 * Usage : claude-sim [-o dossier] [-e faux-esp32.py] script.txt
 *   -o  dossier où écrire les captures (. par défaut)
 *   -e  le faux ESP32 (sim/esp32_simule.py par défaut)
 * Le mode du faux ESP32 se choisit avec la variable SIM_ESP32 (voir
 * sim/esp32_simule.py) ou l'instruction ESP32: des scripts. */
#include "app.h"
#include "platform.h"
#include "sim.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    sim_options_t opt = { .out_dir = ".",
        .esp32_script = "sim/esp32_simule.py" };

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc)
            opt.out_dir = argv[++i];
        else if (!strcmp(argv[i], "-e") && i + 1 < argc)
            opt.esp32_script = argv[++i];
        else if (argv[i][0] != '-' && !opt.script_path)
            opt.script_path = argv[i];
        else {
            fprintf(stderr, "usage : %s [-o dossier] [-e faux-esp32.py] "
                "script.txt\n", argv[0]);
            return 1;
        }
    }
    if (!opt.script_path) {
        fprintf(stderr, "sim : aucun script indiqué\n");
        return 1;
    }

    sim_start(&opt);
    pf_init();
    app_run();
    pf_quit();
    return sim_finish();
}
