#!/usr/bin/env python3
"""efficient lamp – Release-Dateien zusammenstellen: ein ZIP pro Programm, der Launcher und catalog.json.

Aufruf (im Repo-Hauptordner, nachdem tools/build-all.sh gelaufen ist):
    python3 tools/package.py --tag v1.0.0
    python3 tools/package.py --tag v1.0.0 --base-url http://127.0.0.1:8765/   # zum lokalen Testen

Ergebnis in dist/:
    EfficientLamp.exe, DeskCats.zip, SnowDesk.zip, TriggerSwitch.zip, WaveMuteSync.zip, FragDesk.zip, catalog.json,
    fragdesk-server-linux-x64 (eigener Server für FragDesk, lädt server/install.sh), SHA256SUMS.txt

Die ZIPs sind reproduzierbar (feste Zeitstempel, feste Reihenfolge): gleicher Quellstand -> gleiche Prüfsumme.
Vorher wird geprüft, dass jede Versionsnummer in app.json mit den Quelltexten übereinstimmt.
"""
import argparse
import datetime
import hashlib
import json
import pathlib
import re
import shutil
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
REPO = 'Micha040/effective-lamp'
SERVER_FILE = 'fragdesk-server-linux-x64'
CRLF_SUFFIXES = {'.ps1', '.cmd', '.bat'}
BOM_SUFFIXES = {'.ps1', '.cs'}   # Windows PowerShell 5.1 liest UTF-8 nur mit BOM richtig
ZIP_TIME = (2026, 1, 1, 12, 0, 0)


def fail(msg):
    print('FEHLER: ' + msg, file=sys.stderr)
    sys.exit(1)


def normalize(name, data):
    """Zeilenenden und BOM für Windows-Skripte vereinheitlichen."""
    suffix = pathlib.PurePath(name).suffix.lower()
    if suffix not in CRLF_SUFFIXES | BOM_SUFFIXES:
        return data
    text = data.decode('utf-8-sig').replace('\r\n', '\n')
    if suffix in CRLF_SUFFIXES or suffix == '.cs':
        text = text.replace('\n', '\r\n')
    out = text.encode('utf-8')
    return b'\xef\xbb\xbf' + out if suffix in BOM_SUFFIXES else out


def check_versions(app_dir, meta):
    for chk in meta.get('versionCheck', []):
        path = app_dir / chk['file']
        m = re.search(chk['pattern'], path.read_text(encoding='utf-8-sig'))
        if not m:
            fail(f"{meta['id']}: Version nicht gefunden in {chk['file']}")
        if m.group(1) != meta['version']:
            fail(f"{meta['id']}: app.json sagt {meta['version']}, {chk['file']} sagt {m.group(1)}")


def build_zip(app_dir, meta, out_path):
    pkg = meta['package']
    folder = pkg['folder']
    with zipfile.ZipFile(out_path, 'w') as z:
        for name in sorted(pkg['files']):
            src = app_dir / pkg['files'][name]
            if not src.is_file():
                fail(f"{meta['id']}: {src.relative_to(ROOT)} fehlt – erst tools/build-all.sh ausführen")
            info = zipfile.ZipInfo(f'{folder}/{name}', ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, normalize(name, src.read_bytes()), compresslevel=9)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def copy_server(src, out):
    """fragdesk-server: nur ein statisch gelinktes Linux-Programm für x86_64 (install.sh lädt es aus dem Release)."""
    if not src.is_file():
        fail(f'{src.relative_to(ROOT)} fehlt – erst tools/build-all.sh ausführen')
    data = src.read_bytes()
    if data[:4] != b'\x7fELF' or data[4] != 2 or int.from_bytes(data[18:20], 'little') != 62:
        fail(f'{src.name} ist kein Linux-Programm für x86_64')
    if b'/lib64/ld-linux' in data:
        fail(f'{src.name} ist nicht statisch gelinkt')
    shutil.copyfile(src, out / SERVER_FILE)
    (out / SERVER_FILE).chmod(0o755)


