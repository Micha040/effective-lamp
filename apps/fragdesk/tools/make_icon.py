#!/usr/bin/env python3
"""FragDesk – Programmsymbol aus brand/tiles/fragdesk.svg erzeugen.

Aufruf (im Repo-Hauptordner):  python3 apps/fragdesk/tools/make_icon.py
Braucht: rsvg-convert (librsvg2-bin) und Pillow.

Erzeugt apps/fragdesk/res/fragdesk.ico (16–256 px). Für 16–24 px wird eine vereinfachte Fassung mit dickeren
Linien gezeichnet, damit das Fadenkreuz klein noch zu erkennen ist.
"""
import io
import pathlib
import subprocess

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[3]
SVG = ROOT / 'brand/tiles/fragdesk.svg'
OUT = ROOT / 'apps/fragdesk/res/fragdesk.ico'

SMALL = '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" width="256" height="256">
  <defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FFA24C"/><stop offset="1" stop-color="#E4570E"/></linearGradient></defs>
  <rect width="256" height="256" rx="48" fill="url(#g)"/>
  <circle cx="128" cy="128" r="74" fill="none" stroke="#FFFFFF" stroke-width="26"/>
  <g stroke="#FFFFFF" stroke-width="26" stroke-linecap="square">
    <line x1="128" y1="20" x2="128" y2="50"/><line x1="128" y1="206" x2="128" y2="236"/>
    <line x1="20" y1="128" x2="50" y2="128"/><line x1="206" y1="128" x2="236" y2="128"/>
  </g>
  <g fill="#FFFFFF"><rect x="96" y="128" width="28" height="40"/><rect x="134" y="96" width="28" height="72"/></g>
</svg>'''


def render(svg, size):
    data = svg.read_bytes() if isinstance(svg, pathlib.Path) else svg.encode('utf-8')
    png = subprocess.run(['rsvg-convert', '-w', str(size), '-h', str(size)], input=data, capture_output=True, check=True).stdout
    return Image.open(io.BytesIO(png)).convert('RGBA')


def main():
    sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
    imgs = [render(SMALL if s <= 24 else SVG, s) for s in sizes]
    imgs[-1].save(OUT, sizes=[(s, s) for s in sizes], append_images=imgs[:-1])
    print(f'{OUT.relative_to(ROOT)}  {OUT.stat().st_size // 1024} KB')


if __name__ == '__main__':
    main()
