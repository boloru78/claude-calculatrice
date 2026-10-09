#!/usr/bin/env python3
"""Faux ESP32 pour le simulateur : répond à une requête du protocole.

Le simulateur l'appelle à chaque échange, avec la requête dans la variable
d'environnement SIM_REQUETE (« PING », « Q <question> », « NOUV », « SCAN »,
« WIFI … » ou « WIFIC … »), et lit les lignes de la réponse sur sa sortie.
Le mode est choisi par SIM_ESP32 :

    faux      réponses toutes faites, toujours les mêmes (par défaut :
              captures d'écran et vérifications automatiques)
    mac       la vraie mini-API du Mac (mac/serveur.py doit tourner) :
              on discute vraiment avec Claude depuis l'app simulée
    sanswifi  l'ESP32 répond, mais n'a pas de Wi-Fi
    absent    l'ESP32 ne répond pas (câble débranché)

Les réseaux Wi-Fi sont toujours les mêmes, inventés (RESEAUX) ; le mot de
passe « mauvais » est refusé.
"""

import datetime
import os
import pathlib
import sys
import urllib.error
import urllib.request

PROJET = pathlib.Path(__file__).resolve().parent.parent.parent
CODE_SECRET = PROJET / "mac" / "code-secret.txt"
MINI_API = os.environ.get("SIM_MINI_API", "http://127.0.0.1:8765")

REPONSES_FAUSSES = {
    "pi": "Pi vaut environ 3,14159. C'est le rapport entre le tour d'un "
          "cercle et son diamètre.\nIl a une infinité de décimales.",
    "dérivée": "La dérivée mesure la pente d'une courbe en un point.\n"
               "- x^2 -> 2x\n- sin(x) -> cos(x)\n- e^x -> e^x",
}


# Réseaux « trouvés » par SCAN : force, sécurité (« * » : mot de passe
# connu), nom.
RESEAUX = [
    (-48, "P*", "MaisonWifi"),
    (-61, "E", "Ecole-Personnel"),
    (-67, "O", "Cafe du coin"),
    (-72, "P", "Voisin_5G"),
    (-80, "X", "Labo-Securise"),
    (-84, "W", "VieuxRouteur"),
    (-88, "P", "Un nom de réseau bien trop long"),
]
RESEAU_ACTUEL = "MaisonWifi"


def repondre(lignes):
    for ligne in lignes:
        print(ligne)


def reponse_fausse(question):
    for mot, texte in REPONSES_FAUSSES.items():
        if mot in question.lower():
            return texte
    return "Je suis le faux ESP32 du simulateur : pas de vrai Claude ici. " \
           "Lance le mode « mac » pour lui parler."


def appeler_mac(chemin, texte):
    code = CODE_SECRET.read_text().strip() if CODE_SECRET.exists() else ""
    requete = urllib.request.Request(
        MINI_API + chemin, data=texte.encode("utf-8"), method="POST",
        headers={"X-Code": code, "Content-Type": "text/plain; charset=utf-8"})
    try:
        with urllib.request.urlopen(requete, timeout=65) as r:
            return 200, r.read().decode("utf-8")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", errors="replace")
    except OSError:
        return -1, "Mac injoignable (serveur lancé ?)"


def connexion_wifi(requete):
    """Réponse à WIFI <n>⇥… ou WIFIC <sécurité>⇥<nom>⇥…"""
    champs = requete.split(" ", 1)[1].split("\t")
    if requete.startswith("WIFIC "):
        nom = champs[1] if len(champs) > 1 else ""
        identifiants = champs[2:]
    else:
        numero = int(champs[0]) if champs[0].isdigit() else 0
        if not 1 <= numero <= len(RESEAUX):
            return ["ERR Réseau inconnu : relance la recherche."]
        _, secu, nom = RESEAUX[numero - 1]
        identifiants = champs[1:]
        if secu.startswith("X"):
            return ["ERR Ce réseau demande un certificat : non pris en charge."]
        if not identifiants and secu not in ("O", "P*"):
            return ["ERR Mot de passe nécessaire."]
    if "mauvais" in identifiants:
        return ["ERR Mot de passe refusé."]
    return [f"R Wi-Fi : connecté à {nom}.", "R Internet : oui.",
            "R Mac : trouvé.", "FIN"]


def main():
    mode = os.environ.get("SIM_ESP32", "faux")
    requete = os.environ.get("SIM_REQUETE", "")
    if mode == "absent":
        return

    if requete == "PING":
        if mode == "sanswifi":
            repondre(["PONG - -"])
        elif mode == "mac":
            repondre(["PONG -52 " + datetime.datetime.now().strftime("%H:%M:%S")
                      + " " + RESEAU_ACTUEL])
        else:
            repondre(["PONG -58 14:03:27 " + RESEAU_ACTUEL])
        return

    if requete == "SCAN":
        repondre(["ATT"] + [f"W {i} {rssi} {secu} {nom}" for i, (rssi, secu, nom)
                            in enumerate(RESEAUX, 1)] + ["FIN"])
        return

    if requete.startswith(("WIFI ", "WIFIC ")):
        repondre(["ATT"] + connexion_wifi(requete))
        return

    if mode == "sanswifi" and requete.startswith(("Q ", "NOUV")):
        repondre(["ERR Pas de Wi-Fi."])
        return

    if requete == "NOUV":
        if mode == "mac":
            statut, corps = appeler_mac("/nouvelle", "")
            repondre(["OK"] if statut == 200 else ["ERR " + corps])
        else:
            repondre(["OK"])
        return

    if requete.startswith("Q "):
        question = requete[2:]
        repondre(["ATT"])
        if mode == "mac":
            statut, texte = appeler_mac("/question", question)
            if statut != 200:
                repondre(["ERR " + texte.replace("\n", " ")])
                return
        else:
            texte = reponse_fausse(question)
        repondre(["R " + ligne for ligne in texte.split("\n")] + ["FIN"])
        return

    repondre(["ERR Commande inconnue."])


if __name__ == "__main__":
    sys.exit(main())
