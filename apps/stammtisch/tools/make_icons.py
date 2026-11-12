#!/usr/bin/env python3
"""STAMMTISCH – App-Symbole aus brand/tiles/stammtisch.svg erzeugen.

Aufruf (im Repo-Hauptordner):  python3 apps/stammtisch/tools/make_icons.py
Braucht: rsvg-convert (librsvg2-bin).

Schreibt apps/stammtisch/web/icon-180.png, icon-192.png, icon-512.png – randlos, weil iOS und Android die Ecken selbst
abrunden – und docs/media/tiles/stammtisch.png (96 px, mit runden Ecken) für die README.
"""
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[3]
SVG = ROOT / 'brand/tiles/stammtisch.svg'
WEB = ROOT / 'apps/stammtisch/web'


def render(svg_text, size, out):
    subprocess.run(['rsvg-convert', '-w', str(size), '-h', str(size), '-o', str(out)], input=svg_text.encode(), check=True)
    print('geschrieben:', out.relative_to(ROOT))


def main():
    svg = SVG.read_text(encoding='utf-8')
    full = re.sub(r'<rect width="256" height="256" rx="\d+"', '<rect width="256" height="256"', svg, count=1)
    for size in (180, 192, 512):
        render(full, size, WEB / f'icon-{size}.png')
    render(svg, 96, ROOT / 'docs/media/tiles/stammtisch.png')


if __name__ == '__main__':
    main()
