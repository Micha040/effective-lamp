#!/usr/bin/env python3
"""FragDesk – Datenordner mit vielen Matches für Bilder der Oberfläche und Tests der Laufzeit.

Aus echten fdanalyze-Ausgaben (öffentliche Test-Demos) entstehen Kopien, in denen ein erfundener Testspieler
jeweils die Rolle eines anderen echten Spielers übernimmt. So passen alle Zahlen innerhalb eines Matches zueinander,
und über alle Kopien ergibt sich ein abwechslungsreicher Verlauf. Datum, Map-Name (nur bei Kopien desselben Matches)
und Premier-Wertung werden für eine glaubwürdige Verlaufskurve gesetzt. Es sind ausdrücklich Testdaten.

Aufruf: python3 tests/make_testdata.py <Datenordner> <fdanalyze.json> [<fdanalyze.json> …] [--count N]
Danach: FragDesk.exe --data <Datenordner> --me 76561190000000001 --render bild.png --page dashboard
"""
import argparse
import datetime
import json
import pathlib
import random

ME = '76561190000000001'
ME_NAME = 'Testspieler'
MAPS = ['de_mirage', 'de_inferno', 'de_ancient', 'de_nuke', 'de_anubis', 'de_dust2', 'de_train', 'de_overpass']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('sources', nargs='+')
    ap.add_argument('--count', type=int, default=40, help='Anzahl Matches (Standard 40)')
    ap.add_argument('--seed', type=int, default=5, help='Startwert (5 ergibt die Bilder der README)')
    ap.add_argument('--demo-path', help='vorhandene Demo-Datei, auf die alle Matches zeigen (dann sind „Ansehen“/„Aufnehmen“ aktiv)')
    args = ap.parse_args()

    rnd = random.Random(args.seed)
    sources = [json.loads(pathlib.Path(s).read_text(encoding='utf-8')) for s in args.sources]
    # Rollen: jeder Spieler mit mindestens einem Kill aus jedem Match
    roles = []
    for si, src in enumerate(sources):
        for p in src['players']:
            if (p.get('stats') or {}).get('kills'):
                roles.append((si, p['steamId']))
    if not roles:
        raise SystemExit('keine Spieler mit Kills gefunden')

    out = pathlib.Path(args.out)
    (out / 'matches').mkdir(parents=True, exist_ok=True)
    for old in (out / 'matches').glob('test-*.json'):
        old.unlink()

    now = datetime.datetime(2026, 11, 6, 21, 30, tzinfo=datetime.timezone.utc)
    rating = 13850
    # aus jedem Match mindestens eine Rolle, der Rest zufällig
    picks = []
    for si in range(len(sources)):
        own = [r for r in roles if r[0] == si]
        if own:
            picks.append(rnd.choice(own))
    while len(picks) < args.count:
        picks.append(rnd.choice(roles))
    picks = picks[:args.count]
    rnd.shuffle(picks)
    for i, (si, sid) in enumerate(picks):
        age = args.count - 1 - i                      # 0 = neuestes Match
        when = now - datetime.timedelta(days=age * 1.6 + rnd.random() * 0.5, hours=rnd.randint(0, 5))
        text = json.dumps(sources[si], ensure_ascii=False).replace(sid, ME)
        m = json.loads(text)
        m['checksum'] = f'test-{i:03d}'
        stamp = when.strftime('%Y-%m-%dT%H:%M:%SZ')
        m['match']['date'] = stamp
        m['demo']['modified'] = stamp
        m['demo']['file'] = f'testmatch-{i:03d}.dem'
        m['demo']['path'] = args.demo_path or f'C:\\FragDesk-Testdaten\\testmatch-{i:03d}.dem'
        # Kopien desselben Matches bekommen verschiedene Map-Namen, damit die Map-Übersicht etwas zeigt
        if sum(1 for s, _ in picks[:i] if s == si) > 0 and m['match']['mode'] == 'premier':
            m['match']['map'] = MAPS[i % len(MAPS)]
        me = next(p for p in m['players'] if p['steamId'] == ME)
        me['name'] = ME_NAME
        if m['match']['mode'] == 'premier':
            won = me['result'] == 'win'
            delta = rnd.randint(170, 430) if won else -rnd.randint(140, 360)
            if me['result'] == 'draw':
                delta = rnd.randint(-40, 40)
            me['rankType'] = 11
            me['rankOld'] = rating
            me['rankNew'] = rating + delta
            rating += delta
        (out / 'matches' / f'test-{i:03d}.json').write_text(json.dumps(m, ensure_ascii=False), encoding='utf-8')
    settings = {'steamId': ME, 'theme': '', 'watchReplays': False, 'watchDownloads': False}
    (out / 'settings.json').write_text(json.dumps(settings, indent=1), encoding='utf-8')
    print(f'{args.count} Matches in {out / "matches"}; Premier am Ende: {rating}')


if __name__ == '__main__':
    main()
