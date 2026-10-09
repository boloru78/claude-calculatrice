/* wifi.c — Menu Wi-Fi.
 *
 * Liste des réseaux :
 *
 *   ████ Réseaux (5) ████████
 *   ▂▄▆█ 🔒 MaisonWifi      *     * : mot de passe connu de l'ESP32
 *   ▂▄▆  👤 Ecole-Personnel        👤 : nom d'utilisateur et mot de passe
 *   ▂▄      Cafe du coin           (rien) : réseau ouvert
 */
#include "wifi.h"
#include "app.h"
#include "gfx.h"
#include "platform.h"
#include "protocol.h"
#include "status.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Délais d'attente de l'ESP32, en millisecondes : une recherche prend
 * quelques secondes ; une connexion au plus 20 s, puis les vérifications
 * d'Internet et du Mac. */
#define SCAN_TIMEOUT_MS 25000
#define CONNECT_TIMEOUT_MS 50000

#define NETWORKS_MAX 20

/* Liste des réseaux : 6 lignes sous la barre de titre. */
#define LIST_TOP 11
#define LIST_ROWS 6
#define NAME_X 21        /* après l'icône du signal et celle de la sécurité */
#define NAME_RIGHT 117   /* puis « * », puis la barre de défilement */

/* Requête la plus longue : « WIFIC E⇥nom⇥utilisateur⇥mot de passe ». */
#define REQUEST_SIZE (8 + WIFI_SSID_MAX + 2 * (1 + WIFI_SECRET_MAX) + 1)

/* Largeur utile des lignes d'une boîte de dialogue. */
#define DIALOG_TEXT_WIDTH 112

static void message(char const *line1, char const *line2)
{
    char const *lines[] = { line1, line2 };
    char const *items[] = { "OK" };
    ui_dialog("Wi-Fi", lines, line2 ? 2 : 1, items, 1, 0, app_background,
        NULL);
}

/* Erreur de l'ESP32, dans la conversation (le message peut être long). */
static void report_error(char const *text)
{
    char line[160];
    snprintf(line, sizeof line, "Wi-Fi : %s", text);
    app_info(line);
}

/* Demande un texte au clavier visuel. Renvoie faux avec EXIT. */
static bool ask(char const *title, char *out, size_t limit)
{
    editor_t e;
    editor_clear(&e);
    editor_set_limit(&e, limit);
    if (!app_edit(&e, title))
        return false;
    memcpy(out, e.text, e.len + 1);
    return true;
}

/* Nom d'utilisateur (réseaux Entreprise), puis mot de passe. */
static bool ask_credentials(wifi_security_t security, char *user,
    char *password)
{
    if (security == WIFI_ENTERPRISE
            && !ask("Nom d'utilisateur", user, WIFI_SECRET_MAX))
        return false;
    return ask("Mot de passe", password, WIFI_SECRET_MAX);
}

/* Envoie la requête de connexion et note le compte rendu dans la
 * conversation. */
static void send_connection(char const *request, char const *ssid)
{
    char name[48];
    gfx_fit(name, sizeof name, ssid, DIALOG_TEXT_WIDTH);
    char const *lines[] = { "Connexion à", name };
    ui_box("Wi-Fi", lines, 2, app_background, NULL);

    char *raw = app_raw(), *text = app_text();
    if (!pf_link_exchange(request, raw, APP_BUFFER_SIZE,
            CONNECT_TIMEOUT_MS)) {
        app_info("Wi-Fi : l'ESP32 ne répond pas.");
        return;
    }
    reply_t r = protocol_parse(raw, text, APP_BUFFER_SIZE);
    if (r.kind == REPLY_ANSWER)
        app_info(text);
    else if (r.kind == REPLY_ERROR)
        report_error(text);
    else
        app_info("Wi-Fi : réponse incompréhensible de l'ESP32.");
    app_poll_status();
}

