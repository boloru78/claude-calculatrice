/* wifi.h — Menu Wi-Fi : choisir le réseau de l'ESP32 depuis la calculatrice.
 *
 *   EXIT, puis « Wi-Fi » : réseau actuel et force du signal, puis
 *     - Chercher les réseaux : liste des réseaux autour (SCAN), EXE sur
 *       l'un d'eux, puis mot de passe (et nom d'utilisateur pour les
 *       réseaux Entreprise) au clavier visuel ;
 *     - Réseau caché : son nom, sa sécurité, puis les identifiants.
 *
 * Le compte rendu de la connexion (réseau, Internet, Mac) s'affiche dans la
 * conversation. L'ESP32 retient les réseaux qui ont marché. */
#ifndef WIFI_H
#define WIFI_H

void wifi_menu(void);

#endif /* WIFI_H */
