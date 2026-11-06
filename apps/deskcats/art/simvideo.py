# Setzt die Simulation (out/sim/frames.txt + Sprites) zu einem Video zusammen: python3 art/simvideo.py out.mp4 [start_s end_s]
import sys, pathlib, subprocess, math
from PIL import Image, ImageDraw, ImageFont

SW, SH, TB, S = 1280, 720, 40, 2
out_path = sys.argv[1]
t0 = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0
t1 = float(sys.argv[3]) if len(sys.argv) > 3 else 1e9

def font(size, bold=False):
    p = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf' if bold else '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
    try:
        return ImageFont.truetype(p, size)
    except OSError:
        return ImageFont.load_default()

_sprites = {}
def sprite(key):
    if key not in _sprites:
        data = pathlib.Path(f'out/sim/{key}.raw').read_bytes()
        nl = data.index(b'\n')
        w, h, ox, oy = map(int, data[:nl].split())
        im = Image.frombytes('RGBA', (w, h), data[nl + 1:nl + 1 + w * h * 4])
        _sprites[key] = im.resize((w * S, h * S), Image.NEAREST)
    return _sprites[key]

# Hintergrund
bg = Image.new('RGB', (SW, SH))
px = bg.load()
for y in range(SH):
    for x in range(SW):
        g = math.exp(-((x - 900) ** 2 + (y - 200) ** 2) / 260000)
        t = y / SH
        px[x, y] = (int(26 + 36 * t + 34 * g), int(42 + 50 * t + 44 * g), int(78 + 66 * t + 64 * g))
bg = bg.convert('RGBA')

