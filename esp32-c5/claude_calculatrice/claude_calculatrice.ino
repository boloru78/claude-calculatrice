/* claude_calculatrice.ino — Firmware de l'ESP32-C5 pour « Claude sur la
 * calculatrice ».
 *
 * L'ESP32 fait le relais entre la calculatrice (câble sur son port
 * 3 broches) et la mini-API du Mac (mac/serveur.py) en Wi-Fi :
 *
 *   Calculatrice ──série──► ESP32 ──Wi-Fi (HTTP)──► Mac ──► Claude
 *
 * Protocole avec la calculatrice : des lignes de texte UTF-8 terminées par
 * « \n ». Les mêmes commandes sont acceptées sur le port USB : on peut donc
 * tout tester depuis le moniteur série de l'Arduino IDE, sans la
 * calculatrice. La réponse repart toujours par où la question est arrivée.
 *
 *   Reçu                 Réponse
 *   PING                 PONG <rssi> <heure> <réseau> : force du Wi-Fi en
 *                        dBm, heure « hh:mm:ss » (« - » si inconnues) et nom
 *                        du réseau (absent sans Wi-Fi)
 *   Q <question>         ATT, puis une ligne « R <texte> » par ligne de la
 *                        réponse, puis FIN ; ou ERR <message>
 *   NOUV                 OK (nouvelle conversation) ; ou ERR <message>
 *   SCAN                 ATT, puis « W <n> <rssi> <sécurité> <nom> » par
 *                        réseau trouvé (le plus fort d'abord), puis FIN
 *   WIFI <n>[⇥<utilisateur>][⇥<mot de passe>]
 *                        connexion au réseau n° n de la dernière recherche :
 *                        ATT, puis des lignes « R » (réseau, Internet, Mac)
 *                        et FIN ; ou ERR <message>
 *   WIFIC <sécurité>⇥<nom>[⇥<utilisateur>][⇥<mot de passe>]
 *                        pareil pour un réseau caché, tapé à la main
 *   (autre chose)        ERR Commande inconnue.
 *
 * ⇥ est une tabulation. Sécurité d'un réseau : O ouvert, W WEP, P mot de
 * passe (WPA, WPA2, WPA3), E nom d'utilisateur et mot de passe (WPA2/WPA3
 * Entreprise : école, travail, eduroam), X non pris en charge (certificat,
 * WAPI). Dans SCAN, une « * » après la lettre indique un réseau dont l'ESP32
 * connaît déjà le mot de passe : « WIFI <n> » suffit alors.
 *
 * Les réseaux auxquels on s'est connecté sont retenus (les RESEAUX_MAX
 * derniers, dans la mémoire flash) : au démarrage, ou si le Wi-Fi est perdu,
 * l'ESP32 se connecte au premier qu'il voit, puis au réseau de secrets.h.
 *
 * La mini-API est cherchée sur le réseau avec Bonjour (mDNS), puis à
 * l'adresse de secrets.h, puis par Internet (MAC_URL_INTERNET, Tailscale
 * Funnel, en HTTPS) : le Mac peut rester à la maison.
 *
 * Carte : ESP32-C5 (Arduino-ESP32 3.3 ou plus récent, carte « ESP32C5 Dev
 * Module »). Pour la version WROOM-1U, une antenne Wi-Fi doit être branchée
 * sur la prise U.FL, sinon le Wi-Fi ne capte presque rien. */
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_eap_client.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <time.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copier secrets.example.h en secrets.h (même dossier) et le remplir."
#endif
#ifndef MAC_URL_INTERNET
#define MAC_URL_INTERNET ""  // secrets.h d'avant l'accès par Internet
#endif

//---
// Réglages
//---

/* Liaison avec la calculatrice (port 3 broches, jack 2,5 mm, 3,3 V) :
 *   pointe du jack (réception de la calculatrice)  <- GPIO 5 (TX de l'ESP32)
 *   anneau du jack (émission de la calculatrice)   -> GPIO 4 (RX de l'ESP32)
 *   corps du jack (masse)                          -- GND
 * À vérifier au multimètre avant de brancher. */
#define CALC_RX 4
#define CALC_TX 5
#define CALC_VITESSE 9600  // bauds ; à accorder avec l'app de la calculatrice

#define USB_VITESSE 115200       // moniteur série de l'Arduino IDE
#define LONGUEUR_MAX_LIGNE 1100  // une question fait au plus 1000 caractères
#define DELAI_HTTP_MS 65000      // le plus long que permet HTTPClient
#define VOYANT_RGB 1             // 0 si la carte n'a pas de LED RGB (GPIO 27)

/* Wi-Fi. */
#define RESEAUX_MAX 5               // réseaux retenus (les plus récents)
#define VUS_MAX 20                  // réseaux envoyés après une recherche
#define DELAI_CONNEXION_MS 20000    // pour se connecter à un réseau
#define DELAI_PERTE_MS 30000        // Wi-Fi perdu depuis 30 s : autre réseau
#define PAUSE_RECHERCHE_MS 60000    // aucun réseau connu : réessai 1 min après
#define SERVICE_BONJOUR "claudecalc" // annoncé par la mini-API (_claudecalc._tcp)
#define NOM_BONJOUR "calculatrice-claude"
#define URL_INTERNET "http://connectivitycheck.gstatic.com/generate_204"

