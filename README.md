# Claude sur la calculatrice

Poser des questions à Claude depuis une Casio fx-9750GIII. La calculatrice
envoie la question à un ESP32-C5 par son port 3 broches ; l'ESP32 la
transmet en Wi-Fi à ce Mac, qui la pose à Claude avec l'abonnement Claude
(Claude Code), **sans clé API ni coût en plus**.

```
Calculatrice ──câble 2,5 mm──► ESP32-C5 ──Wi-Fi──► Mac (mini-API) ──► Claude
```

## Organisation

```
claude-calculatrice/
├── CLAUDE.md                 notes de suivi pour Claude Code (choix, état)
├── mac/                      La mini-API, sur ce Mac
│   ├── serveur.py              le serveur que l'ESP32 interroge
│   ├── demarrer.sh             le lance en empêchant le Mac de dormir
│   ├── consignes.md            ce que Claude sait de la calculatrice
│   ├── poser.py                poser une question depuis le Terminal
│   ├── calculatrice.py         adapte le texte à la police et à l'écran
│   ├── police.txt              la police de la calculatrice
│   ├── code-secret.txt         créé au premier lancement (ne pas publier)
│   └── conversation.txt        la conversation complète (ne pas publier)
├── esp32-c5/
│   └── claude_calculatrice/  Le firmware de l'ESP32-C5 (Arduino)
│       ├── claude_calculatrice.ino
│       ├── secrets.example.h   modèle des réglages secrets
│       └── secrets.h           à créer soi-même (ne pas publier)
└── calculatrice/             L'app de la calculatrice (add-in Claude.g1a)
    └── README.md               son mode d'emploi
```

## 1. La mini-API sur le Mac

```sh
cd ~/Documents/claude-calculatrice/mac
./demarrer.sh
```

Le serveur affiche son adresse (par exemple `http://192.168.1.42:8765`) et
crée `code-secret.txt`. Au premier lancement, macOS peut demander si Python
a le droit d'accepter des connexions : répondre **Autoriser**, sinon l'ESP32
ne pourra pas le joindre. Le Mac ne doit pas dormir : `demarrer.sh`
l'en empêche tant que le serveur tourne, mais un Mac portable fermé sur
batterie s'endort quand même.

| Requête | Effet |
| --- | --- |
| `GET /etat` | répond « OK » (sans code, pour tester) |
| `POST /question`, corps = la question | répond le texte, adapté à l'écran |
| `POST /nouvelle` | commence une nouvelle conversation |

Les requêtes `POST` portent le code secret dans l'en-tête `X-Code`. Essai
depuis le Terminal :

```sh
curl -X POST -H "X-Code: $(cat code-secret.txt)" --data-binary "Salut !" http://127.0.0.1:8765/question
```

Pour essayer Claude sans serveur : `python3 poser.py "ta question"` (voir
`--help`).

**Lire la conversation sur le Mac :** tout ce que la calculatrice demande, et
les réponses complètes, sont notés dans `mac/conversation.txt` :

```sh
open ~/Documents/claude-calculatrice/mac/conversation.txt     # dans TextEdit
tail -f ~/Documents/claude-calculatrice/mac/conversation.txt  # en direct (Ctrl+C)
```

**Bonjour :** le serveur s'annonce sur le réseau local (`_claudecalc._tcp`).
L'ESP32 le retrouve ainsi tout seul, même si l'adresse du Mac change ou si
on change de réseau.

**Rapidité :** le serveur garde Claude Code démarré pendant une
conversation. La 1re question prend environ 7 s, les suivantes 3 à 4 s.
Claude Code s'arrête après 15 minutes sans question.

**Réglages** en tête de `serveur.py` : 60 questions par heure au plus,
1 000 caractères par question, 60 s d'attente au plus. Pour changer de
modèle : `./demarrer.sh --modele haiku` (ou `sonnet`, `opus`).

## 2. Le firmware de l'ESP32-C5

1. Copier `secrets.example.h` en `secrets.h` et le remplir : contenu de
   `code-secret.txt` (obligatoire), et si on veut le Wi-Fi de la maison et
   l'adresse du Mac (facultatifs : le Wi-Fi se choisit aussi depuis la
   calculatrice, et le Mac est retrouvé par Bonjour).
2. Ouvrir `claude_calculatrice.ino` dans l'Arduino IDE.
3. Carte : **ESP32C5 Dev Module** (Arduino-ESP32 3.3 ou plus récent).
4. Brancher l'ESP32 en USB, choisir son port, téléverser.
5. Ouvrir le moniteur série à **115200 bauds**, fin de ligne « Nouvelle
   ligne ».

La version **WROOM-1U** a besoin d'une **antenne Wi-Fi U.FL** branchée,
sinon elle ne capte presque rien. Le programme occupe 97 % de la place
prévue par défaut : si un jour l'IDE dit qu'il est trop gros, choisir
**Outils → Partition Scheme → Huge APP**.

Le voyant RGB indique l'état : bleu = connexion au Wi-Fi, vert = prêt,
jaune = Claude réfléchit, rouge = erreur. L'heure est réglée par Internet
(NTP) au fuseau de Montréal / Québec (`FUSEAU`).

