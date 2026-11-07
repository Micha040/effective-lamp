#!/usr/bin/env python3
"""FragDesk – Handy-App einbauen.

Aufruf (im Repo-Hauptordner):  python3 apps/fragdesk/tools/make_web.py [--icons] [--cpp <datei>]

Schreibt apps/fragdesk/res/web.rc: alle Dateien aus apps/fragdesk/web als RCDATA-Ressourcen (Nummern ab 2000), und
apps/fragdesk/src/win/web_assets.inc mit der Zuordnung Pfad -> Nummer, damit FragDesk.exe die Handy-App ohne
zusätzliche Dateien ausliefert.
--icons: erzeugt außerdem die App-Symbole (icon-180/192/512.png) aus brand/tiles/fragdesk.svg – randlos, weil
iOS und Android die Ecken selbst abrunden. Braucht dafür rsvg-convert.
--cpp <datei>: schreibt stattdessen nur eine C++-Datei mit allen Dateien als Byte-Felder (für fragdesk-server unter
Linux, siehe server/build.sh). web.rc und web_assets.inc bleiben dann unverändert.
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
RADARS = ROOT / 'apps/fragdesk/res/radars'   # nur für --cpp (fragdesk-server); FragDesk.exe hat sie schon (res/radars.rc)


def web_files():
    return sorted(p for p in WEB.rglob('*') if p.is_file() and not p.name.startswith('.'))


def icons():
    svg = SVG.read_text(encoding='utf-8')
    full = re.sub(r'<rect width="256" height="256" rx="\d+"', '<rect width="256" height="256"', svg, count=1)
    for size in (180, 192, 512):
        out = WEB / f'icon-{size}.png'
        subprocess.run(['rsvg-convert', '-w', str(size), '-h', str(size), '-o', str(out)], input=full.encode(), check=True)
        print('geschrieben:', out.relative_to(ROOT))


def rc():
    files = web_files()
    head = '// Erzeugt von tools/make_web.py – Handy-App (apps/fragdesk/web) als Ressourcen. Nicht von Hand ändern.'
    lines, inc = [head], [head]
    for i, p in enumerate(files):
        rel = p.relative_to(WEB).as_posix()
        lines.append(f'{2000 + i} RCDATA "../web/{rel}"')
        inc.append(f'{{"{rel}", {2000 + i}}},')
    OUT.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    INC.write_text('\n'.join(inc) + '\n', encoding='utf-8')
    print('geschrieben:', OUT.relative_to(ROOT), 'und', INC.relative_to(ROOT), f'({len(files)} Dateien)')


def cpp(target):
    files = web_files()
    radars = sorted(RADARS.glob('*.png'))
    out = ['// Erzeugt von tools/make_web.py --cpp – Handy-App (apps/fragdesk/web) für fragdesk-server. Nicht von Hand ändern.',
           '#include "web_assets.h"', '', 'namespace fdsrv {', '', 'namespace {', '']
    table = []
    for i, p in enumerate(files):
        data = p.read_bytes()
        rel = p.relative_to(WEB).as_posix()
        out.append(f'// {rel}')
        out.append(f'const unsigned char f{i}[] = {{')
        for k in range(0, len(data), 24):
            out.append('    ' + ','.join(str(b) for b in data[k:k + 24]) + ',')
        out.append('    0};')
        table.append(f'    {{"{rel}", f{i}, {len(data)}}},')
    rtable = []
    for i, p in enumerate(radars):
        data = p.read_bytes()
        out.append(f'// Radarbild {p.name} (© Valve Corporation, über CS Demo Manager – siehe res/radars/NOTICE.txt)')
        out.append(f'const unsigned char r{i}[] = {{')
        for k in range(0, len(data), 24):
            out.append('    ' + ','.join(str(b) for b in data[k:k + 24]) + ',')
        out.append('    0};')
        rtable.append(f'    {{"{p.name}", r{i}, {len(data)}}},')
    out += ['', '}  // namespace', '', 'const WebFile kWebFiles[] = {'] + table + ['};',
            f'const size_t kWebFileCount = {len(files)};', '', 'const WebFile kRadarFiles[] = {'] + rtable + ['};',
            f'const size_t kRadarFileCount = {len(radars)};', '', '}  // namespace fdsrv', '']
    path = pathlib.Path(target)
    path.parent.mkdir(parents=True, exist_ok=True)
    text = '\n'.join(out)
    if not path.exists() or path.read_text(encoding='utf-8') != text:   # unverändert: nicht neu übersetzen
        path.write_text(text, encoding='utf-8')
    print('geschrieben:', path, f'({len(files)} Dateien, {len(radars)} Radarbilder)')


if __name__ == '__main__':
    if '--cpp' in sys.argv:
        i = sys.argv.index('--cpp')
        if i + 1 >= len(sys.argv):
            sys.exit('FEHLER: --cpp braucht einen Dateinamen')
        cpp(sys.argv[i + 1])
        sys.exit(0)
    if '--icons' in sys.argv:
        icons()
    rc()