/* Heure : réglée par Internet (NTP), au fuseau de Montréal / Québec, heure
 * d'été comprise. Règle POSIX : voir la documentation de la fonction tzset. */
#define FUSEAU "EST5EDT,M3.2.0,M11.1.0"

HardwareSerial Calculatrice(1);

//---
// Types (avant toute fonction : l'Arduino IDE déclare les fonctions en
// tête du programme, et leurs types doivent déjà exister)
//---

/* Un réseau auquel on sait se connecter. Sécurité : voir l'en-tête. */
struct Reseau {
    String nom;
    char securite = 'P';
    bool cache = false;      // réseau caché : on l'essaie même sans le voir
    String utilisateur;      // sécurité E seulement
    String mot_de_passe;
};

/* Un réseau vu lors d'une recherche. */
struct Vu {
    String nom;
    wifi_auth_mode_t auth = WIFI_AUTH_OPEN;
    int32_t rssi = 0, canal = 0;
    uint8_t bssid[6] = { 0 };
};

/* Connexion automatique : voir auto_etape(). */
enum Auto { AUTO_CONNECTE, AUTO_RECHERCHE, AUTO_ESSAI, AUTO_PAUSE };

struct Candidat {
    Reseau reseau;
    bool vu = false;
    Vu info;
};

//---
// Voyant : bleu = connexion Wi-Fi, vert = prêt, jaune = Claude réfléchit,
// rouge = erreur
//---

static void voyant(uint8_t r, uint8_t v, uint8_t b)
{
#if VOYANT_RGB && defined(RGB_BUILTIN)
    rgbLedWrite(RGB_BUILTIN, r / 4, v / 4, b / 4);
#else
    (void)r; (void)v; (void)b;
#endif
}

static void voyant_etat(void)
{
    if (WiFi.status() == WL_CONNECTED)
        voyant(0, 255, 0);
    else
        voyant(0, 0, 255);
}

//---
// Réseaux Wi-Fi
//---

static Reseau reseaux[RESEAUX_MAX];  // retenus, le plus récent d'abord
static int nb_reseaux;
static Vu vus[VUS_MAX];              // dernière recherche de la calculatrice
static int nb_vus;

/* Raison de la dernière déconnexion, et nombre de refus du mot de passe
 * pendant l'essai en cours (mis à jour par sur_deconnexion()). */
static volatile uint8_t derniere_raison;
static volatile int refus;

static char securite(wifi_auth_mode_t auth)
{
    switch (auth) {
    case WIFI_AUTH_OPEN:
    case WIFI_AUTH_OWE:
        return 'O';
    case WIFI_AUTH_WEP:
        return 'W';
    case WIFI_AUTH_WPA_PSK:
    case WIFI_AUTH_WPA2_PSK:
    case WIFI_AUTH_WPA_WPA2_PSK:
    case WIFI_AUTH_WPA3_PSK:
    case WIFI_AUTH_WPA2_WPA3_PSK:
    case WIFI_AUTH_WPA3_EXT_PSK:
    case WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE:
        return 'P';
    case WIFI_AUTH_WPA_ENTERPRISE:
    case WIFI_AUTH_WPA2_ENTERPRISE:
    case WIFI_AUTH_WPA3_ENTERPRISE:
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
        return 'E';
    default:
        return 'X';  // WPA3 Entreprise 192 bits (certificat), WAPI, DPP
    }
}

/* Un nom de réseau affichable : les caractères de contrôle deviennent « ? ». */
static String propre(String const &texte)
{
    String s = texte;
    for (unsigned i = 0; i < s.length(); i++) {
        if ((uint8_t)s[i] < 0x20 || s[i] == 0x7f)
            s.setCharAt(i, '?');
    }
    return s;
}

/* Le réseau de secrets.h, s'il a été rempli. */
static bool reseau_secrets(Reseau &r)
{
    if (!WIFI_NOM[0] || !strcmp(WIFI_NOM, "nom-du-wifi"))
        return false;
    r = Reseau();
    r.nom = WIFI_NOM;
    r.mot_de_passe = WIFI_MOT_DE_PASSE;
    r.securite = r.mot_de_passe.length() ? 'P' : 'O';
    return true;
}

static int reseau_retenu(String const &nom)
{
    for (int i = 0; i < nb_reseaux; i++)
        if (reseaux[i].nom == nom)
            return i;
    return -1;
}

