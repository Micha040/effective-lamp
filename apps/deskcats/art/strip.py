# Filmstreifen + GIF einer Handlung: python3 art/strip.py <prefix> <scale> <step_px> [out.png] [out.gif] [loops]
import sys, glob, pathlib
from PIL import Image, ImageDraw

def load(p):
    data = pathlib.Path(p).read_bytes()
    nl = data.index(b'\n')
    w, h, ox, oy = map(int, data[:nl].split())
    return Image.frombytes('RGBA', (w, h), data[nl + 1:nl + 1 + w * h * 4]), ox, oy

prefix, S, step = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
out_png = sys.argv[4] if len(sys.argv) > 4 else f'out/{prefix}.png'
out_gif = sys.argv[5] if len(sys.argv) > 5 else f'out/{prefix}.gif'
loops = int(sys.argv[6]) if len(sys.argv) > 6 else 3
frames = [load(f) for f in sorted(glob.glob(f'out/px/{prefix}-*.raw'))]
above = max(oy for _, _, oy in frames)
below = max(im.height - oy for im, _, oy in frames)
left = max(ox for _, ox, _ in frames)
right = max(im.width - ox for im, ox, _ in frames)
cw = (left + right) * S + 16
H = (above + below) * S + 30
# Streifen
strip = Image.new('RGBA', (cw * len(frames), H), (52, 64, 84, 255))
d = ImageDraw.Draw(strip)
gy = 10 + above * S
d.rectangle([0, gy, strip.width, gy + 8], fill=(236, 236, 240, 255))
for i, (im, ox, oy) in enumerate(frames):
    big = im.resize((im.width * S, im.height * S), Image.NEAREST)
    strip.alpha_composite(big, (i * cw + 8 + (left - ox) * S, gy - oy * S))
    d.text((i * cw + 4, 2), str(i), fill=(200, 200, 200, 255))
strip.save(out_png)
# GIF in Echtzeit (12 Bilder/s); bei Gang-Zyklen läuft die Katze über das Bild
n = len(frames) * loops
travel = step * n
GW = (left + right + travel) * S + 20
GH = (above + below) * S + 20
gifs = []
for k in range(n):
    im, ox, oy = frames[k % len(frames)]
    fr = Image.new('RGBA', (GW, GH), (52, 64, 84, 255))
    dd = ImageDraw.Draw(fr)
    gy = 10 + above * S
    dd.rectangle([0, gy, GW, gy + 6], fill=(236, 236, 240, 255))
    for t in range(0, GW, 40 * S // 3):   # Markierungen, damit Rutschen auffällt
        dd.rectangle([t, gy, t + 2, gy + 6], fill=(180, 180, 190, 255))
    x = 10 + (left - ox + step * k) * S
    big = im.resize((im.width * S, im.height * S), Image.NEAREST)
    fr.alpha_composite(big, (x, gy - oy * S))
    gifs.append(fr.convert('P', palette=Image.ADAPTIVE, colors=128))
gifs[0].save(out_gif, save_all=True, append_images=gifs[1:], duration=83, loop=0, disposal=2)
print(out_png, out_gif, len(frames), 'Bilder')
