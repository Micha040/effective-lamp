import sys
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from pxtool import sheet, PALETTES
from PIL import Image

def check(name, rows):
    w = len(rows[0])
    for i, r in enumerate(rows):
        if len(r) != w:
            raise SystemExit(f'{name}: Zeile {i} hat {len(r)} statt {w} Zeichen: {r!r}')
    return rows

F_HEAD_FRONT = check('ff', [
    "...oo...oo...",
    "..odmooomdo..",
    ".omlllllllmo.",
    "omldmdmdmlmmo",
    "ommmmmmmmmmmo",
    "omwkmmmmwkmmo",
    "omekmmmmekmmo",
    "ommmmmcncmmmo",
    "ommmcccccmmmo",
    ".ommccsccmmo.",
    "..oocccccoo..",
    "....ooooo....",
])
F_HEAD_34 = check('f34', [
    "...oo....oo..",
    "..odmoooomdo.",
    ".omllllllllmo",
    "omlldmdmdmlmo",
    "ommmmmmmmmmmo",
    "ommwkmmmmwkmo",
    "ommekmmmmekmo",
    "ommmmmmcncmmo",
    "ommmmccccccmo",
    ".ommmccsccmo.",
    "..ooocccccoo.",
    ".....ooooo...",
])
K_HEAD_FRONT = check('kf', [
    "..o.......o..",
    ".opo.....opo.",
    ".oppo...oppo.",
    "ommmmoooommmo",
    "omlllllllllmo",
    "ommmmmmmmmmmo",
    "omwemmmmmwemo",
    "omkemmmmmkemo",
    "ommmmmnmmmmmo",
    "ommmmcccmmmmo",
    ".omcccsccccmo",
    "..occcccccoo.",
    "...ooooooo...",
])
if __name__ == '__main__':
    out = sys.argv[1]
    a = sheet([('fatos vorne', F_HEAD_FRONT), ('fatos 3/4', F_HEAD_34)], PALETTES['fatos'], scale=14, cols=2, ground=False)
    b = sheet([('kosta vorne', K_HEAD_FRONT)], PALETTES['kosta'], scale=14, cols=1, ground=False)
    o = Image.new('RGB', (a.width + b.width, max(a.height, b.height)), (112, 150, 190))
    o.paste(a, (0, 0)); o.paste(b, (a.width, 0))
    o.save(out + '/play.png')
