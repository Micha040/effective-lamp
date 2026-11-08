#!/usr/bin/env python3
"""fragdesk-server – Ende-zu-Ende-Test über HTTP mit dem fertigen Programm.

Startet den Server auf einem freien Port (7451–7459, oder FD_TEST_PORT) und spielt den ganzen Ablauf durch:
Einladung über die Befehlszeile, Registrieren, Matches und Videos hochladen (in Stücken, fortsetzen, 409),
Kontingent (507), Handy koppeln, Ansichten nur mit den Daten des eigenen Kontos, zwei Konten getrennt,
Admin (Einladen, Limit, Entfernen), Grenzen gegen Raten, Abmelden, Neustart mit allen Daten.

  python3 tests/server_e2e.py --bin build/fragdesk-server-linux-x64 --data <Testdaten> --tmp <leerer Ordner>
  python3 tests/server_e2e.py --make-data <Ordner>   künstliche Testdaten (matches/, clips.json, videos/) erzeugen

Testdaten wie bei tests/mobile_dev.cpp: matches/*.json (+ .heat), clips.json mit path/title/match/highlight/map/
created/seconds, daneben je ein .jpg zum .mp4, settings.json mit der eigenen SteamID.
"""
import argparse
import hashlib
import http.client
import json
import os
import pathlib
import random
import re
import shutil
import signal
import socket
import struct
import subprocess
import sys
import threading
import time

ME = '76561190000000001'
FRIEND = '76561190000000002'
MB = 1024 * 1024
checks = 0
fails = 0


def check(cond, what):
    global checks, fails
    checks += 1
    if not cond:
        fails += 1
        print('FEHLER:', what, flush=True)
    return cond


