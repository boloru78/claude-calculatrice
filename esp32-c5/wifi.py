#!/usr/bin/env python3
"""Choisit le Wi-Fi de l'ESP32 depuis le Mac, par le câble USB.

Fait la même chose que le menu Wi-Fi de la calculatrice (EXIT, puis
Wi-Fi) : recherche des réseaux, choix, mot de passe (tapé sans s'afficher),
puis compte rendu de la connexion. Pratique tant que le câble de la
calculatrice n'est pas prêt.

    python3 esp32-c5/wifi.py                 # trouve l'ESP32 tout seul
    python3 esp32-c5/wifi.py /dev/cu.usbmodem14101

L'ESP32 doit être branché en USB, et le moniteur série de l'Arduino IDE
fermé (un seul programme à la fois peut utiliser le port).
"""

import getpass
import glob
import os
import select
import sys
import termios
import time

SECURITES = {
    "O": "ouvert",
    "W": "mot de passe (WEP)",
    "P": "mot de passe",
    "E": "nom d'utilisateur + mot de passe",
    "X": "non pris en charge (certificat)",
}


class ESP32:
    """Le port série USB de l'ESP32, ligne par ligne."""

    def __init__(self, port):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(self.fd)
        attrs[0] = attrs[1] = attrs[3] = 0  # mode brut
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[4] = attrs[5] = termios.B115200
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        self.tampon = b""

    def lignes(self, duree):
        """Les lignes reçues pendant [duree] secondes au plus."""
        limite = time.time() + duree
        while time.time() < limite:
            while b"\n" in self.tampon:
                ligne, self.tampon = self.tampon.split(b"\n", 1)
                yield ligne.decode("utf-8", "replace").rstrip("\r")
            if select.select([self.fd], [], [], 0.1)[0]:
                try:
                    self.tampon += os.read(self.fd, 4096)
                except BlockingIOError:
                    pass

    def commande(self, texte, duree):
        """Envoie une commande ; renvoie ses lignes de réponse, jusqu'à la
        ligne finale (FIN, OK, PONG… ou ERR…), ou None sans réponse. Les
        messages de l'ESP32 (démarrage, Wi-Fi…) sont affichés en passant."""
        os.write(self.fd, texte.encode("utf-8") + b"\n")
        reponse = []
        for ligne in self.lignes(duree):
            if ligne == "ATT" or ligne.startswith(("R ", "W ", "PONG", "ERR")) \
                    or ligne in ("FIN", "OK"):
                reponse.append(ligne)
            elif ligne:
                print("  (ESP32) " + ligne)
            if ligne in ("FIN", "OK") or ligne.startswith(("PONG", "ERR")):
                return reponse
        return None


def trouver_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.usbserial*")
                   + glob.glob("/dev/cu.wchusbserial*"))
    if not ports:
        sys.exit("ESP32 introuvable : est-il branché en USB ?")
    if len(ports) == 1:
        return ports[0]
    for i, port in enumerate(ports, 1):
        print(f"{i}. {port}")
    return ports[int(input("Quel port ? ")) - 1]


def demander(question, choix):
    while True:
        reponse = input(question).strip().lower()
        if reponse in choix:
            return reponse


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else trouver_port()
    esp = ESP32(port)
    print(f"ESP32 sur {port}. Un instant…")
    for ligne in esp.lignes(4):  # l'ouverture du port peut le redémarrer
        if ligne:
            print("  (ESP32) " + ligne)

    etat = esp.commande("PING", 5)
    if not etat:
        sys.exit("L'ESP32 ne répond pas (moniteur série de l'IDE ouvert ?).")
    print("Recherche des réseaux…")
    reponse = esp.commande("SCAN", 30) or ["ERR pas de réponse"]
    if reponse[-1].startswith("ERR"):
        sys.exit("Erreur : " + reponse[-1][4:])

    reseaux = []
    for ligne in reponse:
        morceaux = ligne.split(" ", 4)
        if len(morceaux) == 5 and morceaux[0] == "W":
            _, numero, rssi, secu, nom = morceaux
            reseaux.append((numero, int(rssi), secu[0], secu.endswith("*"), nom))
    for numero, rssi, secu, connu, nom in reseaux:
        info = SECURITES.get(secu, "?") + (", mot de passe connu" if connu else "")
        print(f"{numero:>3}. {nom}  ({rssi} dBm, {info})")
    print("  c. réseau caché (à taper)")

    choix = demander("Ton choix (numéro, c, ou q pour quitter) : ",
                     {r[0] for r in reseaux} | {"c", "q"})
    if choix == "q":
        return

    champs = []
    if choix == "c":
        nom = input("Nom du réseau : ")
        secu = {"1": "O", "2": "P", "3": "E"}[demander(
            "Sécurité : 1 ouvert, 2 mot de passe, 3 nom + mot de passe : ",
            {"1", "2", "3"})]
        commande = f"WIFIC {secu}\t{nom}"
        connu = False
    else:
        _, _, secu, connu, nom = next(r for r in reseaux if r[0] == choix)
        commande = f"WIFI {choix}"
        if secu == "X":
            sys.exit("Ce réseau demande un certificat : non pris en charge.")

    if secu != "O" and not (connu and demander(
            "Utiliser le mot de passe connu ? (o/n) : ", {"o", "n"}) == "o"):
        if secu == "E":
            champs.append(input("Nom d'utilisateur : "))
        champs.append(getpass.getpass("Mot de passe (il ne s'affiche pas) : "))
    for champ in champs:
        commande += "\t" + champ

    print(f"Connexion à {nom}… (jusqu'à 1 min)")
    reponse = esp.commande(commande, 120)
    if not reponse:
        etat = esp.commande("PING", 5) or []
        morceaux = etat[-1].split(" ", 3) if etat else []
        if len(morceaux) == 4:
            sys.exit(f"Pas de compte rendu, mais l'ESP32 est connecté à "
                     f"{morceaux[3]}.")
        sys.exit("L'ESP32 ne répond pas.")
    for ligne in reponse:
        if ligne.startswith("R "):
            print(ligne[2:])
        elif ligne.startswith("ERR"):
            print("Erreur : " + ligne[4:])


if __name__ == "__main__":
    try:
        main()
    except (KeyboardInterrupt, EOFError):
        print()