def write_notes(path, tag, base, launcher_version, apps):
    lines = [
        f'**Am einfachsten:** [EfficientLamp.exe]({base}EfficientLamp.exe) herunterladen und starten. '
        'Der Launcher installiert und aktualisiert alle Programme selbst und prüft jede Datei per SHA-256.',
        '',
        'Windows meldet beim ersten Start „Der Computer wurde durch Windows geschützt“ (das Programm ist nicht signiert): '
        '**Weitere Informationen → Trotzdem ausführen**.',
        '',
        '| Programm | Version | Download |',
        '|---|:-:|---|',
        f'| Launcher (efficient lamp) | {launcher_version} | [EfficientLamp.exe]({base}EfficientLamp.exe) |',
    ]
    for a in apps:
        d = a['download']
        lines.append(f"| {a['name']} – {a['tagline']} | {a['version']} | [{d['file']}]({d['url']}) |")
    lines += ['', 'Einzelne Programme: ZIP entpacken und `Setup.cmd` doppelklicken. Prüfsummen: `SHA256SUMS.txt`.', '',
              f'**Eigener Server für FragDesk** (Linux, x86_64): [{SERVER_FILE}]({base}{SERVER_FILE}) – installiert mit einem Befehl, '
              f'siehe [FragDesk › Eigener Server](https://github.com/{REPO}/tree/main/apps/fragdesk#eigener-server).', '']
    path.write_text('\n'.join(lines), encoding='utf-8')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--tag', required=True, help='Release-Tag, z. B. v1.0.0')
    ap.add_argument('--out', default=str(ROOT / 'dist'))
    ap.add_argument('--base-url', help='Download-Adresse der Release-Dateien (Standard: GitHub-Release des Tags)')
    ap.add_argument('--launcher', default=str(ROOT / 'launcher/build/EfficientLamp.exe'))
    ap.add_argument('--server', default=str(ROOT / 'apps/fragdesk/build' / SERVER_FILE))
    ap.add_argument('--notes', help='Release-Notizen (Markdown) in diese Datei schreiben')
    args = ap.parse_args()

    out = pathlib.Path(args.out)
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    base = args.base_url or f'https://github.com/{REPO}/releases/download/{args.tag}/'
    if not base.endswith('/') and not base.endswith('\\'):
        base += '/'

    # Launcher-Version aus dem Quelltext
    common = (ROOT / 'launcher/src/win/common.h').read_text(encoding='utf-8')
    lver = re.search(r'kVersionA = "([^"]+)"', common).group(1)
    for f, pat in (('launcher/src/win/common.h', r'kVersion = L"([^"]+)"'), ('launcher/res/launcher.rc', r'VALUE "ProductVersion", "([0-9.]+)\.0"'),
                   ('launcher/res/app.manifest', r'name="EfficientLamp" version="([0-9.]+)\.0"')):
        found = re.search(pat, (ROOT / f).read_text(encoding='utf-8')).group(1)
        if found != lver:
            fail(f'Launcher: {f} sagt {found}, erwartet {lver}')
    launcher_src = pathlib.Path(args.launcher)
    if not launcher_src.is_file():
        fail(f'{launcher_src} fehlt – erst tools/build-all.sh ausführen')
    shutil.copyfile(launcher_src, out / 'EfficientLamp.exe')
    copy_server(pathlib.Path(args.server), out)

    apps = []
    for app_json in sorted(ROOT.glob('apps/*/app.json')):
        meta = json.loads(app_json.read_text(encoding='utf-8'))
        app_dir = app_json.parent
        check_versions(app_dir, meta)
        zip_name = meta['package']['folder'] + '.zip'
        build_zip(app_dir, meta, out / zip_name)
        entry = {k: meta[k] for k in ('id', 'name', 'version', 'tagline', 'description', 'requires', 'color') if k in meta}
        entry['page'] = f'https://github.com/{REPO}/tree/main/apps/{meta["id"]}#readme'
        entry['download'] = {
            'file': zip_name,
            'url': base + zip_name,
            'size': (out / zip_name).stat().st_size,
            'sha256': sha256(out / zip_name),
            'folder': meta['package']['folder'],
        }
        for k in ('process', 'installDir', 'detect', 'install', 'uninstall', 'open'):
            if k in meta:
                entry[k] = meta[k]
        apps.append((meta.get('order', 99), entry))
    apps.sort(key=lambda a: a[0])

    catalog = {
        'schema': 1,
        'name': 'efficient lamp',
        'release': args.tag,
        'published': datetime.date.today().isoformat(),
        'repo': REPO,
        'launcher': {
            'version': lver,
            'file': 'EfficientLamp.exe',
            'url': base + 'EfficientLamp.exe',
            'size': (out / 'EfficientLamp.exe').stat().st_size,
            'sha256': sha256(out / 'EfficientLamp.exe'),
        },
        'apps': [a for _, a in apps],
    }
    (out / 'catalog.json').write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

    sums = [f'{sha256(p)}  {p.name}' for p in sorted(out.iterdir()) if p.is_file()]
    (out / 'SHA256SUMS.txt').write_text('\n'.join(sums) + '\n', encoding='utf-8')
    if args.notes:
        write_notes(pathlib.Path(args.notes), args.tag, base, lver, [a for _, a in apps])
    print(f'Release {args.tag} in {out}:')
    for p in sorted(out.iterdir()):
        print(f'  {p.name:<26} {p.stat().st_size:>9,} Bytes'.replace(',', '.'))
    for _, a in apps:
        print(f"  {a['name']} {a['version']}")


if __name__ == '__main__':
    main()
