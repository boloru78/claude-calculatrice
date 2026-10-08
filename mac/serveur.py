#!/usr/bin/env python3
"""La mini-API de la calculatrice, qui tourne sur ce Mac.

L'ESP32 envoie une question ; ce serveur la pose à Claude (Claude Code, avec
l'abonnement Claude du Mac, sans clé API) et renvoie la réponse, adaptée à
l'écran de la calculatrice.

    python3 serveur.py                    # réseau local, port 8765
    python3 serveur.py --hote 127.0.0.1   # ce Mac seulement (essais)
    ./demarrer.sh                         # empêche aussi le Mac de dormir

Requêtes, en texte brut UTF-8 :

    GET  /etat                          -> « OK » (sans code, pour tester)
    POST /question   corps = question   -> la réponse
    POST /nouvelle                      -> « OK », nouvelle conversation

Les requêtes POST doivent porter le code secret dans l'en-tête X-Code (voir
code-secret.txt). En cas d'erreur, le corps est un message court, lisible
sur la calculatrice : 403 code faux ou appareil hors du réseau local,
413 question trop longue, 429 trop de questions, 503 Claude indisponible.

Sécurité : Claude est lancé sans aucun outil (il ne peut ni lire ni modifier
de fichier, ni exécuter de commande), et seuls les appareils du réseau local
qui connaissent le code secret sont acceptés.
"""

import argparse
import datetime
import http.server
import ipaddress
import json
import os
import pathlib
import queue
import secrets
import socket
import subprocess
import threading
import time

from calculatrice import adapter

DOSSIER = pathlib.Path(__file__).resolve().parent
CONSIGNES = DOSSIER / "consignes.md"
CODE_SECRET = DOSSIER / "code-secret.txt"

PORT = 8765
TAILLE_MAX_QUESTION = 1000      # caractères
QUESTIONS_PAR_HEURE = 60        # pour protéger le quota de l'abonnement
DELAI_REPONSE = 60              # secondes avant d'abandonner une question
                                # (l'ESP32 n'attend pas plus de 65 s)
INACTIVITE_MAX = 15 * 60        # Claude Code s'arrête après 15 min sans question


def journal(message):
    print(datetime.datetime.now().strftime("%H:%M:%S"), message, flush=True)


def resume(texte, n=60):
    """Début d'un texte, sur une ligne, pour le journal."""
    texte = " ".join(texte.split())
    return texte if len(texte) <= n else texte[:n - 1] + "…"


def lire_code_secret():
    """Lit le code secret, ou en crée un au premier lancement."""
    if not CODE_SECRET.exists():
        CODE_SECRET.write_text(secrets.token_urlsafe(12) + "\n")
        os.chmod(CODE_SECRET, 0o600)
    return CODE_SECRET.read_text().strip()


def adresse_locale():
    """Adresse IP de ce Mac sur le réseau local (aucune donnée envoyée)."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        try:
            s.connect(("192.168.1.1", 1))
            return s.getsockname()[0]
        except OSError:
            return "127.0.0.1"


class Claude:
    """Un Claude Code gardé démarré pendant toute une conversation.

    Il lit les questions sur son entrée et écrit ses réponses sur sa sortie,
    au format stream-json : la 2e question d'une conversation ne coûte plus
    le démarrage de Claude Code (quelques secondes de gagnées). Si le
    processus s'arrête, la conversation reprend grâce à son identifiant. """

    def __init__(self, modele=None):
        self.modele = modele
        self.processus = None
        self.sortie = queue.Queue()
        self.session = None          # identifiant de la conversation
        self.derniere_question = 0.0

    def _demarrer(self):
        commande = [
            "claude", "-p",
            "--safe-mode",           # ignore les réglages perso, garde l'abonnement
            "--tools", "",           # aucun outil : Claude ne fait que répondre
            "--input-format", "stream-json",
            "--output-format", "stream-json", "--verbose",
            "--system-prompt", CONSIGNES.read_text(encoding="utf-8"),
        ]
        if self.modele:
            commande += ["--model", self.modele]
        if self.session:
            commande += ["--resume", self.session]
        self.processus = subprocess.Popen(
            commande, cwd=DOSSIER, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
            bufsize=1)
        self.sortie = queue.Queue()
        threading.Thread(target=self._lire, args=(self.processus, self.sortie),
                         daemon=True).start()
        journal("Claude Code démarré"
                + (" (suite de la conversation)" if self.session else ""))

    @staticmethod
    def _lire(processus, sortie):
        """Transmet chaque ligne de la sortie de Claude Code à la file."""
        for ligne in processus.stdout:
            sortie.put(ligne)
        sortie.put(None)  # fin : le processus s'est arrêté

    def arreter(self):
        if self.processus and self.processus.poll() is None:
            self.processus.stdin.close()
            try:
                self.processus.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.processus.kill()
        self.processus = None

    def nouvelle_conversation(self):
        self.arreter()
        self.session = None

    def arreter_si_inactif(self):
        if self.processus and time.time() - self.derniere_question > INACTIVITE_MAX:
            journal("Claude Code arrêté (inactif)")
            self.arreter()

    def demander(self, question):
        """Pose la question ; renvoie la réponse, ou lève RuntimeError."""
        if not self.processus or self.processus.poll() is not None:
            self._demarrer()
        self.derniere_question = time.time()
        message = {"type": "user",
                   "message": {"role": "user", "content": question}}
        try:
            self.processus.stdin.write(json.dumps(message) + "\n")
            self.processus.stdin.flush()
        except OSError:
            self.arreter()
            raise RuntimeError("Claude Code s'est arrêté.")

        limite = time.time() + DELAI_REPONSE
        while True:
            try:
                ligne = self.sortie.get(timeout=max(limite - time.time(), 0.1))
            except queue.Empty:
                self.arreter()
                raise RuntimeError("Claude met trop de temps à répondre.")
            if ligne is None:
                self.arreter()
                raise RuntimeError("Claude Code s'est arrêté.")
            try:
                evenement = json.loads(ligne)
            except json.JSONDecodeError:
                continue
            if evenement.get("type") != "result":
                continue
            self.session = evenement.get("session_id") or self.session
            if evenement.get("is_error"):
                raise RuntimeError("Claude n'a pas pu répondre.")
            return evenement.get("result") or ""


