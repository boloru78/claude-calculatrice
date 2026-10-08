#!/usr/bin/env python3
"""Faux ESP32 pour le simulateur : répond à une requête du protocole.

Le simulateur l'appelle à chaque échange, avec la requête dans la variable
d'environnement SIM_REQUETE (« PING », « Q <question> » ou « NOUV »), et lit
les lignes de la réponse sur sa sortie. Le mode est choisi par SIM_ESP32 :

    faux      réponses toutes faites, toujours les mêmes (par défaut :
              captures d'écran et vérifications automatiques)
    mac       la vraie mini-API du Mac (mac/serveur.py doit tourner) :
              on discute vraiment avec Claude depuis l'app simulée
    sanswifi  l'ESP32 répond, mais n'a pas de Wi-Fi
    absent    l'ESP32 ne répond pas (câble débranché)
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


def main():
    mode = os.environ.get("SIM_ESP32", "faux")
    requete = os.environ.get("SIM_REQUETE", "")
    if mode == "absent":
        return

    if requete == "PING":
        if mode == "sanswifi":
            repondre(["PONG - -"])
        elif mode == "mac":
            repondre(["PONG -52 " + datetime.datetime.now().strftime("%H:%M:%S")])
        else:
            repondre(["PONG -58 14:03:27"])
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
