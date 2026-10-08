"""Adapte un texte à l'écran de la calculatrice (police, largeur, pages).

Utilisé par poser.py (essais dans le Terminal) et serveur.py (la mini-API).
"""

import pathlib
import unicodedata

POLICE = pathlib.Path(__file__).resolve().parent / "police.txt"

LARGEUR_ECRAN = 128  # pixels
LIGNES_PAR_PAGE = 7

# Caractères courants absents de la police, et leur équivalent affichable.
REMPLACEMENTS = {
    "’": "'", "‘": "'", "“": '"', "”": '"',
    "—": "-", "–": "-", "−": "-", " ": " ", " ": " ",
    "•": "-", "·": ".", "œ": "oe", "Œ": "OE", "æ": "ae", "Æ": "AE",
    "→": "->", "←": "<-", "⇒": "=>", "≤": "<=", "≥": ">=", "≠": "!=",
    "≈": "~", "√": "racine", "π": "pi", "²": "^2", "³": "^3", "°": " deg",
    "÷": "/", "∞": "infini", "\t": " ", "\r": "",
}


def charger_police():
    """Largeur en pixels de chaque caractère de la police du jeu."""
    largeurs = {}
    courant = None
    for ligne in POLICE.read_text(encoding="utf-8").splitlines():
        ligne = ligne.rstrip()
        if ligne.startswith(": "):
            nom = ligne[2:].strip()
            courant = " " if nom == "space" else nom
        elif courant and ligne and set(ligne) <= set("#."):
            largeurs[courant] = len(ligne)
        elif not ligne:
            courant = None
    return largeurs


LARGEURS = charger_police()


def adapter(texte):
    """Ne garde que des caractères que la calculatrice sait afficher."""
    resultat = []
    for c in texte:
        c = REMPLACEMENTS.get(c, c)
        for d in c:
            if d == "\n" or d in LARGEURS:
                resultat.append(d)
                continue
            # Lettre accentuée absente de la police : on retire l'accent.
            base = unicodedata.normalize("NFD", d)[0]
            resultat.append(base if base in LARGEURS else "?")
    return "".join(resultat).strip()


def couper(texte):
    """Coupe le texte en lignes de LARGEUR_ECRAN pixels, comme l'écran."""
    def largeur(mot):
        return sum(LARGEURS.get(c, 5) for c in mot) + max(len(mot) - 1, 0)

    espace = LARGEURS[" "] + 2  # l'espace et les deux pixels d'écart autour
    lignes = []
    for paragraphe in texte.split("\n"):
        ligne, l = "", 0
        for mot in paragraphe.split(" "):
            if not mot:
                continue
            lm = largeur(mot)
            if ligne and l + espace + lm <= LARGEUR_ECRAN:
                ligne, l = ligne + " " + mot, l + espace + lm
            else:
                if ligne:
                    lignes.append(ligne)
                ligne, l = mot, lm
        lignes.append(ligne)
    while lignes and not lignes[-1]:
        lignes.pop()
    return lignes


def afficher(lignes):
    """Montre les lignes page par page, dans un cadre d'écran."""
    pages = [lignes[i:i + LIGNES_PAR_PAGE]
             for i in range(0, len(lignes), LIGNES_PAR_PAGE)] or [[]]
    largeur = max([21] + [len(l) for l in lignes])
    for n, page in enumerate(pages, 1):
        titre = f" page {n}/{len(pages)} "
        print("┌" + titre.center(largeur + 2, "─") + "┐")
        for ligne in page + [""] * (LIGNES_PAR_PAGE - len(page)):
            print("│ " + ligne.ljust(largeur) + " │")
        print("└" + "─" * (largeur + 2) + "┘")