static void charger_reseaux(void)
{
    Preferences p;
    nb_reseaux = 0;
    if (!p.begin("reseaux", true))
        return;  // premier démarrage : rien de retenu
    int n = p.getInt("n", 0);
    for (int i = 0; i < n && i < RESEAUX_MAX; i++) {
        String k = String(i);
        Reseau &r = reseaux[nb_reseaux];
        r.nom = p.getString(("s" + k).c_str(), "");
        r.securite = (char)p.getUChar(("c" + k).c_str(), 'P');
        r.cache = p.getBool(("h" + k).c_str(), false);
        r.utilisateur = p.getString(("u" + k).c_str(), "");
        r.mot_de_passe = p.getString(("p" + k).c_str(), "");
        if (r.nom.length())
            nb_reseaux++;
    }
    p.end();
}

static void sauver_reseaux(void)
{
    Preferences p;
    if (!p.begin("reseaux", false))
        return;
    p.clear();
    p.putInt("n", nb_reseaux);
    for (int i = 0; i < nb_reseaux; i++) {
        String k = String(i);
        p.putString(("s" + k).c_str(), reseaux[i].nom);
        p.putUChar(("c" + k).c_str(), (uint8_t)reseaux[i].securite);
        p.putBool(("h" + k).c_str(), reseaux[i].cache);
        p.putString(("u" + k).c_str(), reseaux[i].utilisateur);
        p.putString(("p" + k).c_str(), reseaux[i].mot_de_passe);
    }
    p.end();
}

/* Retient un réseau qui a marché, en tête de liste. */
static void retenir(Reseau const &r)
{
    int i = reseau_retenu(r.nom);
    if (i < 0)
        i = nb_reseaux < RESEAUX_MAX ? nb_reseaux++ : RESEAUX_MAX - 1;
    for (; i > 0; i--)
        reseaux[i] = reseaux[i - 1];
    reseaux[0] = r;
    sauver_reseaux();
}

/* Appelée par le Wi-Fi (autre tâche) à chaque déconnexion. */
static void sur_deconnexion(arduino_event_id_t, arduino_event_info_t info)
{
    uint8_t raison = info.wifi_sta_disconnected.reason;
    if (raison == WIFI_REASON_ASSOC_LEAVE)
        return;  // c'est nous qui avons coupé
    derniere_raison = raison;
    switch (raison) {
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_MIC_FAILURE:
    case WIFI_REASON_802_1X_AUTH_FAILED:
        refus = refus + 1;  // pas « ++ » : déconseillé sur un volatile
        break;
    default:
        break;
    }
}

/* Lance la connexion à un réseau, sans attendre. [vu] (facultatif) donne
 * le canal et l'appareil exact trouvés par une recherche. */
static void lancer(Reseau const &r, Vu const *vu)
{
    WiFi.disconnect();
    delay(50);
    derniere_raison = 0;
    refus = 0;
    int32_t canal = vu ? vu->canal : 0;
    uint8_t const *bssid = vu ? vu->bssid : NULL;

    if (r.securite == 'E') {
        /* PEAP ou TTLS, selon ce que propose le réseau ; le nom
         * d'utilisateur sert aussi d'identité. Le certificat du réseau
         * n'est pas vérifié (il faudrait le charger dans l'ESP32). */
        WiFi.begin(r.nom.c_str(), WPA2_AUTH_PEAP, r.utilisateur.c_str(),
            r.utilisateur.c_str(), r.mot_de_passe.c_str(), NULL, NULL, NULL,
            -1, canal, bssid);
        return;
    }

    esp_wifi_sta_enterprise_disable();
    /* Accepte aussi les vieux réseaux WEP et WPA (refusés par défaut). */
    WiFi.setMinSecurity(r.securite == 'W' || r.cache ? WIFI_AUTH_WEP
                                                     : WIFI_AUTH_WPA_PSK);
    bool owe = vu && vu->auth == WIFI_AUTH_OWE;
    char const *mdp = r.mot_de_passe.length() ? r.mot_de_passe.c_str() : NULL;
    WiFi.begin(r.nom.c_str(), mdp, canal, bssid, !owe);
    if (owe) {
        /* « Ouvert amélioré » (OWE) : chiffré, mais sans mot de passe. */
        wifi_config_t conf;
        esp_wifi_get_config(WIFI_IF_STA, &conf);
        conf.sta.owe_enabled = 1;
        conf.sta.threshold.authmode = WIFI_AUTH_OWE;
        esp_wifi_set_config(WIFI_IF_STA, &conf);
        esp_wifi_connect();
    }
}

/* Message à afficher après un échec de connexion. */
static String message_echec(uint8_t raison, char secu)
{
    switch (raison) {
    case 0:
        return "Pas de réponse du réseau.";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_MIC_FAILURE:
    case WIFI_REASON_802_1X_AUTH_FAILED:
        return secu == 'E' ? "Nom d'utilisateur ou mot de passe refusé."
                           : "Mot de passe refusé.";
    case WIFI_REASON_NO_AP_FOUND:
        return "Réseau introuvable.";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        return "Sécurité du réseau non prise en charge.";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "Signal trop faible.";
    default:
        return String("Connexion impossible (code ") + raison + ").";
    }
}

/* Se connecte en attendant le résultat. Renvoie "" si c'est réussi, sinon
 * le message d'erreur. */
