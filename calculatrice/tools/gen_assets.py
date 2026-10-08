#!/usr/bin/env python3
"""Convertit les ressources dessinées en texte (dossier assets/) en code C.

    assets/font.txt     -> police du jeu    -> src/assets.c, src/assets.h
    assets/sprites.txt  -> images du jeu    -> src/assets.c, src/assets.h
    assets/icon.txt     -> icône de l'add-in -> assets-fx/icon.png

Usage :
    python3 tools/gen_assets.py          # régénère les fichiers
    python3 tools/gen_assets.py --check  # vérifie qu'ils sont à jour (CI)

L'icône PNG demande Pillow (déjà requis par le fxSDK) ; avec --check elle
est comparée pixel par pixel.
"""

import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
FONT_TXT = ROOT / "assets" / "font.txt"
SPRITES_TXT = ROOT / "assets" / "sprites.txt"
ICON_TXT = ROOT / "assets" / "icon.txt"
OUT_C = ROOT / "src" / "assets.c"
OUT_H = ROOT / "src" / "assets.h"
OUT_ICON = ROOT / "assets-fx" / "icon.png"

FONT_HEIGHT = 7  # hauteur des majuscules, utilisée pour la mise en page
FONT_ROWS = 8    # avec la ligne des jambages
ICON_SIZE = (30, 19)
HEADER = "/* Fichier généré par tools/gen_assets.py à partir du dossier assets/.\n" \
         " * Ne pas le modifier à la main : éditer les .txt puis lancer make assets. */\n"


class AssetError(Exception):
    pass


def parse_pixels(lines, where):
    """Convertit des lignes de « # » et « . » en liste de lignes de booléens."""
    rows = []
    for number, line in lines:
        if any(c not in "#." for c in line):
            raise AssetError(f"{where}, ligne {number} : caractère inattendu dans {line!r}")
        rows.append([c == "#" for c in line])
    widths = {len(r) for r in rows}
    if len(widths) != 1:
        raise AssetError(f"{where} : toutes les lignes doivent avoir la même longueur")
    return rows


def read_blocks(path, marker):
    """Découpe un fichier en blocs « <marker> nom » suivis de lignes de pixels."""
    blocks = []
    current = None
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.rstrip()
        if line.startswith("#") and current is None:
            continue
        if line.startswith(marker + " "):
            current = (line[len(marker) + 1:].strip(), number, [])
            blocks.append(current)
        elif line == "" or (line.startswith("# ") and not set(line) <= set("#.")):
            current = None
        elif current is not None:
            current[2].append((number, line))
        else:
            raise AssetError(f"{path.name}, ligne {number} : pixels hors d'un bloc")
    return blocks


def load_font():
    glyphs = {}
    for name, number, lines in read_blocks(FONT_TXT, ":"):
        char = " " if name == "space" else name
        where = f"{FONT_TXT.name}, caractère {name!r} (ligne {number})"
        if len(char) != 1:
            raise AssetError(f"{where} : un seul caractère attendu")
        if char in glyphs:
            raise AssetError(f"{where} : caractère déjà défini")
        rows = parse_pixels(lines, where)
        if len(rows) not in (FONT_HEIGHT, FONT_ROWS):
            raise AssetError(f"{where} : {FONT_HEIGHT} ou {FONT_ROWS} lignes attendues, "
                             f"{len(rows)} trouvées")
        rows += [[False] * len(rows[0])] * (FONT_ROWS - len(rows))
        if not 1 <= len(rows[0]) <= 8:
            raise AssetError(f"{where} : largeur entre 1 et 8 pixels attendue")
        glyphs[char] = rows
    missing = [chr(c) for c in range(32, 127) if chr(c) not in glyphs]
    if missing:
        raise AssetError(f"{FONT_TXT.name} : caractères ASCII manquants : {''.join(missing)!r}")
    return glyphs


def load_sprites():
    sprites = []
    names = set()
    for name, number, lines in read_blocks(SPRITES_TXT, "@"):
        where = f"{SPRITES_TXT.name}, image {name!r} (ligne {number})"
        if not name.isidentifier() or name in names:
            raise AssetError(f"{where} : nom invalide ou déjà utilisé")
        rows = parse_pixels(lines, where)
        if not rows or len(rows[0]) > 32:
            raise AssetError(f"{where} : 1 à 32 pixels de large attendus")
        names.add(name)
        sprites.append((name, rows))
    return sprites


def load_icon():
    lines = [(n, l.rstrip()) for n, l in
             enumerate(ICON_TXT.read_text(encoding="utf-8").splitlines(), 1)
             if l.strip() and not l.startswith("# ")]
    rows = parse_pixels(lines, ICON_TXT.name)
    if (len(rows[0]), len(rows)) != ICON_SIZE:
        raise AssetError(f"{ICON_TXT.name} : l'icône doit faire {ICON_SIZE[0]}×{ICON_SIZE[1]} pixels")
    return rows