class Serveur(http.server.ThreadingHTTPServer):
    def __init__(self, adresse, code, claude):
        super().__init__(adresse, Requete)
        self.code = code
        self.claude = claude
        self.verrou = threading.Lock()  # une question à la fois
        self.historique = []            # heures des dernières questions
        self.verrou_historique = threading.Lock()


class Requete(http.server.BaseHTTPRequestHandler):
    server_version = "CalculatriceClaude/1.0"

    def log_message(self, format, *args):
        pass  # le journal est tenu par journal()

    def repondre(self, statut, texte):
        corps = texte.encode("utf-8")
        self.send_response(statut)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(corps)))
        self.end_headers()
        self.wfile.write(corps)

    def autorise(self):
        """Seuls les appareils du réseau local avec le bon code passent."""
        ip = ipaddress.ip_address(self.client_address[0])
        if not (ip.is_private or ip.is_loopback):
            journal(f"refusé : {ip} n'est pas sur le réseau local")
            self.repondre(403, "Appareil hors du réseau local.")
            return False
        code = self.headers.get("X-Code", "")
        if not secrets.compare_digest(code, self.server.code):
            journal(f"refusé : code faux depuis {ip}")
            self.repondre(403, "Code secret incorrect.")
            return False
        return True

    def lire_corps(self):
        taille = int(self.headers.get("Content-Length") or 0)
        if taille > TAILLE_MAX_QUESTION * 4:
            return None
        return self.rfile.read(taille).decode("utf-8", errors="replace")

    def do_GET(self):
        if self.path == "/etat":
            self.repondre(200, "OK")
        else:
            self.repondre(404, "Adresse inconnue.")

    def do_POST(self):
        if self.path not in ("/question", "/nouvelle"):
            self.repondre(404, "Adresse inconnue.")
            return
        if not self.autorise():
            return
        corps = self.lire_corps()
        if corps is None:
            self.repondre(413, "Question trop longue.")
            return

        if self.path == "/nouvelle":
            with self.server.verrou:
                self.server.claude.nouvelle_conversation()
            journal("nouvelle conversation")
            self.repondre(200, "OK")
            return

        question = corps.strip()
        if not question:
            self.repondre(400, "Question vide.")
            return
        if len(question) > TAILLE_MAX_QUESTION:
            self.repondre(413, "Question trop longue.")
            return

        # Pas plus de QUESTIONS_PAR_HEURE questions par heure.
        with self.server.verrou_historique:
            maintenant = time.time()
            recentes = [t for t in self.server.historique
                        if maintenant - t < 3600]
            trop = len(recentes) >= QUESTIONS_PAR_HEURE
            if not trop:
                recentes.append(maintenant)
            self.server.historique = recentes
        if trop:
            self.repondre(429, "Trop de questions : réessaie plus tard.")
            return

        journal(f"question : {resume(question)}")
        debut = time.time()
        with self.server.verrou:
            try:
                reponse = adapter(self.server.claude.demander(question))
            except RuntimeError as erreur:
                journal(f"erreur : {erreur}")
                self.repondre(503, str(erreur))
                return
        journal(f"réponse en {time.time() - debut:.1f} s : {resume(reponse)}")
        self.repondre(200, reponse or "(réponse vide)")


def surveiller(serveur):
    """Arrête Claude Code quand personne ne pose de question."""
    while True:
        time.sleep(60)
        with serveur.verrou:
            serveur.claude.arreter_si_inactif()


def main():
    parser = argparse.ArgumentParser(description="Mini-API de la calculatrice.")
    parser.add_argument("--hote", default="0.0.0.0",
                        help="adresse d'écoute (0.0.0.0 : tout le réseau local)")
    parser.add_argument("--port", type=int, default=PORT)
    parser.add_argument("--modele",
                        help="modèle à utiliser (par exemple haiku, sonnet, opus)")
    args = parser.parse_args()

    code = lire_code_secret()
    serveur = Serveur((args.hote, args.port), code, Claude(args.modele))
    threading.Thread(target=surveiller, args=(serveur,), daemon=True).start()

    ip = adresse_locale() if args.hote == "0.0.0.0" else args.hote
    print(f"Mini-API de la calculatrice : http://{ip}:{args.port}")
    print(f"Code secret : dans {CODE_SECRET.name} (à recopier dans secrets.h)")
    print("Ctrl+C pour arrêter.", flush=True)
    try:
        serveur.serve_forever()
    except KeyboardInterrupt:
        print()
    finally:
        serveur.claude.arreter()
        journal("serveur arrêté")


if __name__ == "__main__":
    main()