/* Connexion à un réseau de la liste. Renvoie faux si le joueur renonce
 * (retour à la liste). */
static bool connect_to(wifi_network_t const *n)
{
    char name[48];
    gfx_fit(name, sizeof name, n->ssid, DIALOG_TEXT_WIDTH);
    char request[REQUEST_SIZE];
    char user[WIFI_SECRET_MAX + 1] = "", password[WIFI_SECRET_MAX + 1] = "";

    if (n->security == WIFI_UNSUPPORTED) {
        message("Non pris en charge :", "certificat demandé.");
        return false;
    }
    if (n->security == WIFI_OPEN) {
        protocol_wifi(request, sizeof request, n->number, NULL, NULL);
        send_connection(request, n->ssid);
        return true;
    }
    if (n->saved) {
        char const *lines[] = { "Mot de passe connu." };
        char const *items[] = { "Se connecter", "Nouveau mot de passe",
            "Annuler" };
        int choice = ui_dialog(name, lines, 1, items, 3, 0, app_background,
            NULL);
        if (choice == 0) {
            protocol_wifi(request, sizeof request, n->number, NULL, NULL);
            send_connection(request, n->ssid);
            return true;
        }
        if (choice != 1)
            return false;
    }
    if (!ask_credentials(n->security, user, password))
        return false;
    protocol_wifi(request, sizeof request, n->number,
        n->security == WIFI_ENTERPRISE ? user : NULL, password);
    send_connection(request, n->ssid);
    return true;
}

static void draw_network(wifi_network_t const *n, int y)
{
    status_draw_bars(1, y, protocol_wifi_bars(n->rssi));
    if (n->security == WIFI_WEP || n->security == WIFI_PASSWORD)
        gfx_sprite(14, y, &SPR_WIFI_LOCK, GFX_BLACK);
    else if (n->security == WIFI_ENTERPRISE)
        gfx_sprite(14, y, &SPR_WIFI_USER, GFX_BLACK);
    else if (n->security == WIFI_UNSUPPORTED)
        gfx_text(14, y, "×", GFX_BLACK);

    char name[48];
    gfx_fit(name, sizeof name, n->ssid, NAME_RIGHT - NAME_X);
    gfx_text(NAME_X, y, name, GFX_BLACK);
    if (n->saved)
        gfx_text_at(123, y, "*", GFX_BLACK, ALIGN_RIGHT);
}

/* Liste des réseaux. Renvoie l'indice choisi, ou -1 avec EXIT. */
static int network_list(wifi_network_t const *list, int count, int selected)
{
    int top = 0;
    for (;;) {
        if (selected < top)
            top = selected;
        if (selected >= top + LIST_ROWS)
            top = selected - LIST_ROWS + 1;

        gfx_clear();
        char title[24];
        snprintf(title, sizeof title, "Réseaux (%d)", count);
        ui_title_bar(title);
        for (int i = top; i < count && i < top + LIST_ROWS; i++) {
            int y = LIST_TOP + (i - top) * UI_ITEM_HEIGHT;
            draw_network(&list[i], y);
            if (i == selected)
                gfx_rect(0, y - 1, GFX_WIDTH - 3, y + FONT_HEIGHT,
                    GFX_INVERT);
        }
        if (count > LIST_ROWS) {
            int height = LIST_ROWS * UI_ITEM_HEIGHT - 1;
            int thumb = height * LIST_ROWS / count;
            int y = LIST_TOP - 1
                + (height - thumb) * top / (count - LIST_ROWS);
            gfx_rect(GFX_WIDTH - 1, LIST_TOP - 1, GFX_WIDTH - 1,
                LIST_TOP - 1 + height, GFX_BLACK);
            gfx_rect(GFX_WIDTH - 2, y, GFX_WIDTH - 1, y + thumb, GFX_BLACK);
        }
        gfx_present();

        switch (ui_list_input(pf_getkey(false), &selected, count)) {
        case UI_CHOOSE:
            return selected;
        case UI_CANCEL:
            return -1;
        default:
            break;
        }
    }
}