_wins = {}
def window_img(w, h, title):
    k = (w, h, title)
    if k in _wins:
        return _wins[k]
    im = Image.new('RGBA', (w + 30, h + 30), (0, 0, 0, 0))
    sh = Image.new('RGBA', im.size, (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle([12, 18, w + 14, h + 20], 9, fill=(0, 0, 0, 90))
    from PIL import ImageFilter
    im.alpha_composite(sh.filter(ImageFilter.GaussianBlur(10)))
    d = ImageDraw.Draw(im)
    ox, oy = 8, 8
    d.rounded_rectangle([ox, oy, ox + w, oy + h], 7, fill=(250, 250, 251, 255), outline=(198, 198, 204, 255))
    d.rectangle([ox + 1, oy + 30, ox + w - 1, oy + 30], fill=(226, 226, 230, 255))
    d.text((ox + 12, oy + 8), title, fill=(32, 32, 32, 255), font=font(12))
    for k2, sym in enumerate(['×', '□', '–']):
        d.text((ox + w - 24 - k2 * 40, oy + 6), sym, fill=(50, 50, 50, 255), font=font(13))
    for i in range(min(8, (h - 60) // 28)):
        y = oy + 46 + i * 28
        d.rounded_rectangle([ox + 16, y, ox + 16 + int((w - 32) * (0.9 - 0.13 * (i % 3))), y + 12], 5, fill=(236, 237, 241, 255))
    _wins[k] = im
    return im

taskbar = Image.new('RGBA', (SW, TB), (30, 32, 38, 255))
d = ImageDraw.Draw(taskbar)
for k, c in enumerate([(90, 140, 220), (240, 190, 80), (110, 190, 120), (220, 110, 110), (170, 130, 220)]):
    cx = SW // 2 - 120 + k * 50
    d.rounded_rectangle([cx - 11, 9, cx + 11, 31], 5, fill=c + (255,))
d.text((SW - 70, 13), '14:52', fill=(230, 230, 230, 255), font=font(12))

cursor = Image.new('RGBA', (14, 20), (0, 0, 0, 0))
dc = ImageDraw.Draw(cursor)
dc.polygon([(0, 0), (0, 15), (4, 11), (7, 18), (9, 17), (6, 10), (11, 10)], fill=(255, 255, 255, 255), outline=(0, 0, 0, 255))

CAPTIONS = [
    (0.0, 5.6, 'Fatos döst auf den Notizen, Kosta sitzt auf dem Browser'),
    (6.0, 29.5, 'Füttern: beide kommen zum Napf, auch von hoch oben'),
    (32.0, 53.5, 'Spielen: Wollknäuel anspringen, schubsen, hinterherjagen'),
    (55.0, 65.8, 'Rufen: beide kommen zum Mauszeiger'),
    (66.0, 71.8, 'Fenster verschieben: die Katzen fahren mit'),
    (72.0, 78.5, 'Fenster schließen: wer oben sitzt, fällt runter'),
    (79.0, 86.5, 'Mit der Maus hochheben und woanders absetzen'),
    (87.0, 99.0, 'Schlafen schicken'),
]
cap_font = font(17)
_caps = {}
def caption_img(text):
    if text not in _caps:
        tw = int(ImageDraw.Draw(Image.new('RGBA', (1, 1))).textlength(text, font=cap_font))
        im = Image.new('RGBA', (tw + 28, 34), (0, 0, 0, 0))
        ImageDraw.Draw(im).rounded_rectangle([0, 0, tw + 27, 33], 9, fill=(16, 18, 24, 200))
        ImageDraw.Draw(im).text((14, 7), text, fill=(240, 240, 244, 255), font=cap_font)
        _caps[text] = im
    return _caps[text]

def flush(frame, ff, t):
    wins, cats, items, cur = frame
    im = bg.copy()
    # Fenster von hinten nach vorne, Katzen/Gegenstände direkt über ihrer Fläche
    for (wid, x0, y0, x1, y1, title) in reversed(wins):
        im.alpha_composite(window_img(x1 - x0, y1 - y0, title), (x0 - 8, y0 - 8))
        for (key, x, y, plat) in items:
            if plat == wid:
                im.alpha_composite(sprite(key), (x, y))
        for (key, x, y, plat, top) in cats:
            if plat == wid and not top:
                im.alpha_composite(sprite(key), (x, y))
    im.alpha_composite(taskbar, (0, SH - TB))
    for (key, x, y, plat) in items:
        if plat in (0, 1):
            im.alpha_composite(sprite(key), (x, y))
    for (key, x, y, plat, top) in cats:
        if plat == 1 and not top:
            im.alpha_composite(sprite(key), (x, y))
    for (key, x, y, plat, top) in cats:
        if top or (plat not in [w[0] for w in wins] and plat != 1):
            im.alpha_composite(sprite(key), (x, y))
    if cur:
        im.alpha_composite(cursor, cur)
    for a, b, text in CAPTIONS:
        if a <= t < b:
            im.alpha_composite(caption_img(text), (16, 14))
    ff.stdin.write(im.convert('RGB').tobytes())

ff = subprocess.Popen(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{SW}x{SH}', '-r', '12', '-i', '-',
                       '-vf', 'fps=24', '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', '20', '-movflags', '+faststart', out_path],
                      stdin=subprocess.PIPE)
frame = None
fi = -1
n = 0
for line in open('out/sim/frames.txt'):
    p = line.split()
    if p[0] == 'frame':
        if frame is not None and t0 <= fi / 12.0 < t1:
            flush(frame, ff, fi / 12.0)
            n += 1
        fi = int(p[1])
        frame = ([], [], [], None)
    elif p[0] == 'win':
        frame[0].append((int(p[1]), int(p[2]), int(p[3]), int(p[4]), int(p[5]), ' '.join(p[6:])))
    elif p[0] == 'cat':
        frame[1].append((p[2], int(p[3]), int(p[4]), int(p[5]), int(p[6])))
    elif p[0] == 'item':
        frame[2].append((p[1], int(p[2]), int(p[3]), int(p[4])))
    elif p[0] == 'cursor':
        frame = (frame[0], frame[1], frame[2], (int(p[1]), int(p[2])))
if frame is not None and t0 <= fi / 12.0 < t1:
    flush(frame, ff, fi / 12.0)
    n += 1
ff.stdin.close()
ff.wait()
print(out_path, n, 'Bilder')