static String connecter(Reseau const &r, Vu const *vu)
{
    voyant(0, 0, 255);
    lancer(r, vu);
    uint32_t debut = millis();
    while (WiFi.status() != WL_CONNECTED) {
        /* Deux refus : inutile d'insister, le mot de passe est faux. */
        if (refus >= 2 || millis() - debut > DELAI_CONNEXION_MS) {
            uint8_t raison = derniere_raison;
            WiFi.disconnect();
            return message_echec(raison, r.securite);
        }
        delay(50);
    }
    return "";
}

//---
// Connexion automatique aux réseaux retenus
//
// Au démarrage, et quand le Wi-Fi est perdu depuis DELAI_PERTE_MS : une
// recherche (sans bloquer : la calculatrice reçoit toujours ses réponses),
// puis un essai de chaque réseau retenu qui est visible (ou caché), du plus
// récent au plus ancien, puis du réseau de secrets.h.
//---

static Auto auto_etat = AUTO_PAUSE;
static uint32_t auto_depuis;  // début de l'étape
static Candidat candidats[RESEAUX_MAX + 1];
static int nb_candidats, candidat;

static void auto_etape_vers(Auto etat)
{
    auto_etat = etat;
    auto_depuis = millis();
}

static void auto_relancer(void)
{
    Reseau r;
    if (nb_reseaux == 0 && !reseau_secrets(r)) {
        auto_etape_vers(AUTO_PAUSE);  // aucun réseau connu
        return;
    }
    WiFi.disconnect();
    WiFi.scanDelete();
    if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) {
        auto_etape_vers(AUTO_PAUSE);
        return;
    }
    auto_etape_vers(AUTO_RECHERCHE);
}

/* Ajoute [r] aux candidats s'il est visible dans la recherche (n réseaux)
 * ou caché. */
static void ajouter_candidat(Reseau const &r, int n)
{
    Candidat &c = candidats[nb_candidats];
    c.reseau = r;
    c.vu = false;
    for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) != r.nom || (c.vu && WiFi.RSSI(i) <= c.info.rssi))
            continue;
        c.vu = true;
        c.info.nom = r.nom;
        c.info.auth = WiFi.encryptionType(i);
        c.info.rssi = WiFi.RSSI(i);
        c.info.canal = WiFi.channel(i);
        memcpy(c.info.bssid, WiFi.BSSID(i), 6);
    }
    if (c.vu || r.cache)
        nb_candidats++;
}

static void essai_suivant(void)
{
    if (++candidat >= nb_candidats) {
        WiFi.disconnect();
        auto_etape_vers(AUTO_PAUSE);
        return;
    }
    Candidat const &c = candidats[candidat];
    Serial.printf("Essai du réseau « %s »...\n", c.reseau.nom.c_str());
    lancer(c.reseau, c.vu ? &c.info : NULL);
    auto_etape_vers(AUTO_ESSAI);
}

static void auto_etape(void)
{
    uint32_t duree = millis() - auto_depuis;
    switch (auto_etat) {
    case AUTO_CONNECTE:
        if (WiFi.status() == WL_CONNECTED)
            auto_depuis = millis();
        else if (duree > DELAI_PERTE_MS)
            auto_relancer();
        break;
    case AUTO_PAUSE:
        if (duree > PAUSE_RECHERCHE_MS)
            auto_relancer();
        break;
    case AUTO_RECHERCHE: {
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING)
            break;
        if (n < 0) {
            auto_etape_vers(AUTO_PAUSE);
            break;
        }
        nb_candidats = 0;
        for (int i = 0; i < nb_reseaux; i++)
            ajouter_candidat(reseaux[i], n);
        Reseau r;
        if (reseau_secrets(r) && reseau_retenu(r.nom) < 0)
            ajouter_candidat(r, n);
        WiFi.scanDelete();
        candidat = -1;
        essai_suivant();
        break;
    }
    case AUTO_ESSAI:
        if (WiFi.status() == WL_CONNECTED)
            auto_etape_vers(AUTO_CONNECTE);
        else if (refus >= 2 || duree > DELAI_CONNEXION_MS)
            essai_suivant();
        break;
    }
}

