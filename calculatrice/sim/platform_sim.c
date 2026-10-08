/* platform_sim.c — Implémentation de platform.h pour le simulateur PC.
 *
 * Le simulateur n'a pas de fenêtre : il lit une suite de touches dans un
 * script et enregistre les écrans demandés en images PBM (voir sim/README.md
 * pour la syntaxe des scripts). Le temps est simulé : chaque touche fait
 * avancer l'horloge, ce qui rend les captures parfaitement reproductibles.
 *
 * La liaison avec l'ESP32 est confiée à sim/esp32_simule.py, un faux ESP32
 * qui parle le même protocole (réponses toutes faites, ou vraie mini-API du
 * Mac). */
#include "platform.h"
#include "sim.h"
#include "gfx.h"
#include "keyboard.h"
#include "protocol.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Image affichée en dernier. */
static uint32_t screen[GFX_HEIGHT * 4];

static sim_options_t opt;
static char *script;         /* contenu du script */
static char *cursor;         /* position de lecture */
static uint32_t now_ms;      /* horloge simulée */
static uint32_t key_delay_ms = 250;
static int wait_ticks;       /* tics d'attente restants (WAIT:n) */

/* Touches préparées par TEXTE:, à rendre une par une. */
static input_t planned[4096];
static int planned_count, planned_next;

/* Enregistrement d'une animation (REC:on / REC:off). */
static bool recording;
static int frame_count;
static FILE *frame_index;

static void write_pbm(char const *path, uint32_t const *vram)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "sim : impossible d'écrire %s\n", path);
        exit(1);
    }
    fprintf(fp, "P4\n%d %d\n", GFX_WIDTH, GFX_HEIGHT);
    for (int i = 0; i < GFX_HEIGHT * 4; i++) {
        uint8_t bytes[4] = { vram[i] >> 24, vram[i] >> 16, vram[i] >> 8,
            vram[i] };
        fwrite(bytes, 1, 4, fp);
    }
    fclose(fp);
}

