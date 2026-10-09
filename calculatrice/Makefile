# Outils de développement sur PC (macOS, Linux) pour l'app de la calculatrice.
# La compilation pour la calculatrice se fait avec le fxSDK : fxsdk build-fx
#
#   make test         tests de la logique de l'app
#   make sim          simulateur PC (build/claude-sim)
#   make check        tests + parcours de tous les écrans + ressources à jour
#   make screenshots  captures d'écran (docs/images/)
#   make assets       régénère src/assets.c et assets-fx/icon.png
#   make fx           compile l'add-in (raccourci pour fxsdk build-fx)
#
# Discuter avec le vrai Claude depuis l'app simulée (mac/serveur.py lancé) :
#   SIM_ESP32=mac ./build/claude-sim sim/essai.txt

CC ?= cc
PYTHON ?= python3
CFLAGS ?= -O1 -g
CFLAGS += -std=c11 -Wall -Wextra -Werror -Isrc -Isim -D_DEFAULT_SOURCE
SANITIZE ?= -fsanitize=address,undefined -fno-omit-frame-pointer

APP_SRC := src/app.c src/assets.c src/chat.c src/editor.c src/gfx.c \
           src/help.c src/keyboard.c src/protocol.c src/status.c src/ui.c \
           src/wifi.c
SIM_SRC := sim/main_sim.c sim/platform_sim.c
TEST_SRC := tests/test_main.c tests/test_protocol.c tests/test_chat.c \
            tests/test_keyboard.c tests/test_gfx.c sim/platform_sim.c
HEADERS := $(wildcard src/*.h sim/*.h tests/*.h)
SCRIPTS := $(wildcard sim/scripts/*.txt)

.PHONY: all test sim screenshots check check-texts assets check-assets fx clean

all: test sim

build/claude-sim: $(APP_SRC) $(SIM_SRC) $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(SANITIZE) -o $@ $(APP_SRC) $(SIM_SRC)

build/tests: $(APP_SRC) $(TEST_SRC) $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(SANITIZE) -o $@ $(APP_SRC) $(TEST_SRC)

sim: build/claude-sim

test: build/tests
	./build/tests

# Rejoue chaque script avec le faux ESP32 : échoue si un texte sort de l'écran.
check-texts: build/claude-sim
	@mkdir -p build/sim
	@for s in $(SCRIPTS); do \
	    echo "sim $$s"; \
	    SIM_ESP32=faux ./build/claude-sim -o build/sim $$s || exit 1; \
	done

screenshots: build/claude-sim
	@rm -rf build/shots && mkdir -p build/shots
	SIM_ESP32=faux ./build/claude-sim -o build/shots sim/scripts/screenshots.txt
	"$(PYTHON)" tools/pbm_to_png.py build/shots docs/images

assets:
	"$(PYTHON)" tools/gen_assets.py

check-assets:
	"$(PYTHON)" tools/gen_assets.py --check

check: test check-texts check-assets

fx:
	fxsdk build-fx

clean:
	rm -rf build build-fx Claude.g1a
