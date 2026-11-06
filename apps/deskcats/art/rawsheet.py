# Setzt die Sprites aus out/px/*.raw zu einem Vorschau-Sheet zusammen (ganzzahlig vergrößert, Boden = Fensterkante).
import sys, pathlib
from PIL import Image, ImageDraw

def load(path):
    data = pathlib.Path(path).read_bytes()
    nl = data.index(b'\n')
    w, h, ox, oy = map(int, data[:nl].split())
    im = Image.frombytes('RGBA', (w, h), data[nl + 1:nl + 1 + w * h * 4])
    return im, ox, oy

def main():
    out = sys.argv[1]
    scale = int(sys.argv[2])
    names = sys.argv[3:]
    sprites = [(n, *load(f'out/px/{n}.raw')) for n in names]
    cols = min(4, len(sprites))
    cw = max(im.width for _, im, _, _ in sprites) * scale + 40
    above = max(oy for _, _, _, oy in sprites)
    below = max(im.height - oy for _, im, _, oy in sprites)
    ch = (above + below) * scale + 70
    rows = (len(sprites) + cols - 1) // cols
    sheet = Image.new('RGB', (cw * cols, ch * rows), (58, 74, 96))
    d = ImageDraw.Draw(sheet)
    for i, (n, im, ox, oy) in enumerate(sprites):
        cx, cy = (i % cols) * cw, (i // cols) * ch
        ground = cy + 30 + above * scale
        # Fensterkante: hellgraue Titelleiste unter der Katze
        d.rectangle([cx + 8, ground, cx + cw - 8, ground + 22], fill=(232, 232, 236))
        d.rectangle([cx + 8, ground + 22, cx + cw - 8, ch * (i // cols + 1) - 8], fill=(250, 250, 252))
        big = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
        x = cx + (cw - big.width) // 2
        sheet.paste(big, (x, ground - oy * scale), big)
        d.text((cx + 12, cy + 8), n, fill=(230, 230, 230))
    sheet.save(out)

main()
