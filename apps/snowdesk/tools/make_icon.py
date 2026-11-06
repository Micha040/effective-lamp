# Erzeugt res/snowdesk.ico (mehrere Größen) und res/logo.png: Schneekristall auf eisblauem, abgerundetem Quadrat.
import math, struct, io, sys
from PIL import Image

def seg_dist(px, py, ax, ay, bx, by):
    pax, pay, bax, bay = px-ax, py-ay, bx-ax, by-ay
    h = max(0.0, min(1.0, (pax*bax+pay*bay)/(bax*bax+bay*bay)))
    dx, dy = pax-bax*h, pay-bay*h
    return math.hypot(dx, dy)

def crystal_d(x, y, simple):
    r = math.hypot(x, y)
    ang = math.atan2(y, x) + math.pi/2   # ein Arm zeigt nach oben
    sector = math.pi/3
    a = math.fmod(ang + 4*math.pi, sector) - sector/2
    qx, qy = r*math.cos(a), abs(r*math.sin(a))
    c60, s60 = 0.5, 0.8660254
    if simple:   # kleine Größen: dicke Arme, ein Seitenast
        d = seg_dist(qx, qy, 0, 0, 0.92, 0) - 0.12
        d = min(d, seg_dist(qx, qy, 0.55, 0, 0.55+0.28*c60, 0.28*s60) - 0.09)
        return min(d, r - 0.2)
    d = seg_dist(qx, qy, 0, 0, 0.92, 0) - 0.075
    d = min(d, seg_dist(qx, qy, 0.42, 0, 0.42+0.30*c60, 0.30*s60) - 0.055)
    d = min(d, seg_dist(qx, qy, 0.66, 0, 0.66+0.18*c60, 0.18*s60) - 0.045)
    return min(d, r - 0.16)

def render(size):
    ss = 4 if size <= 64 else 2
    S = size*ss
    img = Image.new('RGBA', (S, S))
    px = img.load()
    rad = 0.22*S
    simple = size <= 24
    flake_r = 0.38*S if not simple else 0.40*S
    for j in range(S):
        for i in range(S):
            x, y = i+0.5, j+0.5
            # abgerundetes Quadrat
            cx = min(max(x, rad), S-rad); cy = min(max(y, rad), S-rad)
            dq = math.hypot(x-cx, y-cy) - rad
            inside = 1.0 if dq <= -0.5 else (0.0 if dq >= 0.5 else 0.5-dq)
            if inside <= 0:
                px[i, j] = (0, 0, 0, 0); continue
            t = y/S
            # Verlauf: oben helles Eisblau, unten tieferes Blau
            r0, g0, b0 = 0x5B, 0xB6, 0xF5
            r1, g1, b1 = 0x1E, 0x63, 0xC8
            R = r0+(r1-r0)*t; G = g0+(g1-g0)*t; B = b0+(b1-b0)*t
            # Kristall
            fx, fy = (x-S/2)/flake_r, (y-S/2)/flake_r
            d = crystal_d(fx, fy, simple)
            pix = 1.0/flake_r
            cov = 1.0 - min(1.0, max(0.0, (d + pix)/(2*pix)))
            R = R*(1-cov) + 255*cov; G = G*(1-cov) + 255*cov; B = B*(1-cov) + 255*cov
            px[i, j] = (int(R+0.5), int(G+0.5), int(B+0.5), int(255*inside+0.5))
    return img.resize((size, size), Image.LANCZOS)

def ico(images, path):
    # PNG-komprimierte Einträge (seit Vista überall unterstützt)
    entries, blobs = [], []
    for im in images:
        b = io.BytesIO(); im.save(b, 'PNG'); data = b.getvalue()
        blobs.append(data); entries.append(im.size[0])
    out = io.BytesIO()
    out.write(struct.pack('<HHH', 0, 1, len(images)))
    offset = 6 + 16*len(images)
    for size, data in zip(entries, blobs):
        w = 0 if size >= 256 else size
        out.write(struct.pack('<BBBBHHII', w, w, 0, 0, 1, 32, len(data), offset))
        offset += len(data)
    for data in blobs: out.write(data)
    open(path, 'wb').write(out.getvalue())

if __name__ == '__main__':
    res = sys.argv[1]
    sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
    imgs = [render(s) for s in sizes]
    ico(imgs, res + '/snowdesk.ico')
    imgs[-1].save(res + '/logo.png')
    # Vorschau-Blatt
    sheet = Image.new('RGBA', (sum(s+10 for s in sizes), 266), (40, 40, 40, 255))
    x = 5
    for s, im in zip(sizes, imgs):
        sheet.paste(im, (x, 5), im); x += s+10
    sheet.save(res + '/../out/icon-sheet.png')
    print('ok')