/* Recherche des réseaux, puis connexion à l'un d'eux. Renvoie vrai si une
 * connexion a été demandée (son compte rendu est dans la conversation). */
static bool choose_network(void)
{
    char const *lines[] = { "Recherche des réseaux…" };
    ui_box("Wi-Fi", lines, 1, app_background, NULL);

    char *raw = app_raw(), *text = app_text();
    if (!pf_link_exchange(PROTOCOL_SCAN, raw, APP_BUFFER_SIZE,
            SCAN_TIMEOUT_MS)) {
        message("L'ESP32 ne répond pas.", "Vérifie le câble.");
        return false;
    }
    reply_t r = protocol_parse(raw, text, APP_BUFFER_SIZE);
    if (r.kind == REPLY_ERROR) {
        report_error(text);
        return true;
    }

    wifi_network_t *list = malloc(NETWORKS_MAX * sizeof *list);
    if (!list) {
        message("Mémoire insuffisante.", NULL);
        return false;
    }
    int count = r.kind == REPLY_ANSWER
        ? protocol_networks(raw, list, NETWORKS_MAX) : 0;
    bool done = false;
    if (count == 0) {
        message("Aucun réseau trouvé.", NULL);
    } else {
        int selected = 0;
        while (!done && (selected = network_list(list, count, selected)) >= 0)
            done = connect_to(&list[selected]);
    }
    free(list);
    return done;
}

/* Réseau caché : nom et sécurité tapés à la main. */
static bool hidden_network(void)
{
    char ssid[WIFI_SSID_MAX + 1];
    char user[WIFI_SECRET_MAX + 1] = "", password[WIFI_SECRET_MAX + 1] = "";
    if (!ask("Nom du réseau", ssid, WIFI_SSID_MAX))
        return false;

    char const *items[] = { "Aucune (ouvert)", "Mot de passe",
        "Nom + mot de passe" };
    int choice = ui_dialog("Sécurité", NULL, 0, items, 3, 1, app_background,
        NULL);
    if (choice < 0)
        return false;
    wifi_security_t security = choice == 0 ? WIFI_OPEN
        : choice == 1 ? WIFI_PASSWORD : WIFI_ENTERPRISE;
    if (security != WIFI_OPEN && !ask_credentials(security, user, password))
        return false;

    char request[REQUEST_SIZE];
    protocol_wifi_hidden(request, sizeof request, security, ssid,
        security == WIFI_ENTERPRISE ? user : NULL,
        security != WIFI_OPEN ? password : NULL);
    send_connection(request, ssid);
    return true;
}

void wifi_menu(void)
{
    int choice = 0;
    for (;;) {
        app_poll_status();
        status_t const *s = app_status();
        char network[64], signal[32], name[48];
        char const *lines[2];
        int count = 0;
        if (!s->esp_ok) {
            lines[count++] = "ESP32 : pas de réponse";
        } else if (!s->wifi) {
            lines[count++] = "Pas connecté";
        } else {
            gfx_fit(name, sizeof name, s->ssid[0] ? s->ssid : "?",
                DIALOG_TEXT_WIDTH - gfx_text_width("Réseau : "));
            snprintf(network, sizeof network, "Réseau : %s", name);
            snprintf(signal, sizeof signal, "Signal : %d/4 (%d dBm)",
                protocol_wifi_bars(s->rssi), s->rssi);
            lines[count++] = network;
            lines[count++] = signal;
        }

        char const *items[] = { "Chercher les réseaux", "Réseau caché",
            "Retour" };
        choice = ui_dialog("Wi-Fi", lines, count, items, 3, choice,
            app_background, NULL);
        if (choice == 0 && choose_network())
            return;
        if (choice == 1 && hidden_network())
            return;
        if (choice < 0 || choice == 2)
            return;
    }
}
