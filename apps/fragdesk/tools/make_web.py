#!/usr/bin/env python3
"""FragDesk – Handy-App einbauen.

Aufruf (im Repo-Hauptordner):  python3 apps/fragdesk/tools/make_web.py [--icons]

Schreibt apps/fragdesk/res/web.rc: alle Dateien aus apps/fragdesk/web als RCDATA-Ressourcen (Nummern ab 2000), und
apps/fragdesk/src/win/web_assets.inc mit der Zuordnung Pfad -> Nummer, damit FragDesk.exe die Handy-App ohne
zusätzliche Dateien ausliefert.
--icons: erzeugt außerdem die App-Symbole (icon-180/192/512.png) aus brand/tiles/fragdesk.svg – randlos, weil
iOS und Android die Ecken selbst abrunden. Braucht dafür rsvg-convert.
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
WEB = ROOT / 'apps/fragdesk/web'
OUT = ROOT / 'apps/fragdesk/res/web.rc'
INC = ROOT / 'apps/fragdesk/src/win/web_assets.inc'
SVG = ROOT / 'brand/tiles/fragdesk.svg'


def icons():
    svg = SVG.read_text(encoding='utf-8')
    full = re.sub(r'<rect width="256" height="256" rx="\d+"', '<rect width="256" height="256"', svg, count=1)
    for size in (180, 192, 512):
        out = WEB / f'icon-{size}.png'
        subprocess.run(['rsvg-convert', '-w', str(size), '-h', str(size), '-o', str(out)], input=full.encode(), check=True)
        print('geschrieben:', out.relative_to(ROOT))


def rc():
    files = sorted(p for p in WEB.rglob('*') if p.is_file() and not p.name.startswith('.'))
    head = '// Erzeugt von tools/make_web.py – Handy-App (apps/fragdesk/web) als Ressourcen. Nicht von Hand ändern.'
    lines, inc = [head], [head]
    for i, p in enumerate(files):
        rel = p.relative_to(WEB).as_posix()
        lines.append(f'{2000 + i} RCDATA "../web/{rel}"')
        inc.append(f'{{"{rel}", {2000 + i}}},')
    OUT.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    INC.write_text('\n'.join(inc) + '\n', encoding='utf-8')
    print('geschrieben:', OUT.relative_to(ROOT), 'und', INC.relative_to(ROOT), f'({len(files)} Dateien)')


if __name__ == '__main__':
    if '--icons' in sys.argv:
        icons()
    rc()
