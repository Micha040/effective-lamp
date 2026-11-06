#!/usr/bin/env python3
"""efficient lamp – Logo, App-Kacheln und README-Banner aus den SVG-Quellen in brand/ erzeugen.

Aufruf (im Repo-Hauptordner):  python3 brand/make_brand.py
Braucht: rsvg-convert (librsvg2-bin), Pillow, die Schrift "Inter" (fonts-inter).

Erzeugt:
  launcher/res/efficientlamp.ico        Programmsymbol (16–256 px; 16–24 px aus logo-small.svg)
  launcher/res/logo.png                 Logo für die Kopfzeile des Launchers (128 px)
  launcher/res/tile-<app>.png           App-Kacheln für den Launcher (128 px)
  docs/media/banner-dark.png / -light.png   README-Banner (2560 x 720)
  docs/media/tiles/<app>.png            App-Kacheln für die README (96 px)
  docs/media/logo.png                   Logo 256 px
"""
import base64
import importlib.util
import io
import pathlib
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parent.parent
BRAND = ROOT / 'brand'
APPS = ['deskcats', 'snowdesk', 'triggerswitch', 'wavemutesync']


def svg_png(svg, size):
    """SVG (Pfad oder Text) -> Pillow-Bild in size x size."""
    data = svg.read_bytes() if isinstance(svg, pathlib.Path) else svg.encode('utf-8')
    w, h = (size, size) if isinstance(size, int) else size
    out = subprocess.run(['rsvg-convert', '-w', str(w), '-h', str(h)], input=data, capture_output=True, check=True).stdout
    return Image.open(io.BytesIO(out)).convert('RGBA')


def pixel_cat():
    spec = importlib.util.spec_from_file_location('make_icon', ROOT / 'apps/deskcats/tools/make_icon.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.base()   # 16 x 16, Inhalt in Zeile 1..14


def tile(app, size):
    if app == 'snowdesk':
        return Image.open(ROOT / 'apps/snowdesk/res/logo.png').convert('RGBA').resize((size, size), Image.LANCZOS)
    if app != 'deskcats':
        return svg_png(BRAND / 'tiles' / f'{app}.svg', size)
    # DeskCats: Pixel-Katze in ganzzahliger Vergrößerung, damit die Pixel scharf bleiben
    im = svg_png(BRAND / 'tiles/deskcats.svg', size)
    scale = max(1, round(size * 0.69 / 16))
    cat = pixel_cat().resize((16 * scale, 16 * scale), Image.NEAREST)
    x0 = (size - 16 * scale) // 2
    y0 = round(size * 0.47 - 8 * scale)           # Inhalt (Zeile 1..14) etwas über der Mitte
    shadow = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    bottom = y0 + 15 * scale
    ImageDraw.Draw(shadow).ellipse((size * 0.2, bottom - size * 0.035, size * 0.8, bottom + size * 0.06), fill=(43, 20, 102, 80))
    im.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(size / 90)))
    im.alpha_composite(cat, (x0, y0))
    return im


def data_uri(im):
    buf = io.BytesIO()
    im.save(buf, 'PNG')
    return 'data:image/png;base64,' + base64.b64encode(buf.getvalue()).decode('ascii')


BANNER = '''<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 1280 360" width="1280" height="360">
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="{bg0}"/>
      <stop offset="1" stop-color="{bg1}"/>
    </linearGradient>
    <radialGradient id="warm" cx="190" cy="180" r="420" gradientUnits="userSpaceOnUse">
      <stop offset="0" stop-color="#FFB23E" stop-opacity="{glow}"/>
      <stop offset="1" stop-color="#FFB23E" stop-opacity="0"/>
    </radialGradient>
  </defs>
  <rect width="1280" height="360" fill="url(#bg)"/>
  <rect width="1280" height="360" fill="url(#warm)"/>
  <image x="62" y="52" width="256" height="256" xlink:href="{logo}"/>
  <text x="356" y="178" font-family="Inter Display" font-weight="700" font-size="104" letter-spacing="-2.5"><tspan fill="{ink}">efficient</tspan><tspan fill="{amber}" dx="27">lamp</tspan></text>
  <text x="362" y="230" font-family="Inter" font-weight="500" font-size="30" fill="{muted}">{tagline}</text>
  {tiles}
</svg>'''


def banner(dark, logo, tiles):
    t = []
    for i, app in enumerate(APPS):
        t.append(f'<image x="{362 + i * 68}" y="262" width="52" height="52" xlink:href="{data_uri(tiles[app])}"/>')
    svg = BANNER.format(
        bg0='#0E1420' if dark else '#F8F9FC', bg1='#18233A' if dark else '#ECEFF5',
        glow='0.24' if dark else '0.20',
        ink='#F2F5FA' if dark else '#101828', amber='#FFB547' if dark else '#E08A00',
        muted='#93A1B5' if dark else '#56637A',
        tagline='Kleine, sparsame Windows-Tools – und ein Launcher für alles.',
        logo=data_uri(logo), tiles='\n  '.join(t))
    return svg_png(svg, (2560, 720))


def main():
    res = ROOT / 'launcher/res'
    media = ROOT / 'docs/media'
    (media / 'tiles').mkdir(parents=True, exist_ok=True)
    res.mkdir(parents=True, exist_ok=True)

    small = BRAND / 'logo-small.svg'
    big = BRAND / 'logo.svg'
    sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
    imgs = [svg_png(small if s <= 24 else big, s) for s in sizes]
    imgs[-1].save(res / 'efficientlamp.ico', sizes=[(s, s) for s in sizes], append_images=imgs[:-1])
    svg_png(big, 128).save(res / 'logo.png', optimize=True)
    logo512 = svg_png(big, 512)
    logo512.resize((256, 256), Image.LANCZOS).save(media / 'logo.png', optimize=True)

    tiles = {}
    for app in APPS:
        tiles[app] = tile(app, 256)
        tile(app, 128).save(res / f'tile-{app}.png', optimize=True)
        tile(app, 96).save(media / 'tiles' / f'{app}.png', optimize=True)

    banner(True, logo512, tiles).save(media / 'banner-dark.png', optimize=True)
    banner(False, logo512, tiles).save(media / 'banner-light.png', optimize=True)
    for p in sorted(list(res.glob('*.png')) + list(res.glob('*.ico')) + list(media.rglob('*.png'))):
        print(f'{p.relative_to(ROOT)}  {p.stat().st_size // 1024} KB')


if __name__ == '__main__':
    sys.exit(main())
