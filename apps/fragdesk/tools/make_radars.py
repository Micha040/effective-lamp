#!/usr/bin/env python3
"""FragDesk – Radarbilder und Kartenwerte für die 2D-Ansicht erzeugen.

Quelle ist CS Demo Manager (https://github.com/akiver/cs-demo-manager): die Radarbilder unter
static/images/maps/cs2/radars/ und die Kartenwerte (pos_x, pos_y, scale, threshold_z) aus
src/node/database/maps/default-maps.ts. Die Bilder stammen aus Counter-Strike 2 (© Valve Corporation);
FragDesk liefert sie wie CS Demo Manager nur mit, um Spielerpositionen darauf anzuzeigen.

Die Bilder werden auf höchstens 1024 px verkleinert und auf 256 Farben (mit Transparenz) reduziert, damit
FragDesk.exe klein bleibt. Ergebnis:
  res/radars/<karte>.png, res/radars/<karte>_lower.png
  res/radars.rc                 (Ressourcen, in res/fragdesk.rc eingebunden)
  src/core/radar_maps.inc       (Tabelle für src/core/radar.cpp)

Aufruf: python3 tools/make_radars.py <Pfad zu cs-demo-manager>
"""
import pathlib
import re
import sys

from PIL import Image

# Untergrenze der oberen Ebene laut Valve (resource/overviews/<karte>.txt, "verticalsections"), wo sie von
# CS Demo Manager abweicht
OFFICIAL_LOWER = {'de_nuke': -495, 'de_train': -50, 'de_vertigo': 11700}
BASE_ID = 300   # Ressourcen-IDs: BASE_ID + 2 * Index (obere Ebene), + 1 (untere Ebene)


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    csdm = pathlib.Path(sys.argv[1])
    root = pathlib.Path(__file__).resolve().parent.parent
    ts = (csdm / 'src/node/database/maps/default-maps.ts').read_text(encoding='utf-8')
    entries = re.findall(r"\{\s*name: '([^']+)',\s*game: Game\.CS2,\s*position_x: (-?[\d.]+),\s*position_y: (-?[\d.]+),"
                         r"\s*scale: ([\d.]+),\s*threshold_z: (-?[\d.]+),", ts)
    src = csdm / 'static/images/maps/cs2/radars'
    out = root / 'res/radars'
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob('*.png'):
        old.unlink()

    rows, rc = [], []
    total = 0
    for name, x, y, scale, tz in sorted(entries):
        main_png = src / f'{name}.png'
        if not main_png.exists():
            continue
        lower_png = src / f'{name}_lower.png'
        native = Image.open(main_png).size[0]
        idx = len(rows)
        for png, suffix, rid in ((main_png, '', BASE_ID + 2 * idx), (lower_png, '_lower', BASE_ID + 2 * idx + 1)):
            if not png.exists():
                continue
            im = Image.open(png).convert('RGBA')
            if im.size[0] > 1024:
                im = im.resize((1024, 1024), Image.LANCZOS)
            q = im.quantize(colors=256, method=Image.Quantize.FASTOCTREE)
            dst = out / f'{name}{suffix}.png'
            q.save(dst, optimize=True)
            total += dst.stat().st_size
            rc.append(f'{rid} RCDATA "radars/{name}{suffix}.png"')
        lower = lower_png.exists()
        lz = float(OFFICIAL_LOWER.get(name, tz)) if lower else 0.0
        span = float(scale) * native   # Weltkoordinaten über die ganze Bildbreite
        rows.append(f'    {{"{name}", {float(x):.1f}f, {float(y):.1f}f, {span:.3f}f, {lz:.1f}f, {"true" if lower else "false"}}},')

    (root / 'res/radars.rc').write_text(
        '// erzeugt von tools/make_radars.py – Radarbilder aus Counter-Strike 2 (© Valve Corporation), über CS Demo Manager\n'
        + '\n'.join(rc) + '\n', encoding='utf-8')
    (root / 'src/core/radar_maps.inc').write_text(
        '// erzeugt von tools/make_radars.py aus den Kartenwerten von CS Demo Manager (Valve-Werte, wo sie abweichen)\n'
        '// Name, pos_x, pos_y (linke obere Ecke), Weltbreite des Bildes, Grenze zur unteren Ebene, untere Ebene vorhanden\n'
        + '\n'.join(rows) + '\n', encoding='utf-8')
    print(f'{len(rows)} Karten, {len(rc)} Bilder, {total // 1024} KB')


if __name__ == '__main__':
    main()