/* Annonce les connexions et déconnexions sur le port USB. */
static void suivre_wifi(void)
{
    static wl_status_t precedent = WL_IDLE_STATUS;
    wl_status_t etat = WiFi.status();
    if (etat == precedent)
        return;
    bool etait_connecte = precedent == WL_CONNECTED;
    precedent = etat;
    if (etat == WL_CONNECTED) {
        Serial.printf("Wi-Fi connecté : %s (ESP32 en %s)\n",
            WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    } else if (etait_connecte) {
        Serial.println("Wi-Fi perdu, reconnexion...");
    }
    voyant_etat();
}

//---
// Mini-API du Mac
//
// Trois façons de la joindre, essayées dans cet ordre :
//   1. Bonjour : le Mac est sur le même réseau que l'ESP32 ;
//   2. l'adresse MAC_ADRESSE de secrets.h, sur le même réseau ;
//   3. son adresse Internet MAC_URL_INTERNET (Tailscale Funnel, en HTTPS) :
//      le Mac reste à la maison, l'ESP32 est n'importe où avec Internet.
//---

/* Certificats racines de Let's Encrypt (ISRG Root X1 et X2, valables
 * jusqu'en 2035 et 2040), qui signent les adresses *.ts.net de Tailscale.
 * Grâce à eux, l'ESP32 vérifie qu'il parle bien à ton Mac avant de lui
 * envoyer le code secret. */
static char const CERTIFICATS_RACINES[] = R"(-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw
CQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2gg
R3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00
MDA5MTcxNjAwMDBaME8xCzAJBgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBT
ZWN1cml0eSBSZXNlYXJjaCBHcm91cDEVMBMGA1UEAxMMSVNSRyBSb290IFgyMHYw
EAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJSvMWSj5cz3es3mcFDR0HttwW
+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvXRdgKam7mAHf7AlF9
ItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0T
AQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI
zj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdW
tL4ndQavEi51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1
/q4AaOeMSQ+2b1tbFfLn
-----END CERTIFICATE-----
)";

static String mac_url = String("http://") + MAC_ADRESSE + ":" + MAC_PORT;
static volatile bool mac_a_chercher = true;  // nouveau réseau : où est le Mac ?
static bool bonjour_pret;

/* Appelée par le Wi-Fi (autre tâche) quand l'ESP32 reçoit une adresse. */
static void sur_adresse(arduino_event_id_t, arduino_event_info_t)
{
    mac_a_chercher = true;
}

/* Envoie une requête à [base] + [chemin] (http:// ou https://). Renvoie le
 * code HTTP (négatif si le Mac est injoignable) et place le corps de la
 * réponse dans [corps]. */
static int requete(char const *methode, String const &base,
    char const *chemin, String const &texte, String &corps,
    uint32_t connexion_ms, uint32_t delai_ms)
{
    HTTPClient http;
    NetworkClientSecure securise;
    String url = base + chemin;
    http.setConnectTimeout(connexion_ms);
    http.setTimeout(delai_ms);
    bool pret;
    if (url.startsWith("https://")) {
        securise.setCACert(CERTIFICATS_RACINES);
        securise.setHandshakeTimeout(15);
        pret = http.begin(securise, url);
    } else {
        pret = http.begin(url);
    }
    if (!pret) {
        corps = "Adresse du Mac invalide.";
        return -1;
    }
    http.addHeader("X-Code", MAC_CODE);
    http.addHeader("Content-Type", "text/plain; charset=utf-8");
    int statut = strcmp(methode, "GET") ? http.POST(texte) : http.GET();
    if (statut > 0)
        corps = http.getString();
    else
        corps = "Mac injoignable (serveur lancé ?)";
    http.end();
    return statut;
}

/* Vrai si la mini-API répond à [base]. */
static bool mac_repond(String const &base, uint32_t delai_ms)
{
    String corps;
    return requete("GET", base, "/etat", "", corps, delai_ms, delai_ms) == 200
        && corps == "OK";
}

/* Cherche la mini-API (voir plus haut). Renvoie comment elle a été
 * trouvée : « réseau local », « Internet », ou "" si elle ne répond pas. */
static String trouver_mac(void)
{
    mac_a_chercher = false;
    if (!bonjour_pret)
        bonjour_pret = MDNS.begin(NOM_BONJOUR);
    if (bonjour_pret && MDNS.queryService(SERVICE_BONJOUR, "tcp") > 0
            && MDNS.address(0) != IPAddress()) {
        mac_url = "http://" + MDNS.address(0).toString() + ":"
            + MDNS.port(0);
        Serial.println("Mini-API trouvée (Bonjour) : " + mac_url);
        return "réseau local";
    }

    String locale = String("http://") + MAC_ADRESSE + ":" + MAC_PORT;
    if (mac_repond(locale, 3000)) {
        mac_url = locale;
        Serial.println("Mini-API trouvée : " + mac_url);
        return "réseau local";
    }

    String internet = MAC_URL_INTERNET;
    while (internet.endsWith("/"))
        internet.remove(internet.length() - 1);
    if (internet.length() && mac_repond(internet, 10000)) {
        mac_url = internet;
        Serial.println("Mini-API trouvée (Internet) : " + mac_url);
        return "Internet";
    }

    mac_url = locale;
    return "";
}

/* POST à la mini-API ; si le Mac ne répond pas, on le cherche de nouveau
 * (son adresse a pu changer) et on réessaie une fois. */
static int appeler_mac(char const *chemin, String const &texte, String &corps)
{
    if (mac_a_chercher)
        trouver_mac();
    int statut = requete("POST", mac_url, chemin, texte, corps, 5000,
        DELAI_HTTP_MS);
    if (statut == HTTPC_ERROR_CONNECTION_REFUSED) {
        String avant = mac_url;
        trouver_mac();
        if (mac_url != avant)
            statut = requete("POST", mac_url, chemin, texte, corps, 5000,
                DELAI_HTTP_MS);
    }
    return statut;
}