# ---- Künstliche Testdaten -------------------------------------------------------------------------------------------
def synth_match(i, rnd):
    checksum = f'synth{i:03d}'
    maps = ['de_mirage', 'de_inferno', 'de_ancient', 'de_nuke']
    date = f'2026-10-{1 + i % 28:02d}T{18 + i % 5:02d}:15:00Z'
    ids = [ME, FRIEND] + [f'7656119{9000000000 + k:010d}' for k in range(8)]
    rounds_n = 22
    score_a = 13 if i % 3 else 9
    score_b = rounds_n - score_a
    winner = 'A' if score_a > score_b else 'B'
    rounds = []
    for n in range(1, rounds_n + 1):
        w = 'A' if (n % 2 == 0) == (winner == 'A') or n > rounds_n - 4 and winner == 'A' else 'B'
        rounds.append({'n': n, 'start': n * 3000, 'freezeEnd': n * 3000 + 1000, 'end': n * 3000 + 2800, 'endOfficial': n * 3000 + 2900,
                       'winner': w, 'winnerSide': 'CT' if (w == 'A') == (n <= 12) else 'T', 'reason': 'elimination',
                       'scoreA': 0, 'scoreB': 0, 'sideA': 'CT' if n <= 12 else 'T', 'econA': 'full', 'econB': 'full',
                       'equipA': 20000, 'equipB': 21000, 'spentA': 15000, 'spentB': 16000, 'moneyA': 30000, 'moneyB': 29000,
                       'durationMs': 60000, 'bomb': '', 'site': '', 'firstKill': 0})
    kills = []
    for n in range(1, rounds_n + 1):
        for k in range(rnd.randint(4, 8)):
            killer, victim = rnd.sample(ids, 2)
            kills.append({'t': n * 3000 + 1100 + k * 150, 'r': n, 'k': killer, 'v': victim, 'ks': 'T', 'vs': 'CT', 'w': 'AK-47',
                          'wt': 'rifle', 'hs': rnd.random() < 0.4, 'dist': 12.5, 'kx': 1.0, 'ky': 2.0, 'vx': 3.0, 'vy': 4.0})
    players = []
    for slot, sid in enumerate(ids):
        k = sum(1 for x in kills if x['k'] == sid)
        d = sum(1 for x in kills if x['v'] == sid)
        hs = sum(1 for x in kills if x['k'] == sid and x['hs'])
        team = 'A' if slot % 2 == 0 else 'B'
        stats = {'kills': k, 'deaths': d, 'assists': rnd.randint(1, 6), 'hsKills': hs, 'dmg': k * 95 + 40, 'rounds': rounds_n,
                 'kastRounds': rounds_n - d // 2, 'shots': k * 9 + 20, 'shotsHit': k * 3 + 5, 'hits': k * 3 + 5, 'k1': k // 2,
                 'k2': k // 6, 'mvps': k // 7, 'score': k * 2, 'firstKillRounds': k // 5, 'aliveSec': 900.5}
        players.append({
            'steamId': sid, 'name': 'Testspieler' if sid == ME else 'Freund' if sid == FRIEND else f'Spieler {slot}', 'team': team,
            'slot': slot, 'result': 'win' if team == winner else 'loss', 'rankType': 11, 'rankOld': 12000 + i * 40,
            'rankNew': 12040 + i * 40, 'wins': 20 + i, 'mvps': k // 7, 'score': k * 2, 'rating': round(0.6 + k / 25, 2),
            'rating1': round(0.6 + k / 25, 2), 'kast': 70.5, 'adr': round((k * 95 + 40) / rounds_n, 2),
            'stats': stats, 'statsT': {key: v // 2 for key, v in stats.items() if isinstance(v, int)},
            'statsCT': {key: v - v // 2 for key, v in stats.items() if isinstance(v, int)},
            'weapons': [{'name': 'AK-47', 'type': 'rifle', 'stats': {'kills': k, 'shots': k * 9 + 20, 'hits': k * 3 + 5, 'dmg': k * 95}}],
            'deathsBy': {'AK-47': d},
            'rounds': [{'n': n, 'side': 'CT' if (n <= 12) == (team == 'A') else 'T', 'k': sum(1 for x in kills if x['k'] == sid and x['r'] == n),
                        'dmg': 50, 'kast': True, 'equip': 4700, 'spent': 2700, 'money': 5000, 'econ': 'full', 'alive': 40.5}
                       for n in range(1, rounds_n + 1)],
            'opponents': [{'steamId': o, 'k': 1, 'd': 1, 'dmgDealt': 100, 'dmgTaken': 90, 'openK': 0, 'openD': 0} for o in ids if o != sid][:5],
        })
    highlights = [{'id': f'{ME}-r7', 'p': ME, 'side': 'T', 'r': 7, 'score': 120 + i, 'kills': 4, 'tags': ['4k'], 'weapons': ['AK-47'],
                   'segments': [[21000, 21500]], 'killTicks': [21100, 21200, 21300, 21400]}]
    return {'schema': 2, 'analyzer': 'test', 'csda': 'test', 'checksum': checksum,
            'demo': {'path': f'C:\\Test\\{checksum}.dem', 'file': f'{checksum}.dem', 'size': 1000, 'modified': date},
            'match': {'source': 'valve', 'mode': 'premier', 'map': maps[i % len(maps)], 'date': date, 'duration': 2400, 'tickrate': 64,
                      'tickCount': 70000, 'serverName': 'Test', 'maxRounds': 24, 'overtimes': 0, 'ranked': True, 'buildNumber': 1,
                      'scoreA': score_a, 'scoreB': score_b, 'winner': winner},
            'teams': [{'letter': 'A', 'name': 'Team A', 'score': score_a, 'startSide': 'CT'}, {'letter': 'B', 'name': 'Team B', 'score': score_b, 'startSide': 'T'}],
            'rounds': rounds, 'players': players, 'kills': kills, 'clutches': [], 'highlights': highlights}


# Kartenwerte wie src/core/radar_maps.inc: linke obere Ecke, Breite, Grenze zur unteren Ebene (None = nur eine Ebene)
RADARS = {'de_mirage': (-3230, 1713, 5120, None), 'de_inferno': (-2087, 3870, 5017.6, None), 'de_ancient': (-2953, 2164, 5120, None),
          'de_nuke': (-3453, 2887, 7168, -495)}


def synth_heat(m, rnd):
    """Gültige Heatmap-Datei (FDHM, siehe src/core/heat.h) zu einem künstlichen Match: Kills aus dem Match, je Runde vier
    Granaten und jede Sekunde alle Spieler – zufällig auf dem Radarbild verteilt, auf Karten mit zwei Ebenen die Hälfte
    unten."""
    def s(text):
        b = text.encode('utf-8')
        return struct.pack('<H', len(b)) + b
    pos_x, pos_y, span, lower_z = RADARS[m['match']['map']]
    players = m['players']
    index = {p['steamId']: i for i, p in enumerate(players)}

    def where(slot):
        x = pos_x + rnd.uniform(0.25, 0.75) * span
        y = pos_y - rnd.uniform(0.25, 0.75) * span
        z = 0 if lower_z is None else lower_z + (-250 if slot % 2 else 250)
        return struct.pack('<hhh', int(x), int(y), int(z))

    def ct(slot, r):   # Team A beginnt als CT
        return (players[slot]['team'] == 'A') == (r['sideA'] == 'CT')
    rounds = {r['n']: r for r in m['rounds']}
    out = bytearray(b'FDHM') + struct.pack('<HHf', 1, 0, 64.0) + s(m['match']['map'])
    out += struct.pack('<H', len(players)) + b''.join(struct.pack('<Q', int(p['steamId'])) + s(p['name']) for p in players)
    out += struct.pack('<H', 2) + s('') + s('AK-47')
    out += struct.pack('<I', len(m['kills']))
    for k in m['kills']:
        r, ki, vi = rounds[k['r']], index[k['k']], index[k['v']]
        flags = (1 if ct(ki, r) else 0) | (2 if ct(vi, r) else 0) | (4 if k['hs'] else 0)
        out += struct.pack('<iBBBB', k['t'], ki, vi, flags, 1) + where(ki) + where(vi)
    out += struct.pack('<I', 4 * len(m['rounds']))
    for r in m['rounds']:
        for g in range(4):
            thrower = (r['n'] + g) % len(players)
            out += struct.pack('<iiBBB', r['freezeEnd'] + 200 + g * 100, r['freezeEnd'] + 280 + g * 100, (1, 2, 4, 3)[g], thrower,
                               1 if ct(thrower, r) else 0) + where(thrower) + where(thrower)
    frames = bytearray()
    count = 0
    for r in m['rounds']:
        for tick in range(r['freezeEnd'], r['end'], 64):
            frames += struct.pack('<iB', tick, len(players))
            for slot in range(len(players)):
                frames += struct.pack('<BB', slot, 1 if ct(slot, r) else 0) + where(slot)
            count += 1
    out += struct.pack('<II', count, len(frames)) + frames + b'END!'
    return bytes(out)


def make_data(out):
    out = pathlib.Path(out)
    rnd = random.Random(7)
    (out / 'matches').mkdir(parents=True, exist_ok=True)
    (out / 'videos').mkdir(parents=True, exist_ok=True)
    for i in range(12):
        m = synth_match(i, rnd)
        (out / 'matches' / f"{m['checksum']}.json").write_text(json.dumps(m), encoding='utf-8')
        if i in (0, 3, 4):   # Heatmaps: zweimal Mirage, einmal Nuke (zwei Ebenen)
            (out / 'matches' / f"{m['checksum']}.heat").write_bytes(synth_heat(m, rnd))
    clips = []
    for i, size in enumerate((1_300_000, 2_100_000, 2_900_000)):
        mp4 = out / 'videos' / f'Clip {i}.mp4'
        body = b'\x00\x00\x00\x20ftypisom' + bytes(rnd.randrange(256) for _ in range(4096))
        mp4.write_bytes((body * (size // len(body) + 1))[:size])
        (out / 'videos' / f'Clip {i}.jpg').write_bytes(b'\xff\xd8\xff\xe0' + bytes(rnd.randrange(256) for _ in range(3000)) + b'\xff\xd9')
        clips.append({'path': str(mp4), 'title': 'R7 4 Kills', 'match': f'synth{i:03d}', 'highlight': f'{ME}-r7', 'map': 'de_mirage',
                      'created': 1794053832 - i * 86400, 'seconds': 6 + i})
    (out / 'clips.json').write_text(json.dumps(clips, indent=1), encoding='utf-8')
    (out / 'settings.json').write_text(json.dumps({'steamId': ME}), encoding='utf-8')
    print('Testdaten geschrieben:', out)


# ---- HTTP -------------------------------------------------------------------------------------------------------------
class Resp:
    def __init__(self, status, body, headers):
        self.status, self.body, self.headers = status, body, headers

    def json(self):
        try:
            return json.loads(self.body.decode('utf-8'))
        except ValueError:
            return None

    def header(self, name):
        for k, v in self.headers:
            if k.lower() == name.lower():
                return v
        return None


class Client:
    def __init__(self, port, token=None, cookie=None, ip=None):
        self.port, self.token, self.cookie, self.ip = port, token, cookie, ip

    def req(self, method, path, body=None, headers=None, fd=True):
        h = {}
        if method not in ('GET', 'HEAD') and fd:
            h['X-FragDesk'] = '1'
        if self.token:
            h['Authorization'] = 'Bearer ' + self.token
        if self.cookie:
            h['Cookie'] = 'fdk=' + self.cookie
        if self.ip:
            h['X-Forwarded-For'] = self.ip
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode('utf-8')
            h['Content-Type'] = 'application/json'
        h.update(headers or {})
        conn = http.client.HTTPConnection('127.0.0.1', self.port, timeout=30)
        try:
            conn.request(method, path, body=body, headers=h)
            r = conn.getresponse()
            return Resp(r.status, r.read(), r.getheaders())
        except (ConnectionError, OSError):
            return Resp(0, b'', [])   # Server hat die Verbindung geschlossen (z. B. 413 vor dem Lesen des Körpers)
        finally:
            conn.close()

    def get(self, path, **kw):
        return self.req('GET', path, **kw)


def free_port():
    env = os.environ.get('FD_TEST_PORT')
    for p in ([int(env)] if env else range(7451, 7460)):
        s = socket.socket()
        try:
            s.bind(('127.0.0.1', p))
            return p
        except OSError:
            pass
        finally:
            s.close()
    sys.exit('FEHLER: kein freier Port zwischen 7451 und 7459')


class Server:
    def __init__(self, binary, data, port, log):
        self.binary, self.data, self.port, self.log, self.proc = binary, data, port, log, None

    def start(self):
        logf = open(self.log, 'ab')
        self.proc = subprocess.Popen([self.binary, 'serve', '--data', self.data, '--listen', f'127.0.0.1:{self.port}',
                                      '--url', f'http://127.0.0.1:{self.port}/', '--min-free', '1'], stdout=logf, stderr=logf)
        for _ in range(100):
            if Client(self.port).get('/api/v1/health').status == 200:
                return
            if self.proc.poll() is not None:
                break
            time.sleep(0.05)
        sys.exit('FEHLER: Server startet nicht – siehe ' + str(self.log))

    def stop(self):
        if not self.proc or self.proc.poll() is not None:
            return self.proc.returncode if self.proc else 0
        self.proc.send_signal(signal.SIGTERM)
        try:
            return self.proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            return -1

    def cli(self, *args):
        r = subprocess.run([self.binary, *args, '--data', self.data], capture_output=True, text=True, timeout=30)
        return r.returncode, r.stdout, r.stderr


def fnv1a64(text):   # wie clipId() in FragDesk
    h = 1469598103934665603
    for b in text.encode('utf-8'):
        h = ((h ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f'{h:016x}'


def load_data(data):
    data = pathlib.Path(data)
    matches = {}
    for p in sorted((data / 'matches').glob('*.json')):
        m = json.loads(p.read_text(encoding='utf-8'))
        matches[m['checksum']] = {'json': p.read_bytes(), 'heat': p.with_suffix('.heat').read_bytes() if p.with_suffix('.heat').exists() else None}
    clips = []
    for c in json.loads((data / 'clips.json').read_text(encoding='utf-8')):
        mp4 = pathlib.Path(c['path'])
        if not mp4.exists():
            mp4 = data / 'videos' / mp4.name   # Testdaten an anderem Ort
        c = dict(c, id=fnv1a64(c['path']), mp4=mp4.read_bytes(), jpg=mp4.with_suffix('.jpg').read_bytes())
        clips.append(c)
    return matches, clips


def upload_clip(c, clip, chunk=MB):
    """Video in Stücken hochladen wie FragDesk (Stand vom Server holen, ab dort weiter)."""
    total = len(clip['mp4'])
    state = c.get('/api/v1/sync/state').json()
    offset = state['clips'].get(clip['id'], {}).get('size', 0)
    while offset < total:
        piece = clip['mp4'][offset:offset + chunk]
        r = c.req('PUT', f"/api/v1/sync/clip/{clip['id']}/data?offset={offset}&total={total}", body=piece)
        if r.status == 409:
            offset = r.json()['size']
            continue
        if r.status != 200:
            return r
        offset = r.json()['size']
    meta = {k: clip[k] for k in ('title', 'match', 'highlight', 'map', 'created', 'seconds')}
    return c.req('POST', f"/api/v1/sync/clip/{clip['id']}/done", body=dict(meta, total=total))


# ---- Ablauf -------------------------------------------------------------------------------------------------------------
def run(args):
    tmp = pathlib.Path(args.tmp)
    if tmp.exists():
        shutil.rmtree(tmp)
    tmp.mkdir(parents=True)
    data_dir = str(tmp / 'data')
    port = free_port()
    srv = Server(os.path.abspath(args.bin), data_dir, port, tmp / 'server.log')
    matches, clips = load_data(args.data)
    check(len(matches) >= 6 and len(clips) >= 2, 'Testdaten: mindestens 6 Matches und 2 Videos')
    srv.start()
    anon = Client(port)
    try:
        flow(srv, anon, port, matches, clips, data_dir)
    finally:
        code = srv.stop()
    check(code == 0, f'Server beendet sich sauber (Code {code})')
    log = (tmp / 'server.log').read_text(encoding='utf-8', errors='replace')
    check('fragdesk-server beendet' in log, 'Protokoll: sauber beendet')
    print(f'server_e2e: {checks} Prüfungen, {fails} Fehler (Port {port}, {len(matches)} Matches, {len(clips)} Videos)')
    return 1 if fails else 0


def flow(srv, anon, port, matches, clips, data_dir):
    r = anon.get('/api/v1/health')
    check(r.status == 200 and r.json()['ok'] is True and r.json()['version'], 'health ohne Anmeldung')
    r = anon.get('/')
    check(r.status == 200 and b'<html' in r.body and r.header('Content-Security-Policy'), 'Handy-App ausgeliefert')
    for asset in ('js/app.js', 'js/pages/admin.js', 'js/pages/invite.js', 'sw.js'):
        check(anon.get('/' + asset).status == 200, f'App-Datei {asset}')
    check(anon.get('/api/v1/info').status == 401, 'Ansichten nur gekoppelt')

    # Erste Einladung (Admin) über die Befehlszeile
    rc, out, err = srv.cli('invite', '--admin')
    link = out.strip().splitlines()[-1] if out.strip() else ''
    check(rc == 0 and link.startswith(f'http://127.0.0.1:{port}/#invite='), f'invite --admin gibt den Link aus ({out!r} {err!r})')
    code_a = link.split('#invite=')[-1]
    reg = {'invite': code_a, 'name': 'Micha', 'steamId': ME, 'client': 'FragDesk 1.4.0'}
    check(anon.req('POST', '/api/v1/account/register', body=reg, fd=False).status == 403, 'register ohne X-FragDesk: 403')
    r = anon.req('POST', '/api/v1/account/register', body=reg)
    ja = r.json() or {}
    check(r.status == 200 and len(ja.get('token', '')) == 64 and ja['user']['admin'] is True and ja['user']['name'] == 'Micha', 'register (Admin)')
    check(ja.get('server', {}).get('url') == f'http://127.0.0.1:{port}/' and ja['server'].get('version'), 'register: server.url/version')
    check(anon.req('POST', '/api/v1/account/register', body=reg).status == 403, 'Einladung gilt nur einmal')
    A = Client(port, token=ja.get('token'))

    # Admin lädt einen Freund ein (über FragDesk, also mit Konto-Token)
    r = A.req('POST', '/api/v1/admin/invite', body={'admin': False, 'days': 7})
    inv = r.json() or {}
    check(r.status == 200 and '#invite=' in inv.get('link', '') and inv.get('code') and inv.get('expires', 0) > time.time() + 6 * 86400, 'admin/invite')
    r = A.get('/api/v1/admin/invites')
    check(r.status == 200 and len(r.json()) == 1 and r.json()[0]['id'] == inv['id'] and 'code' not in r.json()[0], 'admin/invites ohne Code')
    r = anon.req('POST', '/api/v1/account/register', body={'invite': inv['link'], 'name': 'Freund', 'steamId': FRIEND, 'client': 'FragDesk 1.4.0'})
    jb = r.json() or {}
    check(r.status == 200 and jb['user']['admin'] is False, 'register (Freund, ganzer Link als invite)')
    B = Client(port, token=jb.get('token'))
    r = B.get('/api/v1/account')
    check(r.status == 200 and r.json()['quota']['limit'] == 15 * 1024 ** 3 and r.json()['quota']['used'] == 0, 'Standardlimit 15 GB')
    check(A.get('/api/v1/account').json()['quota']['limit'] == 0, 'Admin: unbegrenzt')
    check(B.req('POST', '/api/v1/admin/invite', body={}).status == 403, 'admin nur für Admins')

    # Grenze gegen Raten: Einladung 5 Fehlversuche pro IP, dann 429 (auch mit gültigem Code)
    code_c = A.req('POST', '/api/v1/admin/invite', body={'days': 1}).json()['code']
    bad = Client(port, ip='198.51.100.7')
    for i in range(5):
        check(bad.req('POST', '/api/v1/account/register', body={'invite': '0' * 32, 'name': 'x'}).status == 403, f'falsche Einladung {i + 1}')
    check(bad.req('POST', '/api/v1/account/register', body={'invite': code_c, 'name': 'x'}).status == 429, 'nach 5 Fehlversuchen: 429')
    r = Client(port, ip='198.51.100.8').req('POST', '/api/v1/account/register', body={'invite': code_c, 'name': 'Dritter', 'steamId': '1'})
    check(r.status == 200, 'andere IP darf weiter')
    C = Client(port, token=(r.json() or {}).get('token'))
    c_id = (r.json() or {}).get('user', {}).get('id')

    # Matches hochladen: A alle, B drei
    names = sorted(matches)
    for c in names:
        r = A.req('PUT', f'/api/v1/sync/match/{c}/json', body=matches[c]['json'])
        check(r.status == 200 and r.json()['size'] == len(matches[c]['json']), f'Match {c} hochladen')
        if matches[c]['heat']:
            check(A.req('PUT', f'/api/v1/sync/match/{c}/heat', body=matches[c]['heat']).status == 200, f'Heatmap {c}')
    b_names = names[-3:]
    for c in b_names:
        check(B.req('PUT', f'/api/v1/sync/match/{c}/json', body=matches[c]['json']).status == 200, f'Freund: Match {c}')
    first = names[0]
    check(A.req('PUT', f'/api/v1/sync/match/{names[1]}/json', body=matches[first]['json']).status == 400, 'Prüfsumme muss passen')
    check(A.req('PUT', '/api/v1/sync/match/kaputt/json', body=b'{"schema": 99}').status == 400, 'kaputtes Match: 400')
    check(A.req('PUT', f'/api/v1/sync/match/{first}/heat', body=b'GIF89a...').status == 400, 'falsche Heatmap: 400')
    check(A.req('PUT', '/api/v1/sync/match/..%2Fx/json', body=matches[first]['json']).status in (400, 404), 'Pfad mit ..: abgelehnt')
    r = A.req('PUT', f'/api/v1/sync/match/{first}/json', body=b'x' * (4 * MB + 1))
    check(r.status in (0, 413), f'Match über 4 MB: abgelehnt ({r.status})')
    check(Client(port).req('PUT', f'/api/v1/sync/match/{first}/json', body=matches[first]['json']).status in (0, 401, 413), 'ohne Token: abgelehnt')
    check(A.req('PUT', f'/api/v1/sync/match/{first}/json', body=matches[first]['json'], fd=False).status == 403, 'ohne X-FragDesk: 403')
    st = A.get('/api/v1/sync/state').json()
    heat_ok = all(st['matches'][c]['heat'] == len(matches[c]['heat']) for c in names if matches[c]['heat'])
    check(len(st['matches']) == len(names) and heat_ok and st['matches'][first]['json'] == len(matches[first]['json']), 'sync/state: Matches')
    used = sum(len(m['json']) + len(m['heat'] or b'') for m in matches.values())
    check(st['quota']['used'] == used and st['quota']['limit'] == 0, f'sync/state: belegt {st["quota"]["used"]} = {used}')

    # Videos in Stücken: Fortsetzen, 409, fertig, Vorschaubild
    v0 = clips[0]
    total = len(v0['mp4'])
    url = f"/api/v1/sync/clip/{v0['id']}"
    check(A.req('PUT', f'{url}/data?offset=0&total={total}', body=v0['mp4'][:MB]).json() == {'size': MB}, 'erstes Stück')
    r = A.req('PUT', f'{url}/data?offset=0&total={total}', body=v0['mp4'][:MB])
    check(r.status == 409 and r.json()['size'] == MB and r.json()['error'], '409 mit aktueller Größe (Stück doppelt)')
    check(A.req('PUT', f'{url}/data?offset=5&total={total}', body=b'abc').status == 409, '409 bei falschem offset')
    check(A.req('PUT', f'{url}/data?offset={MB}', body=b'abc').status == 400, 'total fehlt: 400')
    check(A.req('PUT', '/api/v1/sync/clip/NICHTHEX12345678/data?offset=0&total=3', body=b'abc').status == 400, 'clipId geprüft')
    check(A.get('/api/v1/sync/state').json()['clips'][v0['id']] == {'size': MB, 'complete': False, 'thumb': False}, 'sync/state: halbes Video')
    meta = {k: v0[k] for k in ('title', 'match', 'highlight', 'map', 'created', 'seconds')}
    r = A.req('POST', f'{url}/done', body=dict(meta, total=total))
    check(r.status == 409 and r.json()['size'] == MB, 'done vor dem Ende: 409')
    r = upload_clip(A, v0)   # „nach einem Abbruch“: Stand holen, ab dort weiter
    check(r.status == 200, f'Video fortsetzen und fertig ({r.status} {r.body[:200]!r})')
    check(A.req('PUT', f'{url}/thumb', body=v0['jpg']).status == 200, 'Vorschaubild')
    check(A.req('PUT', f'{url}/thumb', body=b'nojpeg').status == 400, 'Vorschaubild muss JPEG sein')
    check(A.req('POST', f'{url}/done', body=dict(meta, total=total)).status == 200, 'done nochmal: geht (Antwort verloren)')
    r = A.req('PUT', f'{url}/data?offset=0&total={total}', body=b'x')
    check(r.status == 409 and r.json()['size'] == total, 'fertiges Video: 409')
    for v in clips[1:]:
        check(upload_clip(A, v, chunk=700 * 1024).status == 200, f"Video {v['id']}")
        check(A.req('PUT', f"/api/v1/sync/clip/{v['id']}/thumb", body=v['jpg']).status == 200, 'Vorschaubild')
    st = A.get('/api/v1/sync/state').json()
    check(all(st['clips'][v['id']] == {'size': len(v['mp4']), 'complete': True, 'thumb': True} for v in clips), 'sync/state: alle Videos fertig')
    used += sum(len(v['mp4']) + len(v['jpg']) for v in clips)
    check(st['quota']['used'] == used, 'belegt mit Videos')

    # Handy koppeln (Konto A)
    r = A.req('POST', '/api/v1/sync/pairing')
    pa = r.json() or {}
    check(r.status == 200 and re.fullmatch(r'\d{6}', pa.get('code', '')) and pa['url'] == f'http://127.0.0.1:{port}/' and pa['until'] > time.time() + 500,
          'sync/pairing')
    check(anon.req('POST', '/api/v1/pair', body={'code': pa['code']}, fd=False).status == 403, 'pair ohne X-FragDesk')
    r = anon.req('POST', '/api/v1/pair', body={'code': pa['code']}, headers={'User-Agent': 'Mozilla/5.0 (iPhone)'})
    cookie = r.header('Set-Cookie') or ''
    check(r.status == 200 and r.json()['kind'] == 'server' and 'HttpOnly' in cookie and 'SameSite=Strict' in cookie, 'pair: Cookie')
    PA = Client(port, cookie=cookie.split(';')[0].split('=', 1)[-1])
    check(PA.get('/api/v1/session').json().get('kind') == 'server', 'session: kind server')
    info = PA.get('/api/v1/info').json() or {}
    check(info.get('matches') == len(names) and info['me']['id'] == ME and info['me']['name'] == 'Micha' and info['kind'] == 'server', 'info: eigene Daten')
    r = PA.get('/api/v1/matches')
    ids = [m['id'] for m in r.json()['items']]
    check(r.status == 200 and sorted(ids) == names, 'matches: alle eigenen')
    check(PA.get('/api/v1/overview').status == 200, 'overview')
    for tab in range(8):
        check(PA.get(f'/api/v1/match/{first}?tab={tab}').status == 200, f'match Tab {tab}')
    for tab in range(11):
        check(PA.get(f'/api/v1/stats?tab={tab}').status == 200, f'stats Tab {tab}')
    heat_checks(PA, anon, matches, names)
    mo = PA.get('/api/v1/moments').json()
    vids = {v['id'] for v in mo['videos']['items']}
    check(vids == {v['id'] for v in clips} and mo['blocked'] == '', 'moments: eigene Videos, nie blockiert')
    r = PA.get(f"/media/thumb/{v0['id']}.jpg")
    check(r.status == 200 and r.body == v0['jpg'] and r.header('Content-Type') == 'image/jpeg', 'Vorschaubild ausliefern')
    r = PA.get(f"/media/clip/{v0['id']}.mp4", headers={'Range': 'bytes=100-199'})
    check(r.status == 206 and r.body == v0['mp4'][100:200], 'Video mit Range')
    r = PA.get(f"/media/clip/{v0['id']}.mp4")
    check(r.status == 200 and hashlib.sha256(r.body).digest() == hashlib.sha256(v0['mp4']).digest(), 'Video ganz')
    me = PA.get('/api/v1/me').json() or {}
    check(me.get('user', {}).get('admin') is True and me['quota']['used'] == used and me['counts']['matches'] == len(names), '/me')
    check(Client(port, token=A.token).get('/api/v1/info').status == 401, 'Konto-Token gilt nicht für die Handy-App')
    check(Client(port, token=PA.cookie).get('/api/v1/sync/state').status == 401, 'Geräte-Token gilt nicht für sync')
    check(PA.get('/api/v1/account').status == 401, 'Geräte-Token gilt nicht für account')

    # Zweites Konto: nur eigene Daten
    pb = B.req('POST', '/api/v1/sync/pairing').json()
    check(pb['code'] != pa['code'], 'Kopplungscodes verschiedener Konten verschieden')
    r = anon.req('POST', '/api/v1/pair', body={'code': pb['code']})
    PB = Client(port, cookie=(r.header('Set-Cookie') or '').split(';')[0].split('=', 1)[-1])
    info = PB.get('/api/v1/info').json() or {}
    check(info.get('matches') == 3 and info['me']['id'] == FRIEND, 'Freund: 3 Matches')
    check(sorted(m['id'] for m in PB.get('/api/v1/matches').json()['items']) == sorted(b_names), 'Freund: nur seine Matches')
    check(PB.get(f'/api/v1/match/{first}?tab=0').status == 404, 'Freund: fremdes Match 404')
    check(PB.get(f'/api/v1/heat?match={first}').status == 404, 'Freund: fremde Heatmap 404')
    check(PB.get(f"/media/clip/{v0['id']}.mp4").status == 404 and PB.get(f"/media/thumb/{v0['id']}.jpg").status == 404, 'Freund: fremdes Video 404')
    check(PB.get('/api/v1/moments').json()['videos']['items'] == [], 'Freund: keine Videos')
    check(PA.get('/api/v1/info').json()['matches'] == len(names), 'A unverändert')
    # gleichzeitig: beide Handys und ein Upload
    errors = []

    def hammer(client, path, want):
        for _ in range(15):
            if client.get(path).status != want:
                errors.append(path)

    threads = [threading.Thread(target=hammer, args=(PA, '/api/v1/stats?tab=0', 200)), threading.Thread(target=hammer, args=(PB, '/api/v1/overview', 200)),
               threading.Thread(target=hammer, args=(PA, '/api/v1/matches', 200)),
               threading.Thread(target=hammer, args=(PA, '/api/v1/heat?mode=0&period=2&layers=127&marks=1', 200)),
               threading.Thread(target=hammer, args=(PA, f'/api/v1/heat?match={first}&layers=3', 200))]
    for t in threads:
        t.start()
    check(B.req('PUT', f'/api/v1/sync/match/{names[0]}/json', body=matches[names[0]]['json']).status == 200, 'Upload während Ansichten')
    for t in threads:
        t.join()
    check(not errors, f'gleichzeitige Anfragen ({len(errors)} Fehler)')
    check(PB.get('/api/v1/info').json()['matches'] == 4, 'neues Match sofort in der Handy-App')

    # Kontingent: Limit knapp über dem Belegten – das nächste Video passt nicht (507 schon beim ersten Stück)
    b_used = B.get('/api/v1/account').json()['quota']['used']
    r = A.req('PUT', f"/api/v1/admin/users/{jb['user']['id']}", body={'limit': b_used + 32 * 1024})
    check(r.status == 200 and r.json()['limit'] == b_used + 32 * 1024, 'admin: Limit setzen')
    v1 = clips[1]
    r = B.req('PUT', f"/api/v1/sync/clip/{v1['id']}/data?offset=0&total={len(v1['mp4'])}", body=v1['mp4'][:16 * 1024])
    check(r.status == 507 and r.json()['error'].startswith('Dein Speicher auf dem Server ist voll ('), f'507: {r.body[:120]!r}')
    check(B.get('/api/v1/sync/state').json()['clips'] == {}, '507: nichts geschrieben')
    big = matches[names[1]]['json']
    check(len(big) <= 32 * 1024 or B.req('PUT', f'/api/v1/sync/match/{names[1]}/json', body=big).status == 507, '507 auch für Matches')
    check(A.req('PUT', f"/api/v1/admin/users/{jb['user']['id']}", body={'limit': -5}).status == 400, 'Limit muss ≥ 0 sein')

    # Admin übers Handy (Cookie), Freund darf das nicht
    r = PA.get('/api/v1/admin/users')
    users = r.json() if r.status == 200 else []
    row = next((u for u in users if u['id'] == ja['user']['id']), {})
    keys = {'id', 'name', 'admin', 'used', 'limit', 'matches', 'clips', 'created', 'lastSeen'}
    check(len(users) == 3 and keys <= set(row) and row['matches'] == len(names) and row['clips'] == len(clips) and row['used'] == used, 'admin/users übers Handy')
    check(PB.get('/api/v1/admin/users').status == 403 and B.get('/api/v1/admin/users').status == 403, 'admin/users nur für Admins')
    r = PA.req('POST', '/api/v1/admin/invite', body={'admin': False})
    inv2 = r.json() or {}
    check(r.status == 200 and len(PA.get('/api/v1/admin/invites').json()) == 1, 'Einladung übers Handy')
    check(PA.req('DELETE', f"/api/v1/admin/invites/{inv2['id']}").status == 200 and PA.get('/api/v1/admin/invites').json() == [], 'Einladung zurückziehen')
    check(anon.req('POST', '/api/v1/account/register', body={'invite': inv2['code'], 'name': 'x'}).status == 403, 'zurückgezogen: gilt nicht')
    check(PA.req('DELETE', f"/api/v1/admin/users/{ja['user']['id']}").status == 409, 'letzten Admin nicht entfernen')
    c_dir = pathlib.Path(data_dir) / 'users' / str(c_id)
    check(c_dir.is_dir() and C.get('/api/v1/account').status == 200, 'drittes Konto da')
    check(A.req('DELETE', f'/api/v1/admin/users/{c_id}').status == 200, 'admin: Konto entfernen')
    check(not c_dir.exists() and C.get('/api/v1/account').status == 401, 'entfernt: Ordner weg, Token ungültig')
    check(len(A.get('/api/v1/admin/users').json()) == 2, 'noch 2 Konten')

    # Profil, Abmelden
    r = A.req('PUT', '/api/v1/account/profile', body={'name': 'Micha K.', 'steamId': ME})
    check(r.status == 200 and r.json()['user']['name'] == 'Micha K.', 'Profil ändern')
    check(PA.get('/api/v1/me').json()['user']['name'] == 'Micha K.' and PA.get('/api/v1/info').json()['me']['name'] == 'Micha K.', 'Name überall')
    check(PB.req('POST', '/api/v1/logout').status == 200 and PB.get('/api/v1/info').status == 401, 'Handy abmelden')

    # Grenze gegen Raten beim Koppeln: 5 Fehlversuche pro IP, dann 429; der Code verfällt (wie in FragDesk)
    pc = A.req('POST', '/api/v1/sync/pairing').json()['code']
    wrong = next(c for c in ('000000', '000001', '000002') if c != pc)
    guess = Client(port, ip='203.0.113.9')
    statuses = [guess.req('POST', '/api/v1/pair', body={'code': wrong}).status for _ in range(5)]
    check(statuses == [403, 403, 403, 403, 429], f'Kopplung: Fehlversuche {statuses}')
    check(guess.req('POST', '/api/v1/pair', body={'code': pc}).status == 429, 'gesperrte IP: auch mit richtigem Code 429')
    check(anon.req('POST', '/api/v1/pair', body={'code': pc}).status == 403, 'Code nach 5 Fehlversuchen verfallen')

    # Befehlszeile
    rc, out, _ = srv.cli('users')
    check(rc == 0 and 'Micha K.' in out and 'Freund' in out and '2 Konten' in out, 'users')
    rc, out, _ = srv.cli('users', '--json')
    check(rc == 0 and len(json.loads(out)) == 2, 'users --json')
    r = subprocess.run([srv.binary, 'users', '--data', str(pathlib.Path(data_dir) / 'gibtsnicht')], capture_output=True, text=True)
    check(r.returncode == 1 and 'local.key' in r.stderr, 'users ohne local.key: Fehler')
    r = anon.req('POST', '/api/v1/local/invite', body={}, headers={'X-FragDesk-Local': 'falsch'})
    check(r.status == 403, 'local: falscher Schlüssel')
    key = (pathlib.Path(data_dir) / 'local.key').read_text().strip()
    check(Client(port, ip='192.0.2.1').req('POST', '/api/v1/local/invite', body={}, headers={'X-FragDesk-Local': key}).status == 404,
          'local: nicht über Caddy (X-Forwarded-For)')

    # „PC neu verbinden“: neuer Schlüssel für ein bestehendes Konto (neuer PC, nach „Trennen“) – der alte gilt nicht mehr,
    # Name und Daten bleiben, kein neues Konto
    r = PA.req('POST', '/api/v1/admin/invite', body={'user': jb['user']['id'], 'days': 7})
    rl = r.json() or {}
    check(r.status == 200 and rl.get('user') == jb['user']['id'] and rl.get('userName') == 'Freund' and rl.get('admin') is False
          and rl.get('expires', 0) <= time.time() + 86400 + 5, f'admin: PC neu verbinden, höchstens 1 Tag ({rl})')
    lst = PA.get('/api/v1/admin/invites').json()
    check(any(i['id'] == rl['id'] and i['user'] == jb['user']['id'] and i['userName'] == 'Freund' for i in lst), 'invites: mit Konto')
    check(PA.req('POST', '/api/v1/admin/invite', body={'user': 'ffffffff'}).status == 404, 'PC neu verbinden: Konto muss es geben')
    check(B.req('POST', '/api/v1/admin/invite', body={'user': jb['user']['id']}).status == 403, 'PC neu verbinden: nur Admins (oder eigenes Handy)')
    b_state = B.get('/api/v1/sync/state').json()
    r = anon.req('POST', '/api/v1/account/register', body={'invite': rl['link'], 'name': 'Anderer Name', 'steamId': '7656', 'client': 'FragDesk 1.4.0'})
    jr = r.json() or {}
    check(r.status == 200 and jr['user']['id'] == jb['user']['id'] and jr['user']['name'] == 'Freund' and jr['user']['admin'] is False,
          f'PC neu verbunden: dasselbe Konto, Name bleibt ({jr.get("user")})')
    B2 = Client(port, token=jr.get('token'))
    check(B.get('/api/v1/account').status == 401, 'alter Schlüssel gilt nicht mehr')
    check(B2.get('/api/v1/sync/state').json() == b_state, 'neuer Schlüssel: Daten unverändert')
    check(len(A.get('/api/v1/admin/users').json()) == 2, 'kein neues Konto')
    check(anon.req('POST', '/api/v1/account/register', body={'invite': rl['code'], 'name': 'x'}).status == 403, 'Link gilt nur einmal')
    B = B2
    # vom eigenen Handy (auch ohne Admin)
    r = PA.req('POST', '/api/v1/me/relink')
    own = r.json() or {}
    check(r.status == 200 and own.get('user') == ja['user']['id'] and '#invite=' in own.get('link', ''), 'Handy: PC neu verbinden fürs eigene Konto')
    check(anon.req('POST', '/api/v1/me/relink').status == 401 and PA.req('POST', '/api/v1/me/relink', fd=False).status == 403,
          'me/relink nur gekoppelt und mit X-FragDesk')
    check(PA.req('DELETE', f"/api/v1/admin/invites/{own['id']}").status == 200, 'eigenen Link zurückziehen')
    rc, out, _ = srv.cli('invite', '--user', jb['user']['id'])
    check(rc == 0 and 'PC neu verbinden' in out and '#invite=' in out and '1 Tag' in out, f'invite --user ({out!r})')
    rc, _, err = srv.cli('invite', '--user', 'kein-konto')
    check(rc == 2, 'invite --user: ID geprüft')

    # Konto-Token abmelden
    check(B.req('POST', '/api/v1/account/logout').status == 200 and B.get('/api/v1/account').status == 401, 'FragDesk abmelden')

    # Auf der Platte: nur SHA-256 der Schlüssel, Rechte
    blob = b''.join(p.read_bytes() for p in pathlib.Path(data_dir).rglob('*.json'))
    check(A.token.encode() not in blob and PA.cookie.encode() not in blob and hashlib.sha256(A.token.encode()).hexdigest().encode() in blob, 'Schlüssel nur als SHA-256')
    check(code_a.encode() not in blob, 'Einladungscode nicht im Klartext')
    mode = lambda p: os.stat(p).st_mode & 0o777
    check(mode(data_dir) == 0o700 and mode(f'{data_dir}/server.json') == 0o600 and mode(f'{data_dir}/local.key') == 0o600, 'Rechte 0700/0600')
    check(not list(pathlib.Path(data_dir).rglob('*.tmp-*')), 'keine Reste von temp-Dateien')

    # Neustart: alles noch da
    check(srv.stop() == 0, 'Neustart: sauber beendet')
    srv.start()
    check(A.get('/api/v1/account').status == 200 and PA.get('/api/v1/info').json()['matches'] == len(names), 'nach Neustart: Konto und Handy')
    st2 = A.get('/api/v1/sync/state').json()
    check(st2 == dict(st, quota=st2['quota']) and st2['quota']['used'] == used, 'nach Neustart: gleicher Stand')
    check(PA.get(f"/media/clip/{clips[-1]['id']}.mp4", headers={'Range': 'bytes=0-9'}).body == clips[-1]['mp4'][:10], 'nach Neustart: Video')

    # Löschen
    check(A.req('DELETE', f'/api/v1/sync/match/{first}').status == 200, 'Match löschen')
    check(PA.get('/api/v1/info').json()['matches'] == len(names) - 1 and PA.get(f'/api/v1/match/{first}?tab=0').status == 404, 'gelöscht: weg')
    check(A.req('DELETE', f"/api/v1/sync/clip/{v0['id']}").status == 200, 'Video löschen')
    check(PA.get(f"/media/clip/{v0['id']}.mp4").status == 404 and v0['id'] not in A.get('/api/v1/sync/state').json()['clips'], 'Video weg')
    check(A.req('POST', '/api/v1/account/logout').status == 200 and A.get('/api/v1/sync/state').status == 401, 'Admin meldet FragDesk ab')


def heat_checks(PA, anon, matches, names):
    """Heatmaps über fragdesk-server: ein Match, alle Schichten mit Punkten, Radarbild (ETag/304), untere Ebene, alle
    Matches einer Map – die .heat-Dateien liest der Server im Thread der Anfrage."""
    info = {c: json.loads(matches[c]['json']) for c in names}
    with_heat = [c for c in names if matches[c]['heat'] and info[c].get('schema', 0) >= 2]
    if not check(with_heat, 'Testdaten: Matches mit Heatmap'):
        return
    c = with_heat[0]
    r = PA.get(f'/api/v1/heat?match={c}')
    d = r.json() or {}
    if not check(r.status == 200 and d.get('scope') == 'match' and d.get('empty') is None and d.get('grid', {}).get('n') == 128 and d['grid']['layers']
                 and d['legend']['items'][0]['label'] == 'Aufenthalt' and d['head']['map'], f'heat: ein Match ({r.status} {r.body[:200]!r})'):
        return
    r = PA.get(f'/api/v1/heat?match={c}&layers=127&marks=1&throws=1')
    d2 = r.json() or {}
    check(r.status == 200 and len(d2.get('legend', {}).get('items', [])) == 7 and d2.get('marks') and d2['matches'][0]['id'] == c,
          'heat: alle Schichten mit Punkten')
    img = PA.get(d['radar']['image'])
    check(img.status == 200 and img.header('Content-Type') == 'image/png' and img.body[:8] == b'\x89PNG\r\n\x1a\n', 'Radarbild')
    check(PA.get(d['radar']['image'], headers={'If-None-Match': img.header('ETag')}).status == 304, 'Radarbild: 304 mit ETag')
    check(anon.get(d['radar']['image']).status == 401 and anon.get(f'/api/v1/heat?match={c}').status == 401, 'Heatmap und Radarbild nur gekoppelt')
    check(PA.get('/api/v1/heat?match=gibtsnicht').status == 404, 'heat: unbekanntes Match 404')
    nuke = [x for x in with_heat if info[x]['match']['map'] == 'de_nuke']
    if nuke:
        r = PA.get(f'/api/v1/heat?match={nuke[0]}&lower=1&who=7')   # alle Spieler (künstlich: „Ich“ ist oben)
        d3 = r.json() or {}
        check(r.status == 200 and d3['filter']['lower'] is True and d3['ui']['levels'] == ['Obere Ebene', 'Untere Ebene'] and d3['grid']['layers']
              and PA.get(d3['radar']['lowerImage']).status == 200, 'heat: untere Ebene (Nuke)')
    # alle Matches einer Map (Statistiken): Matches mit mir und Heatmap-Daten auf dieser Map
    m = info[c]['match']['map']
    n = sum(1 for x in with_heat if info[x]['match']['map'] == m and any(p['steamId'] == ME for p in info[x]['players']))
    r = PA.get(f'/api/v1/heat?mode=0&period=2&pick={m}')
    d4 = r.json() or {}
    want = f"{n} {'Match' if n == 1 else 'Matches'} auf"
    check(r.status == 200 and d4.get('scope') == 'stats' and d4.get('map') == m and want in (d4.get('foot') or '') and any(x['id'] == m for x in d4['maps']),
          f'heat: alle Matches einer Map ({d4.get("foot")!r}, erwartet {want!r})')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--bin')
    ap.add_argument('--data')
    ap.add_argument('--tmp')
    ap.add_argument('--make-data')
    args = ap.parse_args()
    if args.make_data:
        make_data(args.make_data)
        return 0
    if not (args.bin and args.data and args.tmp):
        ap.error('--bin, --data und --tmp angeben (oder --make-data)')
    return run(args)


if __name__ == '__main__':
    sys.exit(main())
