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
 *   PING                 PONG <rssi> <heure> : force du Wi-Fi en dBm et
 *                        heure « hh:mm:ss » (« - » si inconnues)
 *   Q <question>         ATT, puis une ligne « R <texte> » par ligne de la
 *                        réponse, puis FIN ; ou ERR <message>
 *   NOUV                 OK (nouvelle conversation) ; ou ERR <message>
 *   (autre chose)        ERR Commande inconnue.
 *
 * Carte : ESP32-C5 (Arduino-ESP32 3.3 ou plus récent, carte « ESP32C5 Dev
 * Module »). Pour la version WROOM-1U, une antenne Wi-Fi doit être branchée
 * sur la prise U.FL, sinon le Wi-Fi ne capte presque rien. */
#include <HTTPClient.h>
#include <WiFi.h>
#include <time.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copier secrets.example.h en secrets.h (même dossier) et le remplir."
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

/* Heure : réglée par Internet (NTP), au fuseau de Montréal / Québec, heure
 * d'été comprise. Règle POSIX : voir la documentation de la fonction tzset. */
#define FUSEAU "EST5EDT,M3.2.0,M11.1.0"

HardwareSerial Calculatrice(1);

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
// Wi-Fi
//---

static void demarrer_wifi(void)
{
    WiFi.mode(WIFI_STA);
#if SOC_WIFI_SUPPORT_5G
    /* La C5 choisit seule entre 2,4 et 5 GHz. */
    WiFi.setBandMode(WIFI_BAND_MODE_AUTO);
#endif
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_NOM, WIFI_MOT_DE_PASSE);
    /* L'heure se règle toute seule dès que le Wi-Fi est connecté. */
    configTzTime(FUSEAU, "pool.ntp.org", "time.nist.gov");
}

/* Annonce les connexions et déconnexions sur le port USB. */
static void suivre_wifi(void)
{
    static wl_status_t precedent = WL_IDLE_STATUS;
    wl_status_t etat = WiFi.status();
    if (etat == precedent)
        return;
    precedent = etat;
    if (etat == WL_CONNECTED) {
        Serial.printf("Wi-Fi connecté : %s (ESP32 en %s)\n", WIFI_NOM,
            WiFi.localIP().toString().c_str());
        Serial.printf("Mini-API : http://%s:%d\n", MAC_ADRESSE, MAC_PORT);
    } else if (etat == WL_DISCONNECTED || etat == WL_CONNECTION_LOST) {
        Serial.println("Wi-Fi perdu, reconnexion...");
    } else if (etat == WL_NO_SSID_AVAIL) {
        Serial.printf("Wi-Fi « %s » introuvable (antenne branchée ?)\n",
            WIFI_NOM);
    }
    voyant_etat();
}

//---
// Mini-API du Mac
//---

/* Envoie une requête POST à la mini-API. Renvoie le code HTTP (négatif si
 * le Mac est injoignable) et place le corps de la réponse dans [corps]. */
static int appeler_mac(char const *chemin, String const &texte, String &corps)
{
    HTTPClient http;
    String url = String("http://") + MAC_ADRESSE + ":" + MAC_PORT + chemin;
    http.setConnectTimeout(5000);
    http.setTimeout(DELAI_HTTP_MS);
    if (!http.begin(url)) {
        corps = "Adresse du Mac invalide.";
        return -1;
    }
    http.addHeader("X-Code", MAC_CODE);
    http.addHeader("Content-Type", "text/plain; charset=utf-8");
    int statut = http.POST(texte);
    if (statut > 0)
        corps = http.getString();
    else
        corps = "Mac injoignable (serveur lancé ?)";
    http.end();
    return statut;
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

/* « PONG <rssi> <heure> » : la calculatrice en fait sa barre d'état. */
static void repondre_etat(Stream &sortie)
{
    char rssi[8] = "-", heure[12] = "-";
    if (WiFi.status() == WL_CONNECTED)
        snprintf(rssi, sizeof rssi, "%d", (int)WiFi.RSSI());
    struct tm t;
    /* Tant que l'heure n'a pas été reçue, l'horloge est en 1970. */
    if (getLocalTime(&t, 0) && t.tm_year > 120)
        strftime(heure, sizeof heure, "%H:%M:%S", &t);
    sortie.printf("PONG %s %s\n", rssi, heure);
}

static void erreur(Stream &sortie, String const &message)
{
    sortie.print("ERR ");
    sortie.println(message);
    voyant(255, 0, 0);
}

static void traiter(Stream &sortie, String ligne)
{
    ligne.trim();
    if (ligne.length() == 0)
        return;

    if (ligne == "PING") {
        repondre_etat(sortie);
        return;
    }

    bool question = ligne.startsWith("Q ");
    if (!question && ligne != "NOUV") {
        sortie.println("ERR Commande inconnue.");
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        erreur(sortie, "Pas de Wi-Fi.");
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

/* Lit les caractères disponibles et traite chaque ligne complète. */
static void lire(Stream &entree, String &tampon)
{
    while (entree.available()) {
        char c = entree.read();
        if (c == '\r')
            continue;
        if (c == '\n') {
            traiter(entree, tampon);
            tampon = "";
        } else if (tampon.length() < LONGUEUR_MAX_LIGNE) {
            tampon += c;
        }
    }
}

//---
// Programme principal
//---

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
    Serial.println("Commandes : PING, Q <question>, NOUV");
    demarrer_wifi();
}

void loop()
{
    suivre_wifi();
    lire(Serial, tampon_usb);
    lire(Calculatrice, tampon_calculatrice);
    delay(5);
}
