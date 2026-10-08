#!/usr/bin/env python3
"""Pose une question à Claude comme si elle venait de la calculatrice.

    python3 poser.py "C'est quoi une dérivée ?"     # continue la conversation
    python3 poser.py --nouvelle "Salut !"            # nouvelle conversation
    python3 poser.py --modele haiku "Une blague ?"   # autre modèle

Claude reçoit les consignes de consignes.md (écran de calculatrice, texte
brut, réponses courtes) et n'a accès à aucun outil : il ne peut ni lire ni
modifier de fichier. Il passe par l'abonnement Claude du Mac (Claude Code),
sans clé API.

La réponse est affichée telle que la calculatrice la montrerait : seulement
les caractères de sa police, coupée en lignes de 128 pixels, page par page.
"""

import argparse
import json
import pathlib
import subprocess
import sys

from calculatrice import adapter, afficher, couper

DOSSIER = pathlib.Path(__file__).resolve().parent
CONSIGNES = DOSSIER / "consignes.md"
CONVERSATION = DOSSIER / ".conversation"  # identifiant de la conversation en cours


def demander(question, nouvelle, modele):
    """Envoie la question à Claude Code et renvoie (réponse, données)."""
    commande = [
        "claude", "-p",
        "--safe-mode",               # ignore les réglages perso, garde l'abonnement
        "--tools", "",               # aucun outil : Claude ne fait que répondre
        "--output-format", "json",
        "--system-prompt", CONSIGNES.read_text(encoding="utf-8"),
    ]
    if modele:
        commande += ["--model", modele]
    if not nouvelle and CONVERSATION.exists():
        commande += ["--resume", CONVERSATION.read_text().strip()]
    commande.append(question)

    sortie = subprocess.run(commande, cwd=DOSSIER, capture_output=True,
                            text=True, timeout=300)
    try:
        donnees = json.loads(sortie.stdout)
    except json.JSONDecodeError:
        sys.exit("erreur : Claude Code n'a pas répondu correctement\n"
                 + sortie.stderr.strip())
    if donnees.get("is_error"):
        sys.exit("erreur : " + str(donnees.get("result")))
    CONVERSATION.write_text(donnees["session_id"])
    return donnees["result"], donnees


def main():
    parser = argparse.ArgumentParser(
        description="Pose une question à Claude comme la calculatrice.")
    parser.add_argument("question")
    parser.add_argument("--nouvelle", action="store_true",
                        help="commencer une nouvelle conversation")
    parser.add_argument("--modele",
                        help="modèle à utiliser (par exemple haiku, sonnet, opus)")
    parser.add_argument("--brut", action="store_true",
                        help="afficher aussi la réponse d'origine de Claude")
    args = parser.parse_args()

    reponse, donnees = demander(args.question, args.nouvelle, args.modele)
    if args.brut:
        print(reponse)
        print()
    afficher(couper(adapter(reponse)))
    modeles = ", ".join(donnees.get("modelUsage", {})) or "?"
    secondes = donnees.get("duration_ms", 0) / 1000
    print(f"({modeles}, {secondes:.1f} s)")


if __name__ == "__main__":
    main()
