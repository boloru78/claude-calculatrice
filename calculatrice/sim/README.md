# Simulateur PC de l'app

Le simulateur exécute **le même code** que la calculatrice (tout `src/` sauf
`main.c`, `platform_gint.c` et `link.S`), sans fenêtre : il rejoue une suite
de touches écrite dans un script et enregistre les écrans demandés.

La liaison avec l'ESP32 est confiée à un **faux ESP32**,
[`esp32_simule.py`](esp32_simule.py), qui parle le même protocole. Son mode
se choisit avec la variable `SIM_ESP32` ou l'instruction `ESP32:` :

| Mode | Comportement |
| --- | --- |
| `faux` | réponses toutes faites, toujours les mêmes (par défaut) |
| `mac` | la vraie mini-API du Mac (`../mac/demarrer.sh` doit tourner) : le vrai Claude répond |
| `sanswifi` | l'ESP32 répond, mais sans Wi-Fi |
| `absent` | l'ESP32 ne répond pas (câble débranché) |

## Utilisation

```sh
make sim
./build/claude-sim [-o dossier] [-e faux-esp32.py] script.txt
python3 tools/pbm_to_png.py dossier dossier-png   # captures en PNG
```

Le programme renvoie `2` si un texte est sorti de l'écran pendant le script,
`1` en cas d'erreur dans le script, `0` sinon.

## Syntaxe des scripts

Des mots séparés par des espaces ou des retours à la ligne ; tout ce qui
suit un `#` est un commentaire.

| Instruction | Effet |
| --- | --- |
| `UP` `DOWN` `LEFT` `RIGHT` | flèches |
| `EXE` `SHIFT` `EXIT` `DEL` `AC` `F1` `F6` | touches du même nom |
| `MENU` | touche MENU : retour immédiat dans l'app |
| `OTHER` | une touche sans effet |
| `TEXTE:<texte>` | tape le texte **avec le clavier visuel** (flèches et EXE), clavier tout juste ouvert ; `_` = espace, `|` = ↵ (envoi) |
| `ESP32:<mode>` | change le mode du faux ESP32 |
| `WAIT:n` | laisse passer `n` tics de 100 ms |
| `DELAY:ms` | durée simulée de chaque appui (250 ms par défaut) |
| `SHOT:nom` | enregistre l'écran actuel dans `nom.pbm` |
| `REC:on` / `REC:off` | enregistre chaque image affichée |

Exemple : `EXE TEXTE:C'est_quoi_pi_?|` ouvre le clavier, tape la question et
l'envoie.

## Scripts

- [`scripts/screenshots.txt`](scripts/screenshots.txt) : tous les écrans
  (lancé par `make check` et `make screenshots`).
- [`essai.txt`](essai.txt) : une vraie question au vrai Claude (mode `mac`),
  hors de `make check`.
