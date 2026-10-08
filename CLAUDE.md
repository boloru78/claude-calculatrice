# Notes pour Claude Code

Ce fichier est lu automatiquement par Claude Code au début de chaque session
ouverte dans ce dossier. Il résume le projet, les choix déjà faits avec
l'auteur et ce qu'il reste à faire. **Mettez à jour « État actuel » à la fin
de chaque session.**

## Le projet

Poser des questions à Claude depuis la Casio fx-9750GIII de l'auteur :

```
Calculatrice ──câble jack 2,5 mm──► ESP32-C5 ──Wi-Fi──► Mac (mini-API) ──► Claude
```

Trois parties, une par dossier (le README détaille tout) :

- `mac/` : la mini-API (`serveur.py`) qui pose les questions à Claude avec
  **Claude Code en mode `-p`**, donc avec l'abonnement Claude de l'auteur,
  sans clé API ; `consignes.md` dit à Claude qu'il répond sur l'écran de la
  calculatrice.
- `esp32-c5/` : le firmware Arduino de l'ESP32-C5 (relais série ↔ Wi-Fi).
- `calculatrice/` : l'add-in **Claude** (C + gint), sur la base des jeux
  Mines et Tetris (`jeux calculatrice/`, dans ce dossier mais exclu de Git :
  ce sont des dépôts à part).

## L'auteur et ses préférences

- **boloru78**, au Québec, sur un Mac Intel (macOS 15) avec Claude Desktop.
  Il débute avec Git et le Terminal : expliquer pas à pas.
- **Tout en français**, y compris les réponses de Claude.
- **« Si c'est payant, c'est non »** : pas d'API payante, pas de service
  payant. D'où le relais par l'abonnement Claude (Claude Code `-p`).
- A fini le secondaire : pas besoin des applications de maths avancées de la
  calculatrice (elles sont dans le système de toute façon, on ne peut pas les
  supprimer).

## Choix déjà faits (ne pas les remettre en cause sans demander)

- **Relais par le Mac** avec `claude -p --safe-mode --tools ""` : aucun outil
  pour Claude, réglages personnels ignorés, abonnement gardé. **Jamais
  `--bare`** (exige une clé API). Claude Code reste démarré pendant une
  conversation (`--input-format stream-json`) : 1re question ≈ 7 s, suivantes
  ≈ 3-4 s.
- **Mini-API** : réseau local seulement, code secret (`mac/code-secret.txt`,
  en-tête `X-Code`), 60 questions par heure, 1 000 caractères, 60 s.
- **Carte** : ESP32-C5-WROOM-1U (antenne U.FL externe obligatoire), carte
  Arduino « ESP32C5 Dev Module », Arduino-ESP32 3.3.11 installé sur le Mac.
  Liaison calculatrice sur GPIO 4 (RX) et 5 (TX), 9600 bauds.
- **Heure réglée par Internet** (NTP) via l'ESP32, fuseau
  `EST5EDT,M3.2.0,M11.1.0` (Montréal / Québec).