/* Accès à Internet : « oui », « non », ou bloqué par une page de connexion
 * web (hôtel, café…), que l'ESP32 ne peut pas remplir. */
static String etat_internet(void)
{
    HTTPClient http;
    http.setConnectTimeout(4000);
    http.setTimeout(4000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!http.begin(URL_INTERNET))
        return "non";
    int statut = http.GET();
    http.end();
    if (statut == 204)
        return "oui";
    if (statut > 0)
        return "bloqué par une page de connexion web (impossible depuis "
               "l'ESP32)";
    return "non";
}

//---
// Commandes
//---

/* Envoie [texte] ligne par ligne, chacune précédée de « R ». */
static void envoyer_reponse(Stream &sortie, String const &texte)
{
    int debut = 0;
    while (debut <= (int)texte.length()) {
        int fin = texte.indexOf('\n', debut);
        if (fin < 0)
            fin = texte.length();
        sortie.print("R ");
        sortie.println(texte.substring(debut, fin));
        debut = fin + 1;
    }
    sortie.println("FIN");
}

/* « PONG <rssi> <heure> <réseau> » : la calculatrice en fait sa barre
 * d'état. */
static void repondre_etat(Stream &sortie)
{
    char rssi[8] = "-", heure[12] = "-";
    bool connecte = WiFi.status() == WL_CONNECTED;
    if (connecte)
        snprintf(rssi, sizeof rssi, "%d", (int)WiFi.RSSI());
    struct tm t;
    /* Tant que l'heure n'a pas été reçue, l'horloge est en 1970. */
    if (getLocalTime(&t, 0) && t.tm_year > 120)
        strftime(heure, sizeof heure, "%H:%M:%S", &t);
    sortie.printf("PONG %s %s", rssi, heure);
    if (connecte) {
        sortie.print(' ');
        sortie.print(propre(WiFi.SSID()));
    }
    sortie.println();
}

static void erreur(Stream &sortie, String const &message)
{
    sortie.print("ERR ");
    sortie.println(message);
    voyant(255, 0, 0);
}

/* Découpe [texte] aux tabulations. Renvoie le nombre de champs. */
static int champs(String const &texte, String *sortie, int max)
{
    int n = 0, debut = 0;
    while (n < max) {
        int fin = texte.indexOf('\t', debut);
        if (fin < 0 || n == max - 1) {
            sortie[n++] = texte.substring(debut);
            break;
        }
        sortie[n++] = texte.substring(debut, fin);
        debut = fin + 1;
    }
    return n;
}

/* SCAN : les réseaux autour, le plus fort d'abord, un seul par nom. */
static void cmd_scan(Stream &sortie)
{
    sortie.println("ATT");
    /* Pas de recherche pendant un essai de connexion : on l'arrête (la
     * connexion automatique reprendra plus tard). */
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect();
        auto_etape_vers(AUTO_PAUSE);
    }
    while (WiFi.scanComplete() == WIFI_SCAN_RUNNING)
        delay(10);
    WiFi.scanDelete();
    int n = WiFi.scanNetworks();
    if (n < 0) {
        erreur(sortie, "Recherche impossible.");
        return;
    }

    nb_vus = 0;
    for (int i = 0; i < n; i++) {
        String nom = WiFi.SSID(i);
        if (!nom.length())
            continue;  // réseau caché : à taper soi-même
        int j = 0;
        while (j < nb_vus && vus[j].nom != nom)
            j++;
        if (j < nb_vus && vus[j].rssi >= WiFi.RSSI(i))
            continue;  // déjà vu, plus fort
        if (j == nb_vus) {
            if (nb_vus == VUS_MAX) {
                /* Liste pleine : remplace le plus faible s'il l'est plus. */
                j = 0;
                for (int k = 1; k < VUS_MAX; k++)
                    if (vus[k].rssi < vus[j].rssi)
                        j = k;
                if (vus[j].rssi >= WiFi.RSSI(i))
                    continue;
            } else {
                nb_vus++;
            }
        }
        vus[j].nom = nom;
        vus[j].auth = WiFi.encryptionType(i);
        vus[j].rssi = WiFi.RSSI(i);
        vus[j].canal = WiFi.channel(i);
        memcpy(vus[j].bssid, WiFi.BSSID(i), 6);
    }
    WiFi.scanDelete();

    /* Du plus fort au plus faible. */
    for (int i = 1; i < nb_vus; i++)
        for (int j = i; j > 0 && vus[j].rssi > vus[j - 1].rssi; j--)
            std::swap(vus[j], vus[j - 1]);

    for (int i = 0; i < nb_vus; i++) {
        sortie.printf("W %d %d %c%s ", i + 1, (int)vus[i].rssi,
            securite(vus[i].auth), reseau_retenu(vus[i].nom) >= 0 ? "*" : "");
        sortie.println(propre(vus[i].nom));
    }
    sortie.println("FIN");
}

