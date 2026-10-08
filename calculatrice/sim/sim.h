/* sim.h — Réglages du simulateur PC. */
#ifndef SIM_H
#define SIM_H

typedef struct {
    char const *script_path;  /* suite de touches à rejouer */
    char const *out_dir;      /* dossier des captures */
    char const *esp32_script; /* le faux ESP32 (sim/esp32_simule.py) */
} sim_options_t;

void sim_start(sim_options_t const *options);
/* Libère le script ; renvoie le code de sortie du programme (2 si un texte
 * est sorti de l'écran). */
int sim_finish(void);

#endif /* SIM_H */
