# Compilation de l'add-in pour la calculatrice, avec le fxSDK :
#   fxsdk build-fx
# Le fichier Claude.g1a est produit à la racine de ce dossier.

cmake_minimum_required(VERSION 3.15)
project(Claude VERSION 1.1.0 LANGUAGES C ASM)

include(GenerateG1A)
find_package(Gint 2.9 REQUIRED)

# L'app est dans src/ ; le simulateur PC (sim/) n'est pas inclus.
set(SOURCES
  src/main.c
  src/platform_gint.c
  src/link.S
  src/app.c
  src/assets.c
  src/chat.c
  src/editor.c
  src/gfx.c
  src/help.c
  src/keyboard.c
  src/protocol.c
  src/status.c
  src/ui.c
  src/wifi.c
)

add_executable(claude ${SOURCES})
target_compile_options(claude PRIVATE -Wall -Wextra -Werror -Os -flto)
target_link_options(claude PRIVATE -flto)
target_link_libraries(claude Gint::Gint)

if("${FXSDK_PLATFORM_LONG}" STREQUAL fx9860G)
  # Le format g1a limite le nom affiché à 8 caractères.
  generate_g1a(TARGET claude OUTPUT "Claude.g1a"
    NAME "Claude" INTERNAL "@CLAUDE" VERSION "01.01.0000"
    ICON assets-fx/icon.png)
else()
  message(FATAL_ERROR "Cette app cible la fx-9750GIII : utiliser fxsdk build-fx")
endif()