- **App de la calculatrice**, choisie par l'auteur :
  - en haut à gauche l'heure avec les secondes, en haut à droite l'icône
    Wi-Fi (4 barres selon le RSSI, × si l'ESP32 ne répond pas) ;
  - conversation en **style terminal** (`> question`, réponse dessous) ;
  - **clavier visuel QWERTY seulement** (pas de saisie directe par les
    touches), ouvert par EXE sur la barre de texte, parcouru aux flèches,
    3 pages (lettres, 123, éà), ⇧, ◀ ▶, ⌫, ↵.
- **Messagerie (Discord, Snapchat…) abandonnée** : Snapchat n'a pas d'accès
  officiel, les self-bots Discord sont interdits ; l'auteur a préféré Claude.

## Points techniques à connaître

- **gint ne pilote pas le port série** (`gint/serial.h` est un modèle vide,
  même dans la dernière version 2.11). L'add-in appelle les fonctions série
  du système Casio (syscalls `0x418` Serial_Open, `0x40C` ReadOneByte,
  `0x40E` BufferedTransmitOneByte, `0x413` ClearReceiveBuffer, `0x419`
  Close, `0x03B` RTC_GetTicks, d'après la liste de SimLo) pendant un
  `gint_world_switch()` : voir `calculatrice/src/link.S` et
  `platform_gint.c`. **Jamais essayé sur la vraie calculatrice.**
- **6 Ko de mémoire statique** seulement pour un add-in (`ram` dans
  `fx9860g.ld` de gint) : les gros tampons de l'app sont alloués sur le tas.
- **Port 3 broches** : jack 2,5 mm stéréo (3 contacts), pointe = réception
  de la calculatrice, anneau = émission, corps = masse, 3,3 V. À vérifier au
  multimètre avant de brancher.
- **Protocole** calculatrice ↔ ESP32 (lignes UTF-8) : `PING` → `PONG <rssi>
  <hh:mm:ss>` ; `Q <question>` → `ATT`, `R <ligne>`…, `FIN` ou `ERR <msg>` ;
  `NOUV` → `OK`. Décrit dans `calculatrice/src/protocol.h` et le firmware.
- **Docker n'est pas installé** : l'add-in est compilé par la CI GitHub
  (`.github/workflows/build.yml`, artefact `Claude-g1a`). `gh` est installé
  et connecté (compte boloru78). Pillow est dans
  `jeux calculatrice/.venv` (chemin avec une espace : le Makefile met
  `$(PYTHON)` entre guillemets ; `pip` de ce venv ne marche plus depuis son
  déplacement, utiliser `.venv/bin/python -m pip`).
- Commits signés avec l'adresse anonyme de GitHub
  (`169409751+boloru78@users.noreply.github.com`, réglée dans ce dépôt) : le
  compte refuse les envois qui exposent l'adresse e-mail.

## Commandes utiles

```sh
cd mac && ./demarrer.sh                     # la mini-API (Ctrl+C pour arrêter)
cd mac && python3 poser.py "question"       # Claude comme la calculatrice
cd calculatrice && make check PYTHON="../jeux calculatrice/.venv/bin/python"
cd calculatrice && make sim && SIM_ESP32=mac ./build/claude-sim -o build sim/essai.txt
```

Compiler le firmware sans l'IDE (≈ 2 min 30) :

```sh
"/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli" \
    compile --fqbn esp32:esp32:esp32c5 esp32-c5/claude_calculatrice
```

(`secrets.h` doit exister : copie de `secrets.example.h`.)

## Historique

- **08/10/2026** : création du projet. Essais de `claude -p` (abonnement,
  pas de clé API), `consignes.md` et `poser.py`, puis la mini-API (testée :
  code faux refusé, mémoire de conversation, nouvelle conversation). Firmware
  ESP32-C5 (compile sans avertissement). App de la calculatrice : 697
  vérifications, tous les écrans dans le simulateur, vérifiée avec les
  en-têtes de gint 2.11.
- **08/10/2026 (2e session)** : le dossier des jeux a été déplacé dans ce
  projet (exclu de Git). Délai d'inactivité de Claude Code ramené à 15 min.
  Dépôt GitHub public créé avec la CI (tests + add-in) ; première
  compilation de `Claude.g1a` réussie du premier coup, copié sur la
  calculatrice.

## État actuel (08/10/2026)

- Mini-API : fonctionne sur le Mac. Claude Code s'arrête après 15 min sans
  question (`INACTIVITE_MAX`, choisi par l'auteur ; c'était 30 min).
- Firmware : compile, **pas encore téléversé** sur la carte.
- App : dépôt public
  [boloru78/claude-calculatrice](https://github.com/boloru78/claude-calculatrice)
  (licence MIT), la CI compile `Claude.g1a` (72 Ko). **Copié sur la
  calculatrice le 08/10/2026, pas encore essayé** (sans l'ESP32, l'app doit
  démarrer et afficher la croix du Wi-Fi).
- `gh` a maintenant la permission « workflow » (la CI du Tetris peut être
  envoyée).

## Prochaines étapes

1. Essayer l'app sur la calculatrice sans l'ESP32 : démarrage, clavier
   visuel, menu, aide, MENU puis retour, SHIFT puis AC/ON, et la croix du
   Wi-Fi (chaque demande d'état sans réponse fige l'écran 0,6 s).
2. Matériel : antenne U.FL, fiche jack 2,5 mm stéréo à 3 contacts, fils,
   batterie USB.
3. Téléverser le firmware, remplir `secrets.h`, tester `PING` et `Q …` dans
   le moniteur série.
4. Brancher la calculatrice, vérifier au multimètre, puis tester le port
   série ; ajuster `link.S` / `platform_gint.c` si les syscalls se
   comportent autrement que prévu.
