/* secrets.example.h — Modèle des réglages secrets de l'ESP32.
 *
 * 1. Copier ce fichier en « secrets.h », dans le même dossier.
 * 2. Remplir les valeurs ci-dessous.
 *
 * secrets.h est exclu de Git (.gitignore) : il ne doit jamais être publié,
 * car il contient le mot de passe du Wi-Fi et le code secret de la
 * mini-API. */
#ifndef SECRETS_H
#define SECRETS_H

/* Réseau Wi-Fi de départ (2,4 ou 5 GHz : la C5 sait faire les deux).
 * Facultatif : le Wi-Fi se choisit aussi depuis la calculatrice (EXIT, puis
 * Wi-Fi), et l'ESP32 retient les réseaux qui ont marché. Laisser
 * « nom-du-wifi » pour ne rien mettre. */
#define WIFI_NOM "nom-du-wifi"
#define WIFI_MOT_DE_PASSE "mot-de-passe-du-wifi"

/* La mini-API sur le Mac : son adresse et son port sont affichés au
 * démarrage de mac/serveur.py (par exemple « http://192.168.1.42:8765 »).
 * L'adresse ne sert que si Bonjour ne trouve pas le Mac sur le réseau.
 * Le code secret, lui, est obligatoire : il est dans mac/code-secret.txt. */
#define MAC_ADRESSE "192.168.1.42"
#define MAC_PORT 8765
#define MAC_CODE "colle-ici-le-contenu-de-code-secret.txt"

#endif /* SECRETS_H */