void sim_start(sim_options_t const *options)
{
    opt = *options;

    FILE *fp = fopen(opt.script_path, "rb");
    if (!fp) {
        fprintf(stderr, "sim : script introuvable : %s\n", opt.script_path);
        exit(1);
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    script = calloc(size + 1, 1);
    if (!script || fread(script, 1, size, fp) != (size_t)size) {
        fprintf(stderr, "sim : lecture impossible : %s\n", opt.script_path);
        exit(1);
    }
    fclose(fp);
    cursor = script;
}

int sim_finish(void)
{
    if (frame_index)
        fclose(frame_index);
    frame_index = NULL;
    free(script);
    script = cursor = NULL;

    /* Un texte trop long pour l'écran est une erreur. */
    if (gfx_text_clipped > 0) {
        fprintf(stderr, "sim : %d pixels de texte hors de l'écran\n",
            gfx_text_clipped);
        return 2;
    }
    return 0;
}

void pf_init(void) {}
void pf_quit(void) {}

void pf_present(uint32_t const *vram)
{
    memcpy(screen, vram, sizeof screen);
    if (recording) {
        char path[512];
        snprintf(path, sizeof path, "%s/frame-%04d.pbm", opt.out_dir,
            frame_count);
        write_pbm(path, screen);
        fprintf(frame_index, "frame-%04d.pbm %u\n", frame_count,
            (unsigned)now_ms);
        frame_count++;
    }
}

uint32_t pf_time_ms(void)
{
    return now_ms - now_ms % PF_TICK_MS;
}

/* Lit le prochain mot du script (en sautant blancs et commentaires). */
static bool next_token(char *token, size_t size)
{
    for (;;) {
        while (isspace((unsigned char)*cursor))
            cursor++;
        if (*cursor == '#') {
            while (*cursor && *cursor != '\n')
                cursor++;
            continue;
        }
        break;
    }
    if (!*cursor)
        return false;

    size_t n = 0;
    while (*cursor && !isspace((unsigned char)*cursor)) {
        if (n + 1 < size)
            token[n++] = *cursor;
        cursor++;
    }
    token[n] = 0;
    return true;
}

static struct { char const *name; input_t key; } const KEYS[] = {
    { "UP", IN_UP }, { "DOWN", IN_DOWN }, { "LEFT", IN_LEFT },
    { "RIGHT", IN_RIGHT }, { "EXE", IN_EXE }, { "SHIFT", IN_SHIFT },
    { "EXIT", IN_EXIT }, { "DEL", IN_DEL }, { "AC", IN_AC },
    { "F1", IN_F1 }, { "F6", IN_F6 }, { "OTHER", IN_OTHER },
};

/* TEXTE:<texte> : tape le texte avec le clavier visuel, en partant du
 * clavier tout juste ouvert. « _ » = espace, « | » = ↵ (envoi). */
static void plan_text(char const *text)
{
    char buffer[512];
    size_t n = 0;
    for (; *text && n + 1 < sizeof buffer; text++)
        buffer[n++] = (*text == '_') ? ' ' : (*text == '|') ? '\n' : *text;
    buffer[n] = 0;

    int count = keyboard_plan(buffer, planned, sizeof planned
        / sizeof *planned);
    if (count < 0) {
        fprintf(stderr, "sim : impossible de taper : %s\n", buffer);
        exit(1);
    }
    planned_count = count;
    planned_next = 0;
}

input_t pf_getkey(bool timeout)
{
    char token[512];

    for (;;) {
        if (planned_next < planned_count) {
            now_ms += key_delay_ms;
            return planned[planned_next++];
        }
        if (wait_ticks > 0) {
            wait_ticks--;
            now_ms += PF_TICK_MS;
            if (timeout)
                return IN_NONE;
            continue;
        }
        /* Fin du script : le simulateur s'arrête là. */
        if (!next_token(token, sizeof token))
            exit(sim_finish());

        if (!strncmp(token, "WAIT:", 5)) {
            wait_ticks = atoi(token + 5);
            continue;
        }
        if (!strncmp(token, "SHOT:", 5)) {
            char path[512];
            snprintf(path, sizeof path, "%s/%s.pbm", opt.out_dir, token + 5);
            write_pbm(path, screen);
            continue;
        }
        if (!strncmp(token, "DELAY:", 6)) {
            key_delay_ms = atoi(token + 6);
            continue;
        }
        if (!strncmp(token, "ESP32:", 6)) {
            setenv("SIM_ESP32", token + 6, 1);
            continue;
        }
        if (!strncmp(token, "TEXTE:", 6)) {
            plan_text(token + 6);
            continue;
        }
        if (!strcmp(token, "REC:on")) {
            char path[512];
            snprintf(path, sizeof path, "%s/frames.txt", opt.out_dir);
            if (!frame_index && !(frame_index = fopen(path, "w"))) {
                fprintf(stderr, "sim : impossible d'écrire %s\n", path);
                exit(1);
            }
            recording = true;
            pf_present(screen);
            continue;
        }
        if (!strcmp(token, "REC:off")) {
            /* Dernière image, pour que sa durée soit connue. */
            pf_present(screen);
            recording = false;
            continue;
        }
        if (!strcmp(token, "MENU")) {
            now_ms += key_delay_ms;
            return IN_RESUME;
        }

        for (size_t i = 0; i < sizeof KEYS / sizeof *KEYS; i++) {
            if (!strcmp(token, KEYS[i].name)) {
                now_ms += key_delay_ms;
                return KEYS[i].key;
            }
        }
        fprintf(stderr, "sim : instruction inconnue : %s\n", token);
        exit(1);
    }
}

//---
// Liaison avec le faux ESP32
//---

bool pf_link_exchange(char const *request, char *response, size_t size,
    uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (size == 0)
        return false;
    response[0] = 0;

    setenv("SIM_REQUETE", request, 1);
    char command[600];
    snprintf(command, sizeof command, "python3 '%s'", opt.esp32_script);
    FILE *fp = popen(command, "r");
    if (!fp)
        return false;

    /* Le temps passe pendant l'échange : 2 s pour une question. */
    now_ms += strncmp(request, "Q ", 2) ? 50 : 2000;

    bool final = false;
    size_t used = 0;
    char line[1024];
    while (!final && fgets(line, sizeof line, fp)) {
        line[strcspn(line, "\r\n")] = 0;
        final = protocol_is_final(line);
        size_t n = strlen(line);
        if (used > 0 && used + 1 < size)
            response[used++] = '\n';
        if (used + n > size - 1)
            n = size - 1 - used;  /* used < size : n reste positif */
        memcpy(response + used, line, n);
        used += n;
        response[used] = 0;
    }
    pclose(fp);
    return final;
}
