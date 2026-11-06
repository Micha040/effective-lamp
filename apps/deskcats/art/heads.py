import sys
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from pxtool import sheet, PALETTES

# Kopf von vorne (Fatos: rund, Faltohren als Kappen)
F_FRONT_A = [
    "...oo...oo...",
    "..oddoooodd o..",
]
F_FRONT = [
    "..ooo...ooo..",
    ".odddo.oddo..",
    ".olmmooommlo.",
    "olmmdmdmdmmlo",
    "ommmmmmmmmmmo",
    "omwkmmmmmwkmo",
    "omekmmmmmekmo",
    "omeemcncmeemo",
    "ommcccncccmmo",
    ".omcckckccmo.",
    "..occcccccо..",
]
F_FRONT2 = [
    "...oo...oo...",
    "..oddo.oddo..",
    ".olmmooommmlo",
    "olmmdmdmdmmlo",
    "ommmmmmmmmmmo",
    "omwekmmmwekmo",
    "omeekmmmeekmo",
    "ommmmcncmmmmo",
    "omsmccnccmsmo",
    ".oscckckccso.",
    "..ossccccso..",
    "...ooooooo...",
]
K_FRONT = [
    "..o.......o..",
    ".opo.....opo.",
    ".oppo...oppo.",
    ".ommmoooommmo",
    "ommmmmmmmmmmo",
    "omweemmmweemo",
    "omekkmmmekkmo",
    "ommmmmnmmmmmo",
    "ommmmcccmmmmo",
    ".omcckckccmo.",
    "..occcccccо..",
    "...ooooooo...",
]
if __name__ == '__main__':
    out = sys.argv[1]
    clean = lambda rows: [r.replace(' ', '').replace('о', 'o') for r in rows]
    frames = [('fatos A', clean(F_FRONT2))]
    im = sheet(frames, PALETTES['fatos'], scale=14, cols=1, ground=False)
    im2 = sheet([('kosta', clean(K_FRONT))], PALETTES['kosta'], scale=14, cols=1, ground=False)
    from PIL import Image
    W = im.width + im2.width
    o = Image.new('RGB', (W, max(im.height, im2.height)), (112, 150, 190))
    o.paste(im, (0, 0)); o.paste(im2, (im.width, 0))
    o.save(out + '/heads.png')
