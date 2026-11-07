#!/usr/bin/env python3
"""FragDesk – THIRD-PARTY-NOTICES.txt erzeugen: die Lizenztexte aller Go-Module, die in fdanalyze.exe stecken,
und der Hinweis zu den Radarbildern in FragDesk.exe (res/radars/NOTICE.txt).

Aufruf (im Ordner apps/fragdesk, nach dem Bauen von fdanalyze.exe):
    python3 tools/notices.py build/THIRD-PARTY-NOTICES.txt

Die Liste kommt von "go list -deps" für Windows – also genau die Module, die wirklich eingebaut werden.
Fehlt bei einem Modul der Lizenztext, bricht das Skript ab (lieber kein Paket als eines ohne Lizenzhinweis).
"""
import json
import os
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent.parent
LICENSE_NAMES = re.compile(r'^(licen[cs]e|copying|notice|patents)([._-].*)?$', re.IGNORECASE)

HEADER = """FragDesk – Hinweise zu Drittsoftware
====================================

FragDesk.exe und fdanalyze.exe sind Teil von efficient lamp (MIT-Lizenz,
https://github.com/Micha040/effective-lamp).

fdanalyze.exe liest die Demos und enthält dafür die folgenden Open-Source-Bibliotheken,
allen voran cs-demo-analyzer von AkiVer und demoinfocs-golang von markus-wa (beide MIT-Lizenz).
Ihre Lizenztexte stehen unten vollständig, dazu die Lizenz von Go selbst.

FragDesk.exe enthält für die 2D-Ansicht die Radarbilder aus Counter-Strike 2 (© Valve Corporation),
übernommen aus CS Demo Manager von AkiVer (MIT-Lizenz) – Einzelheiten ganz unten.
"""


def go(*args, env=None):
    return subprocess.run(['go', *args], cwd=HERE / 'analyzer', env=env, capture_output=True, text=True, check=True).stdout


def modules():
    env = dict(os.environ, GOOS='windows', GOARCH='amd64', CGO_ENABLED='0')
    text = go('list', '-deps', '-json', '.', env=env)
    dec = json.JSONDecoder()
    pos, found = 0, {}
    while True:
        while pos < len(text) and text[pos].isspace():
            pos += 1
        if pos >= len(text):
            break
        pkg, pos = dec.raw_decode(text, pos)
        mod = pkg.get('Module')
        if not mod or mod.get('Main'):
            continue
        rep = mod.get('Replace') or {}
        found[mod['Path']] = (mod.get('Version', ''), rep.get('Dir') or mod.get('Dir'))
    return found


def license_text(path, folder):
    if not folder or not os.path.isdir(folder):
        sys.exit(f'FEHLER: Quelltext von {path} nicht gefunden ({folder})')
    names = sorted(n for n in os.listdir(folder) if LICENSE_NAMES.match(n) and os.path.isfile(os.path.join(folder, n)))
    if not names:
        sys.exit(f'FEHLER: {path} hat keine Lizenzdatei')
    parts = []
    for n in names:
        body = pathlib.Path(folder, n).read_text(encoding='utf-8', errors='replace').strip()
        parts.append(body if len(names) == 1 else f'--- {n}\n\n{body}')
    return '\n\n'.join(parts)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = pathlib.Path(sys.argv[1])
    mods = modules()
    blocks = [HEADER]
    for path in sorted(mods):
        version, folder = mods[path]
        blocks.append(f'\n=== {path} {version}\n\n{license_text(path, folder)}\n')
    goroot = go('env', 'GOROOT').strip()
    goversion = go('env', 'GOVERSION').strip()
    blocks.append(f'\n=== Go {goversion} (Standardbibliothek und Laufzeit)\n\n{license_text("Go", goroot)}\n')
    radars = HERE / 'res' / 'radars' / 'NOTICE.txt'
    if not radars.is_file():
        sys.exit(f'FEHLER: {radars} fehlt')
    blocks.append(f'\n=== Radarbilder in FragDesk.exe\n\n{radars.read_text(encoding="utf-8").strip()}\n')
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text('\n'.join(blocks).replace('\r\n', '\n'), encoding='utf-8')
    print(f'{out}: {len(mods)} Module')


if __name__ == '__main__':
    main()
