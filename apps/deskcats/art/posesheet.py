# Grundposen-Übersicht für die Abstimmung: zwei Reihen (Fatos, Kosta), vier Posen, stark vergrößert
import sys, pathlib
from PIL import Image, ImageDraw, ImageFont

def load(name):
    data = pathlib.Path(f'out/px/{name}.raw').read_bytes()
    nl = data.index(b'\n')
    w, h, ox, oy = map(int, data[:nl].split())
    return Image.frombytes('RGBA', (w, h), data[nl + 1:nl + 1 + w * h * 4]), ox, oy

def font(size, bold=False):
    path = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf' if bold else '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
    try:
        return ImageFont.truetype(path, size)
    except OSError:
        return ImageFont.load_default()

S = int(sys.argv[2]) if len(sys.argv) > 2 else 6
cats = [('Fatos', 'fatos'), ('Kosta', 'kosta')]
poses = [('Stehen', 'stand'), ('Sitzen', 'sit'), ('Liegen, Pfote hängt runter', 'loaf'), ('Schlafen', 'sleep')]
sprites = {(c, p): load(f'{c}-{p}') for _, c in cats for _, p in poses}
colw = [max(sprites[(c, p)][0].width for _, c in cats) * S + 40 for _, p in poses]
above = max(oy for (_, _, oy) in sprites.values())
below = max(im.height - oy for (im, _, oy) in sprites.values())
left = 150
rowh = (above + below) * S + 60
W = left + sum(colw) + 20
H = 70 + rowh * len(cats) + 10
sheet = Image.new('RGBA', (W, H), (36, 44, 58, 255))
d = ImageDraw.Draw(sheet)
x = left
for (label, _), cw in zip(poses, colw):
    tw = d.textlength(label, font=font(22))
    d.text((x + (cw - tw) / 2, 26), label, fill=(225, 228, 235, 255), font=font(22))
    x += cw
for r, (name, c) in enumerate(cats):
    y0 = 70 + r * rowh
    ground = y0 + 20 + above * S
    d.text((24, ground - 60), name, fill=(240, 240, 240, 255), font=font(30, True))
    # Fensterkante (Titelleiste) als Boden
    d.rectangle([left - 10, ground, W - 20, ground + 30], fill=(238, 238, 242, 255))
    d.rectangle([left - 10, ground + 30, W - 20, ground + 31], fill=(214, 214, 220, 255))
    x = left
    for (_, p), cw in zip(poses, colw):
        im, ox, oy = sprites[(c, p)]
        big = im.resize((im.width * S, im.height * S), Image.NEAREST)
        sheet.alpha_composite(big, (x + (cw - big.width) // 2, ground - oy * S))
        x += cw
sheet.convert('RGB').save(sys.argv[1])
