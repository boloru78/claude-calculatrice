#!/bin/sh
# Démarre la mini-API de la calculatrice et empêche le Mac de se mettre en
# veille tant qu'elle tourne (l'écran, lui, peut s'éteindre). Ctrl+C l'arrête.
cd "$(dirname "$0")" && exec caffeinate -i python3 serveur.py "$@"