/* Vérifie les identifiants avant d'essayer. Renvoie "" s'ils conviennent. */
static String verifier(Reseau const &r, wifi_auth_mode_t auth)
{
    if (!r.nom.length() || r.nom.length() > 32)
        return "Nom du réseau invalide (32 caractères au plus).";
    if (r.securite == 'E') {
        if (!r.utilisateur.length() || !r.mot_de_passe.length())
            return "Nom d'utilisateur et mot de passe nécessaires.";
        if (r.utilisateur.length() > 64 || r.mot_de_passe.length() > 64)
            return "64 caractères au plus.";
    } else if (r.securite != 'O') {
        if (!r.mot_de_passe.length())
            return "Mot de passe nécessaire.";
        if (r.mot_de_passe.length() > 64)
            return "Mot de passe trop long (64 caractères au plus).";
        if (auth != WIFI_AUTH_WEP && !r.cache && r.mot_de_passe.length() < 8)
            return "Mot de passe trop court (8 caractères au moins).";
    }
    return "";
}

/* Connexion demandée par la calculatrice, puis compte rendu. */
static void connecter_et_repondre(Stream &sortie, Reseau const &r,
    Vu const *vu)
{
    String probleme = verifier(r, vu ? vu->auth : WIFI_AUTH_MAX);
    if (!probleme.length())
        probleme = connecter(r, vu);
    if (probleme.length()) {
        erreur(sortie, probleme);
        auto_relancer();  // retour à un réseau connu
        return;
    }
    retenir(r);
    auto_etape_vers(AUTO_CONNECTE);
    voyant_etat();

    String internet = etat_internet();
    String mac = trouver_mac();

    String texte = "Wi-Fi : connecté à " + propre(r.nom) + ".\n";
    texte += "Internet : " + internet + ".\n";
    texte += mac.length() ? "Mac : trouvé (" + mac + ")."
                          : "Mac : introuvable (mini-API lancée ?).";
    envoyer_reponse(sortie, texte);
}

/* WIFI <n>[⇥<utilisateur>][⇥<mot de passe>] : réseau de la dernière
 * recherche. Sans identifiants, ceux retenus servent. */
static void cmd_wifi(Stream &sortie, String const &arguments)
{
    sortie.println("ATT");
    String f[3];
    int n = champs(arguments, f, 3);
    int numero = f[0].toInt();
    if (numero < 1 || numero > nb_vus) {
        erreur(sortie, "Réseau inconnu : relance la recherche.");
        return;
    }
    Vu const &vu = vus[numero - 1];
    Reseau r;
    r.nom = vu.nom;
    r.securite = securite(vu.auth);
    if (r.securite == 'X') {
        erreur(sortie, "Ce réseau demande un certificat : non pris en charge.");
        return;
    }
    if (n == 1) {
        int i = reseau_retenu(r.nom);
        if (i >= 0) {
            r.utilisateur = reseaux[i].utilisateur;
            r.mot_de_passe = reseaux[i].mot_de_passe;
        }
    } else if (r.securite == 'E' && n == 3) {
        r.utilisateur = f[1];
        r.mot_de_passe = f[2];
    } else {
        r.mot_de_passe = f[n - 1];
    }
    connecter_et_repondre(sortie, r, &vu);
}

/* WIFIC <sécurité>⇥<nom>[⇥<utilisateur>][⇥<mot de passe>] : réseau caché. */
static void cmd_wifi_cache(Stream &sortie, String const &arguments)
{
    sortie.println("ATT");
    String f[4];
    int n = champs(arguments, f, 4);
    Reseau r;
    r.cache = true;
    r.securite = f[0].length() == 1 ? f[0][0] : '?';
    if (n >= 2)
        r.nom = f[1];
    if (r.securite == 'E' && n == 4) {
        r.utilisateur = f[2];
        r.mot_de_passe = f[3];
    } else if (r.securite == 'P' && n == 3) {
        r.mot_de_passe = f[2];
    } else if (r.securite != 'O' || n != 2) {
        erreur(sortie, "Commande WIFIC incomplète.");
        return;
    }
    connecter_et_repondre(sortie, r, NULL);
}

