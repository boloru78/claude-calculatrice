#!/usr/bin/env python3
"""Convertit les écrans enregistrés par le simulateur en images pour le README.

    python3 tools/pbm_to_png.py <dossier des captures> <dossier de sortie>

Chaque capture NOM.pbm devient NOM.png, agrandie et colorée comme l'écran
LCD de la calculatrice. Si le dossier contient une animation (frames.txt,
écrite par REC:on / REC:off dans un script), elle devient demo.gif.
Nécessite Pillow.
"""

import pathlib
import sys

from PIL import Image

LCD_LIGHT = (196, 204, 186)  # fond de l'écran
LCD_DARK = (28, 34, 38)      # pixels allumés
SCALE_PNG = 4
SCALE_GIF = 3
BORDER = 2                   # marge autour de l'écran, en pixels de l'écran


def lcd(path, scale):
    """Ouvre une capture PBM et la met à l'échelle avec les couleurs du LCD."""
    mono = Image.open(path).convert("L")
    w, h = mono.size
    out = Image.new("RGB", (w + 2 * BORDER, h + 2 * BORDER), LCD_LIGHT)
    colored = Image.new("RGB", mono.size, LCD_LIGHT)
    colored.paste(LCD_DARK, mask=mono.point(lambda v: 255 if v < 128 else 0))
    out.paste(colored, (BORDER, BORDER))
    return out.resize((out.width * scale, out.height * scale), Image.NEAREST)


def make_gif(src, dst):
    index = src / "frames.txt"
    entries = [line.split() for line in index.read_text().splitlines() if line.strip()]
    if not entries:
        return
    frames, durations = [], []
    for i, (name, time) in enumerate(entries):
        next_time = int(entries[i + 1][1]) if i + 1 < len(entries) else int(time) + 2500
        duration = max(next_time - int(time), 20)
        image = lcd(src / name, SCALE_GIF).convert("P", palette=Image.ADAPTIVE, colors=2)
        # Deux images identiques à la suite : on allonge la première.
        if frames and image.tobytes() == frames[-1].tobytes():
            durations[-1] += duration
            continue
        frames.append(image)
        durations.append(duration)
    frames[0].save(dst / "demo.gif", save_all=True, append_images=frames[1:],
                   duration=durations, loop=0, optimize=True)
    print(f"écrit : {dst / 'demo.gif'} ({len(frames)} images)")


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 1
    src, dst = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    dst.mkdir(parents=True, exist_ok=True)
    for path in sorted(src.glob("*.pbm")):
        if path.name.startswith("frame-"):
            continue
        lcd(path, SCALE_PNG).save(dst / (path.stem + ".png"), optimize=True)
        print(f"écrit : {dst / (path.stem + '.png')}")
    if (src / "frames.txt").exists():
        make_gif(src, dst)
    return 0


if __name__ == "__main__":
    sys.exit(main())
