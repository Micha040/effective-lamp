# Pixel-Art-Werkzeug: Sprites als Textraster (ein Zeichen = ein Pixel), Paletten je Katze, Vorschau-Sheets.
import sys, pathlib
from PIL import Image, ImageDraw

PALETTES = {
    'fatos': {
        'o': (92, 46, 22),     # Kontur
        'd': (186, 98, 43),    # Schatten / Streifen
        'D': (150, 72, 30),    # tiefer Schatten
        'm': (229, 146, 76),   # Grundfarbe
        'l': (244, 185, 120),  # Glanz
        'c': (251, 228, 196),  # Creme (Brust, Schnauze)
        's': (226, 190, 152),  # Creme-Schatten
        'e': (205, 178, 74),   # Iris
        'k': (36, 24, 18),     # Pupille / Linien
        'w': (255, 255, 255),  # Lichtpunkt
        'n': (232, 140, 140),  # Nase
        'p': (236, 168, 160),  # Ohr innen / Ballen
    },
    'kosta': {
        'o': (12, 12, 16),
        'd': (24, 24, 30),
        'D': (16, 16, 20),
        'm': (38, 38, 46),
        'l': (72, 74, 92),
        'c': (246, 243, 236),
        's': (196, 192, 190),
        'e': (166, 206, 60),
        'k': (12, 12, 14),
        'w': (255, 255, 255),
        'n': (70, 56, 62),
        'p': (170, 128, 136),
    },
}

def parse(rows):
    rows = [r.rstrip('\n') for r in rows]
    w = max(len(r) for r in rows)
    return [r.ljust(w, '.') for r in rows]

def to_image(rows, pal):
    rows = parse(rows)
    h, w = len(rows), len(rows[0])
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    px = im.load()
    for y, r in enumerate(rows):
        for x, ch in enumerate(r):
            if ch in '. ':
                continue
            if ch not in pal:
                raise ValueError(f'unbekanntes Zeichen {ch!r} in Zeile {y}: {r}')
            px[x, y] = pal[ch] + (255,)
    return im

def sheet(frames, pal, scale=6, cols=4, bg=((112, 150, 190), (236, 236, 236)), label=True, ground=True):
    """frames: Liste (Name, rows). Hintergrund oben 'Wallpaper', unten 'Fensterkante' (Boden unter dem Sprite)."""
    ims = [(n, to_image(r, pal)) for n, r in frames]
    cw = max(i.width for _, i in ims) * scale + 24
    ch = max(i.height for _, i in ims) * scale + 40
    rows = (len(ims) + cols - 1) // cols
    out = Image.new('RGB', (cw * cols, ch * rows), bg[0])
    d = ImageDraw.Draw(out)
    for idx, (n, im) in enumerate(ims):
        cx, cy = (idx % cols) * cw, (idx // cols) * ch
        big = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
        ox = cx + (cw - big.width) // 2
        oy = cy + 10 + (ch - 40 - big.height)
        if ground:
            d.rectangle([cx, oy + big.height, cx + cw, cy + ch], fill=bg[1])
        out.paste(big, (ox, oy), big)
        if label:
            d.text((cx + 6, cy + 4), n, fill=(255, 255, 255))
    return out

# ---------------------------------------------------------------- Aufbau: Silhouette + Details

class Canvas:
    """Silhouette aus Bereichen: '#' Körper, 'f' fernes Bein (dahinter, dunkler), 't' Schwanz, 'h' Kopf.
    Danach Schattierung (Kontur, Licht oben, Schatten unten) und handgesetzte Details."""
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.m = [['.'] * w for _ in range(h)]
        self.det = [[None] * w for _ in range(h)]

    def span(self, y, x0, x1, ch='#'):
        for x in range(x0, x1 + 1):
            if 0 <= x < self.w and 0 <= y < self.h:
                self.m[y][x] = ch

    def spans(self, ch, rows):
        """rows: {y: (x0, x1)} oder {y: [(x0,x1), ...]}"""
        for y, v in rows.items():
            for a, b in (v if isinstance(v, list) else [v]):
                self.span(y, a, b, ch)

    def put(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.det[y][x] = c

    def puts(self, c, pts):
        for x, y in pts:
            self.put(x, y, c)

    def row(self, y, x0, text):
        """Details als Zeichenkette ab x0 setzen ('.' = nichts ändern, '_' = transparent)."""
        for i, c in enumerate(text):
            if c != '.':
                self.put(x0 + i, y, c)

    def render(self):
        w, h, m = self.w, self.h, self.m
        def at(x, y):
            return m[y][x] if 0 <= x < w and 0 <= y < h else '.'
        out = [['.'] * w for _ in range(h)]
        front = {'#', 'h', 't'}
        for y in range(h):
            for x in range(w):
                c = m[y][x]
                if c == '.':
                    continue
                n4 = [at(x - 1, y), at(x + 1, y), at(x, y - 1), at(x, y + 1)]
                if c == 'f':
                    # fernes Bein: Kontur zur Leere und zum Körper davor
                    if any(n == '.' for n in n4) or any(n in front for n in n4):
                        out[y][x] = 'o'
                    else:
                        out[y][x] = 'd' if at(x, y - 1) != '.' else 'm'
                    continue
                if any(n == '.' for n in n4) or (c == 't' and any(n == 'f' for n in n4) and False):
                    out[y][x] = 'o'
                    continue
                up, down = at(x, y - 1), at(x, y + 1)
                up2 = at(x, y - 2)
                if up in ('.',) or up2 == '.':
                    out[y][x] = 'l'
                elif down == '.' or at(x, y + 2) == '.':
                    out[y][x] = 'd'
                else:
                    out[y][x] = 'm'
        for y in range(h):
            for x in range(w):
                d = self.det[y][x]
                if d is None:
                    continue
                out[y][x] = '.' if d == '_' else d
        return [''.join(r) for r in out]

def show(rows):
    for i, r in enumerate(rows):
        print(f'{i:2d} {r}')