static void traiter(Stream &sortie, String ligne)
{
    /* Les champs de WIFI et WIFIC sont pris tels quels : un mot de passe
     * peut commencer ou finir par une espace. */
    if (ligne.startsWith("WIFI ")) {
        cmd_wifi(sortie, ligne.substring(5));
        return;
    }
    if (ligne.startsWith("WIFIC ")) {
        cmd_wifi_cache(sortie, ligne.substring(6));
        return;
    }

    ligne.trim();
    if (ligne.length() == 0)
        return;
    if (ligne == "PING") {
        repondre_etat(sortie);
        return;
    }
    if (ligne == "SCAN") {
        cmd_scan(sortie);
        return;
    }

    bool question = ligne.startsWith("Q ");
    if (!question && ligne != "NOUV") {
        sortie.println("ERR Commande inconnue.");
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        erreur(sortie, "Pas de Wi-Fi : EXIT, puis Wi-Fi.");
        return;
    }

    String corps;
    int statut;
    if (question) {
        /* Accusé de réception : la calculatrice peut afficher l'attente. */
        sortie.println("ATT");
        voyant(255, 160, 0);
        statut = appeler_mac("/question", ligne.substring(2), corps);
    } else {
        statut = appeler_mac("/nouvelle", "", corps);
    }

    if (statut != 200) {
        /* Le voyant reste rouge jusqu'à la prochaine commande réussie. */
        if (corps.length() == 0)
            corps = String("Erreur ") + String(statut);
        erreur(sortie, corps);
        return;
    }
    if (question)
        envoyer_reponse(sortie, corps);
    else
        sortie.println("OK");
    voyant_etat();
}

/* Montre sur le port USB ce que la calculatrice envoie, pour vérifier le
 * câble : les octets illisibles (mauvaise vitesse, fils inversés…)
 * apparaissent en « \xNN ». Les identifiants Wi-Fi sont masqués. */
static void journal_calculatrice(String const &ligne, bool incomplete)
{
    String s = ligne;
    if (s.startsWith("WIFI") && s.indexOf('\t') >= 0)
        s = s.substring(0, s.indexOf('\t')) + " (identifiants masqués)";
    Serial.print(incomplete ? "Calculatrice (sans fin de ligne) > "
                            : "Calculatrice > ");
    for (unsigned i = 0; i < s.length(); i++) {
        uint8_t c = s[i];
        if (c < 0x20 || c == 0x7f)
            Serial.printf("\\x%02X", c);
        else
            Serial.write(c);
    }
    Serial.println();
}

/* Lit les caractères disponibles et traite chaque ligne complète. Avec
 * [journal], les lignes reçues sont aussi montrées sur le port USB, et un
 * début de ligne resté seul 2 s est montré puis oublié. */
static void lire(Stream &entree, String &tampon, bool journal)
{
    static uint32_t dernier_octet;
    while (entree.available()) {
        char c = entree.read();
        if (journal)
            dernier_octet = millis();
        if (c == '\r')
            continue;
        if (c == '\n') {
            if (journal)
                journal_calculatrice(tampon, false);
            traiter(entree, tampon);
            tampon = "";
        } else if (tampon.length() < LONGUEUR_MAX_LIGNE) {
            tampon += c;
        }
    }
    if (journal && tampon.length() && millis() - dernier_octet > 2000) {
        journal_calculatrice(tampon, true);
        tampon = "";
    }
}

//---
// Programme principal
//---

/* Pourquoi l'ESP32 a (re)démarré : utile si une commande reste sans
 * réponse. */
static char const *raison_demarrage(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "mise sous tension";
    case ESP_RST_SW:       return "redémarrage demandé";
    case ESP_RST_PANIC:    return "PLANTAGE (erreur du programme)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "PLANTAGE (programme bloqué)";
    case ESP_RST_BROWNOUT: return "tension trop faible (alimentation USB ?)";
    case ESP_RST_USB:      return "par l'USB (téléversement, moniteur série)";
    default:               return "autre";
    }
}

static String tampon_usb, tampon_calculatrice;

void setup()
{
    Serial.begin(USB_VITESSE);
    Calculatrice.setRxBufferSize(2048);
    Calculatrice.begin(CALC_VITESSE, SERIAL_8N1, CALC_RX, CALC_TX);
    tampon_usb.reserve(LONGUEUR_MAX_LIGNE);
    tampon_calculatrice.reserve(LONGUEUR_MAX_LIGNE);

    voyant(0, 0, 255);
    Serial.println();
    Serial.println("Claude sur la calculatrice : ESP32 prêt.");
    Serial.printf("Démarrage : %s.\n", raison_demarrage());
    Serial.println("Commandes : PING, Q <question>, NOUV, SCAN, WIFI, WIFIC");

    /* Les réseaux sont retenus par nous (charger_reseaux), pas par le
     * Wi-Fi lui-même. */
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
#if SOC_WIFI_SUPPORT_5G
    /* La C5 choisit seule entre 2,4 et 5 GHz. */
    WiFi.setBandMode(WIFI_BAND_MODE_AUTO);
#endif
    WiFi.setAutoReconnect(true);
    WiFi.onEvent(sur_deconnexion, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.onEvent(sur_adresse, ARDUINO_EVENT_WIFI_STA_GOT_IP);
    /* L'heure se règle toute seule dès que le Wi-Fi est connecté. */
    configTzTime(FUSEAU, "pool.ntp.org", "time.nist.gov");

    charger_reseaux();
    Serial.printf("%d réseau(x) retenu(s).\n", nb_reseaux);
    auto_relancer();
}

void loop()
{
    suivre_wifi();
    auto_etape();
    lire(Serial, tampon_usb, false);
    lire(Calculatrice, tampon_calculatrice, true);
    delay(5);
}
