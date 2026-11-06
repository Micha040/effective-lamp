# Erzeugt res/deskcats.ico (16..256 px) und res/logo.png aus einer 16x16-Pixelzeichnung (Fatos-Kopf).
# Aufruf: python3 tools/make_icon.py res
import sys, pathlib
from PIL import Image

PAL = {
    'o': (92, 44, 20), 'd': (196, 112, 48), 'm': (234, 152, 80), 'l': (248, 192, 126),
    'c': (250, 226, 190), 'k': (40, 24, 16), 'w': (255, 255, 255), 'e': (214, 178, 72),
    'n': (230, 140, 136), 's': (204, 120, 56),
}
ART = [
    "................",
    "..oo........oo..",
    ".odmo......omdo.",
    ".odmmoooooommdo.",
    ".ommmmlsllmmmmo.",
    "ommmmllsllmmmmmo",
    "ommmmmmmmmmmmmmo",
    "ommkkkmmmmkkkmmo",
    "omkwkkmmmmkwkkmo",
    "omkkekmmmmkkekmo",
    "ommkkmmnnmmkkmmo",
    "ommmmcccccccmmmo",
    ".ommccccccccmmo.",
    ".oommccccccmmoo.",
    "...oooooooooo...",
    "................",
]

def base():
    im = Image.new('RGBA', (16, 16), (0, 0, 0, 0))
    for y, row in enumerate(ART):
        assert len(row) == 16, (y, len(row))
        for x, ch in enumerate(row):
            if ch in PAL:
                im.putpixel((x, y), PAL[ch] + (255,))
    return im

def main():
    out = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'res')
    b = base()
    sizes = [16, 32, 48, 64, 128, 256]
    imgs = [b.resize((s, s), Image.NEAREST) for s in sizes]
    imgs[-1].save(out / 'deskcats.ico', sizes=[(s, s) for s in sizes], append_images=imgs[:-1])
    logo = Image.new('RGBA', (150, 150), (0, 0, 0, 0))
    logo.alpha_composite(b.resize((144, 144), Image.NEAREST), (3, 3))
    logo.save(out / 'logo.png')
    b.resize((256, 256), Image.NEAREST).save(out / 'icon-preview.png')
    print('ok')


if __name__ == '__main__':   # brand/make_brand.py importiert base() für die Kachel im Launcher
    main()