### Tester sans la calculatrice

Dans le moniteur série, taper les mêmes commandes que la calculatrice :

| Commande | Réponse |
| --- | --- |
| `PING` | `PONG <rssi> <heure>`, par exemple `PONG -63 14:03:27` (`-` si inconnu) |
| `Q C'est quoi une dérivée ?` | `ATT`, puis une ligne `R …` par ligne de la réponse, puis `FIN` (ou `ERR message`) |
| `NOUV` | `OK` : nouvelle conversation |
| `SCAN` | la liste des réseaux Wi-Fi : `W <n> <rssi> <sécurité> <nom>` |
| `WIFI 2` + Tab + mot de passe | connexion au réseau n° 2 de la liste |

### Le Wi-Fi

Le réseau se choisit **depuis la calculatrice** (EXIT → Wi-Fi). L'ESP32 sait
se connecter aux réseaux :

- **ouverts**, y compris « ouverts améliorés » (OWE) ;
- **à mot de passe** : WPA, WPA2, WPA3, et les vieux réseaux WEP ;
- **à nom d'utilisateur et mot de passe** (WPA2/WPA3 Entreprise, PEAP ou
  TTLS) : école, travail, eduroam ;
- **cachés**, en tapant leur nom.

Il retient les 5 derniers réseaux qui ont marché (dans sa mémoire flash) et
s'y reconnecte tout seul, puis au réseau de `secrets.h`.

Ce qui ne marche pas :

- les réseaux qui demandent un **certificat** (WPA3 Entreprise 192 bits,
  EAP-TLS) ;
- les réseaux à **page de connexion web** (hôtels, cafés, réseaux invités) :
  l'ESP32 n'a pas de navigateur. Il le détecte et l'affiche ;
- pour que Claude réponde, **le Mac doit être sur le même réseau** et
  joignable : beaucoup de réseaux d'école ou publics isolent les appareils
  entre eux. Le compte rendu indique « Mac : introuvable » dans ce cas.

### Branchement à la calculatrice

Le port 3 broches de la calculatrice est une prise **jack 2,5 mm stéréo**
(3 contacts) en **3,3 V** : la même tension que l'ESP32.

| Jack 2,5 mm | Rôle côté calculatrice | ESP32-C5 |
| --- | --- | --- |
| pointe | réception | GPIO 5 (TX) |
| anneau | émission | GPIO 4 (RX) |
| corps | masse | GND |

**Vérifier au multimètre** quel contact est lequel, et la tension, avant
de brancher. Vitesse : 9600 bauds (`CALC_VITESSE`), à accorder avec l'app
de la calculatrice.

## 3. L'app de la calculatrice

L'add-in **Claude** (`calculatrice/`, détails dans son README) :

- en haut, l'**heure** avec les secondes et la **force du Wi-Fi** ;
- au milieu, la **conversation** en style terminal ;
- en bas, la **barre de texte** : EXE ouvre un **clavier visuel QWERTY**
  qu'on parcourt avec les flèches ;
- EXIT : nouvelle conversation, **Wi-Fi** (choisir le réseau de l'ESP32),
  aide.

Testée dans le simulateur (697 vérifications, tous les écrans). **Pas encore
essayée sur la calculatrice** : le port série passe par les fonctions du
système Casio (gint ne le gère pas encore), il faudra le vérifier avec le
câble.

## Sécurité et règles

- Claude est lancé **sans aucun outil** (`--tools ""`) : il ne peut ni lire,
  ni modifier, ni exécuter quoi que ce soit sur le Mac. `--safe-mode` ignore
  les réglages personnels de Claude Code mais garde la connexion à
  l'abonnement. Ne pas utiliser `--bare`, qui exige une clé API payante.
- La mini-API n'accepte que les appareils du **réseau local** qui
  connaissent le **code secret**.
- `secrets.h`, `code-secret.txt` et `conversation.txt` ne doivent **jamais**
  être publiés (déjà exclus par `.gitignore`).
- Les mots de passe Wi-Fi sont gardés **en clair** dans la mémoire de
  l'ESP32 : quelqu'un qui a la carte en main peut les lire.
- Réseaux Entreprise : l'ESP32 **ne vérifie pas le certificat** du réseau.
  Un faux point d'accès du même nom pourrait récupérer le nom d'utilisateur
  et le mot de passe. Éviter d'y mettre un compte important (celui de
  l'école donne souvent accès à tout le reste).
- Les questions comptent dans le quota de l'abonnement Claude, qui est
  personnel : ne pas ouvrir la mini-API à d'autres personnes.
- **Jamais en contrôle ni en examen.** Une calculatrice reliée à une IA,
  c'est de la triche.

## Matériel

- ESP32-C5 (WROOM-1U) et son **antenne Wi-Fi U.FL**
- **Fiche jack 2,5 mm stéréo à 3 contacts** (pas 4), par exemple à bornier
  à vis, ou un câble à couper
- Fils de connexion, et une batterie USB pour alimenter l'ESP32
