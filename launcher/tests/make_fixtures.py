#!/usr/bin/env python3
"""Testdaten für launcher/tests/test_core.cpp: ZIP-Archive mit allen Kompressionsarten.
Aufruf: python3 make_fixtures.py <Zielordner>"""
import os
import random
import sys
import zipfile
import zlib

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
rnd = random.Random(42)

files = {
    'Paket/README.md': ('# Überschrift\n\nÄpfel, Öl und Übermut – UTF-8 mit Umlauten.\n' * 40).encode('utf-8'),
    'Paket/zufall.bin': bytes(rnd.getrandbits(8) for _ in range(70000)),           # kaum komprimierbar
    'Paket/lang.txt': (b'abcdefghij' * 9000) + b'Ende',                             # weite Rückverweise
    'Paket/leer.txt': b'',
    'Paket/Unterordner/klein.txt': b'x',
    'Paket/mix.bin': b''.join(bytes([rnd.randrange(4)]) * rnd.randrange(1, 300) for _ in range(2000)),
}
for name, data in files.items():
    path = os.path.join(out, 'orig', name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)

for level in (1, 6, 9):
    with zipfile.ZipFile(os.path.join(out, f'deflate{level}.zip'), 'w', zipfile.ZIP_DEFLATED, compresslevel=level) as z:
        z.writestr(zipfile.ZipInfo('Paket/'), b'')
        for name, data in files.items():
            z.writestr(name, data)

with zipfile.ZipFile(os.path.join(out, 'stored.zip'), 'w', zipfile.ZIP_STORED) as z:
    for name, data in files.items():
        z.writestr(name, data)

# Rohes Deflate mit festen Huffman-Codes (Z_FIXED) und mit gespeicherten Blöcken (level 0)
for tag, strategy, level in (('fixed', zlib.Z_FIXED, 6), ('level0', zlib.Z_DEFAULT_STRATEGY, 0)):
    for name, data in files.items():
        c = zlib.compressobj(level, zlib.DEFLATED, -15, 9, strategy)
        raw = c.compress(data) + c.flush()
        base = name.replace('/', '_')
        with open(os.path.join(out, f'raw-{tag}-{base}.deflate'), 'wb') as f:
            f.write(raw)

# Archiv mit gefährlichem Namen (Zip Slip) und kaputtes Archiv
with zipfile.ZipFile(os.path.join(out, 'slip.zip'), 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('../boese.txt', b'nein')
data = open(os.path.join(out, 'deflate6.zip'), 'rb').read()
broken = bytearray(data)
with zipfile.ZipFile(os.path.join(out, 'deflate6.zip')) as z:
    info = z.getinfo('Paket/lang.txt')
start = info.header_offset + 30 + len(info.filename.encode()) + len(info.extra)
broken[start + info.compress_size // 2] ^= 0x55   # mitten in die komprimierten Daten
open(os.path.join(out, 'broken.zip'), 'wb').write(bytes(broken))
print('ok')
