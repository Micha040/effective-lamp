# Vorschau: die Katzen in echter Größe auf einem Desktop (1920x1080, Anzeige 2x)
import sys, pathlib, math
from PIL import Image, ImageDraw, ImageFilter, ImageFont

def load(name):
    data = pathlib.Path(f'out/px/{name}.raw').read_bytes()
    nl = data.index(b'\n')
    w, h, ox, oy = map(int, data[:nl].split())
    return Image.frombytes('RGBA', (w, h), data[nl + 1:nl + 1 + w * h * 4]), ox, oy

def font(size, bold=False):
    for f in (['/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'] if bold else []) + ['/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf']:
        try:
            return ImageFont.truetype(f, size)
        except OSError:
            pass
    return ImageFont.load_default()

W, H = 1920, 1080
SCALE = int(sys.argv[2]) if len(sys.argv) > 2 else 2

# Hintergrund: weicher Verlauf
bg = Image.new('RGB', (W, H))
px = bg.load()
for y in range(H):
    for x in range(W):
        t = y / H
        r = int(28 + 40 * t + 30 * math.exp(-((x - 1300) ** 2 + (y - 300) ** 2) / 300000))
        g = int(46 + 55 * t + 40 * math.exp(-((x - 1300) ** 2 + (y - 300) ** 2) / 300000))
        b = int(84 + 70 * t + 60 * math.exp(-((x - 1300) ** 2 + (y - 300) ** 2) / 300000))
        px[x, y] = (r, g, b)
img = bg.convert('RGBA')

def window(x0, y0, x1, y1, title, body):
    sh = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle([x0 + 4, y0 + 10, x1 + 4, y1 + 14], 10, fill=(0, 0, 0, 110))
    img.alpha_composite(sh.filter(ImageFilter.GaussianBlur(14)))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([x0, y0, x1, y1], 8, fill=(250, 250, 250, 255), outline=(200, 200, 205, 255))
    d.rectangle([x0 + 1, y0 + 32, x1 - 1, y0 + 32], fill=(228, 228, 232, 255))
    d.text((x0 + 14, y0 + 8), title, fill=(30, 30, 30, 255), font=font(13))
    for k, sym in enumerate(['×', '□', '–']):
        cx = x1 - 23 - k * 46
        d.text((cx - 5, y0 + 6), sym, fill=(40, 40, 40, 255), font=font(14))
    body(d, x0, y0 + 33, x1, y1)

def notes(d, x0, y0, x1, y1):
    f = font(14)
    lines = ['Einkaufsliste', '', '- Katzenfutter (Huhn!)', '- Kratzbaum-Seil', '- Milch', '', 'Termine', '- Tierarzt Do 16:30']
    for i, t in enumerate(lines):
        d.text((x0 + 18, y0 + 16 + i * 24), t, fill=(40, 40, 40, 255), font=font(15, i in (0, 6)) if i in (0, 6) else f)

def browser(d, x0, y0, x1, y1):
    d.rounded_rectangle([x0 + 12, y0 + 10, x1 - 12, y0 + 38], 14, fill=(236, 236, 240, 255))
    d.text((x0 + 28, y0 + 16), 'wetter.example  ·  Hamburg', fill=(90, 90, 90, 255), font=font(13))
    d.text((x0 + 30, y0 + 70), 'Hamburg', fill=(30, 30, 30, 255), font=font(26, True))
    d.text((x0 + 30, y0 + 110), '7 °C  ·  Nieselregen', fill=(60, 60, 60, 255), font=font(18))
    for i in range(6):
        y = y0 + 170 + i * 46
        d.rounded_rectangle([x0 + 30, y, x1 - 30, y + 32], 6, fill=(242, 243, 246, 255))

def taskbar():
    d = ImageDraw.Draw(img)
    d.rectangle([0, H - 48, W, H], fill=(32, 34, 40, 255))
    for k in range(6):
        cx = W // 2 - 150 + k * 60
        d.rounded_rectangle([cx - 14, H - 38, cx + 14, H - 10], 6, fill=[(90, 140, 220), (240, 190, 80), (110, 190, 120), (220, 110, 110), (170, 130, 220), (200, 200, 205)][k] + (255,))
    d.text((W - 92, H - 34), '14:52', fill=(235, 235, 235, 255), font=font(13))

def place(name, gx, gy, left=False):
    im, ox, oy = load(name)
    if left:
        im = im.transpose(Image.FLIP_LEFT_RIGHT)
        ox = im.width - ox
    big = im.resize((im.width * SCALE, im.height * SCALE), Image.NEAREST)
    img.alpha_composite(big, (gx - ox * SCALE, gy - oy * SCALE))

window(150, 380, 1000, 900, 'Notizen', notes)
window(1120, 250, 1760, 820, 'Browser', browser)
taskbar()
place('fatos-loaf', 640, 380)
place('kosta-sit', 1560, 250, left=True)
img.convert('RGB').save(sys.argv[1])