def bits(row, total):
    """Ligne de pixels -> entier, pixel de gauche sur le bit de poids fort."""
    value = 0
    for i, pixel in enumerate(row):
        if pixel:
            value |= 1 << (total - 1 - i)
    return value


def c_char_comment(char):
    return "espace" if char == " " else ("\\\\" if char == "\\" else char)


def generate(glyphs, sprites):
    order = [chr(c) for c in range(32, 127)]
    order += sorted((c for c in glyphs if ord(c) > 126), key=ord)

    h = [HEADER, "#ifndef ASSETS_H\n#define ASSETS_H\n\n#include <stdint.h>\n\n"]
    h.append("/* Hauteur d'une ligne de texte (les jambages débordent d'un pixel). */\n")
    h.append(f"#define FONT_HEIGHT {FONT_HEIGHT}\n#define FONT_ROWS {FONT_ROWS}\n\n")
    h.append("/* Caractère de la police : chaque ligne est un octet, pixel de gauche\n"
             " * sur le bit de poids fort. */\n")
    h.append("typedef struct {\n    uint32_t code;  /* point de code Unicode */\n"
             "    uint8_t width;  /* largeur en pixels */\n"
             "    uint8_t rows[FONT_ROWS];\n} glyph_t;\n\n")
    h.append("/* Image : chaque ligne est un mot de 32 bits, pixel de gauche sur le\n"
             " * bit de poids fort. */\n")
    h.append("typedef struct {\n    uint8_t w, h;\n    uint32_t const *rows;\n} sprite_t;\n\n")
    h.append("/* Les 95 premiers caractères sont l'ASCII imprimable (32 à 126) dans\n"
             " * l'ordre ; les suivants sont triés par point de code. */\n")
    h.append("extern glyph_t const FONT_GLYPHS[];\nextern int const FONT_GLYPH_COUNT;\n\n")
    for name, _ in sprites:
        h.append(f"extern sprite_t const SPR_{name.upper()};\n")
    h.append("\n#endif /* ASSETS_H */\n")

    c = [HEADER, '#include "assets.h"\n\n', "glyph_t const FONT_GLYPHS[] = {\n"]
    for char in order:
        rows = glyphs[char]
        width = len(rows[0])
        data = ", ".join(f"0x{bits(r, 8):02x}" for r in rows)
        c.append(f"    {{ 0x{ord(char):04x}, {width}, {{ {data} }} }}, /* {c_char_comment(char)} */\n")
    c.append("};\n\nint const FONT_GLYPH_COUNT = sizeof FONT_GLYPHS / sizeof *FONT_GLYPHS;\n")
    for name, rows in sprites:
        c.append(f"\nstatic uint32_t const rows_{name}[] = {{\n")
        for r in rows:
            art = "".join("#" if p else "." for p in r)
            c.append(f"    0x{bits(r, 32):08x}, /* {art} */\n")
        c.append(f"}};\nsprite_t const SPR_{name.upper()} = {{ {len(rows[0])}, {len(rows)}, rows_{name} }};\n")
    return "".join(h), "".join(c)


def icon_image(rows):
    from PIL import Image
    img = Image.new("L", ICON_SIZE, 255)
    for y, row in enumerate(rows):
        for x, pixel in enumerate(row):
            if pixel:
                img.putpixel((x, y), 0)
    return img


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true",
                        help="vérifie que les fichiers générés sont à jour")
    args = parser.parse_args()

    try:
        header, source = generate(load_font(), load_sprites())
        icon = icon_image(load_icon())
    except AssetError as error:
        print(f"erreur : {error}", file=sys.stderr)
        return 1

    if args.check:
        stale = [p.relative_to(ROOT) for p, text in ((OUT_H, header), (OUT_C, source))
                 if not p.exists() or p.read_text(encoding="utf-8") != text]
        from PIL import Image
        if not OUT_ICON.exists() or \
                Image.open(OUT_ICON).convert("L").tobytes() != icon.tobytes():
            stale.append(OUT_ICON.relative_to(ROOT))
        if stale:
            print("fichiers générés pas à jour (lancer make assets) : "
                  + ", ".join(map(str, stale)), file=sys.stderr)
            return 1
        print("ressources à jour")
        return 0

    OUT_H.write_text(header, encoding="utf-8")
    OUT_C.write_text(source, encoding="utf-8")
    OUT_ICON.parent.mkdir(exist_ok=True)
    icon.save(OUT_ICON)
    print(f"écrit : {OUT_H.relative_to(ROOT)}, {OUT_C.relative_to(ROOT)}, "
          f"{OUT_ICON.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
