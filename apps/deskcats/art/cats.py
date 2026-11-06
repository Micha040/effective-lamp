# Fatos und Kosta als Pixel Art (Blick nach rechts; links = gespiegelt)
import sys
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from pxtool import Canvas, sheet, show, PALETTES

def fatos_stand():
    c = Canvas(34, 24)
    # Kopf (rund, Faltohren als kleine Kappen)
    c.spans('h', {2: [(24, 25), (29, 30)], 3: (23, 31), 4: (22, 32), 5: (22, 32), 6: (22, 32), 7: (22, 32),
                  8: (22, 32), 9: (22, 32), 10: (23, 31), 11: (24, 30)})
    # Rumpf
    c.spans('#', {10: (9, 22), 11: (6, 24), 12: (5, 25), 13: (5, 25), 14: (5, 25), 15: (5, 24), 16: (5, 24),
                  17: (6, 23), 18: (7, 22)})
    # ferne Beine (dahinter)
    c.spans('f', {19: [(11, 13), (17, 19)], 20: [(11, 13), (17, 19)], 21: [(11, 13), (17, 19)], 22: [(11, 13), (17, 19)],
                  23: [(11, 14), (17, 20)]})
    # nahe Beine
    c.spans('#', {19: [(6, 9), (20, 22)], 20: [(6, 8), (20, 22)], 21: [(6, 8), (20, 22)], 22: [(6, 9), (20, 22)],
                  23: [(6, 10), (20, 23)]})
    # Schwanz nach oben gebogen
    c.spans('t', {2: (4, 5), 3: (2, 5), 4: (1, 3), 5: (1, 3), 6: (1, 3), 7: (1, 3), 8: (1, 3), 9: (2, 4), 10: (3, 5),
                  11: (4, 6)})
    # Gesicht: Augen, Nase, Schnauze
    c.row(6, 24, '.ooo..ooo')
    c.row(7, 24, 'oewo.oewo')
    c.row(8, 24, 'okeo.okeo')
    c.row(9, 24, '..cc.ncc.')
    c.row(10, 24, '.cccscccc')
    c.row(11, 25, 'csscs')
    # Ohren-Innenseite / Falte
    c.row(3, 24, 'dd...dd')
    # Brust creme
    c.row(12, 22, 'cccc')
    c.row(13, 22, 'cccc')
    c.row(14, 22, 'sccc')
    c.row(15, 22, 'scc')
    # Streifen auf dem Rücken
    for x in (9, 12, 15, 18):
        c.row(11, x, 'd')
        c.row(12, x, 'd')
        c.row(13, x, 'd')
    # Pfoten heller
    c.row(23, 7, 'ccc')
    c.row(23, 21, 'cc')
    return c.render()

if __name__ == '__main__':
    out = sys.argv[1]
    f = fatos_stand()
    show(f)
    sheet([('stehen', f)], PALETTES['fatos'], scale=10, cols=1).save(out + '/fatos_stand.png')
