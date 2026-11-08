package main

import (
	"bytes"
	"compress/gzip"
	"math"
	"math/rand"
	"os"
	"path/filepath"
	"testing"

	"github.com/klauspost/compress/zstd"
)

func TestBaseDemoName(t *testing.T) {
	cases := map[string]string{
		"1-6de46e85-0b21.dem.zst": "1-6de46e85-0b21.dem",
		"match730_0034.dem.bz2":   "match730_0034.dem",
		"match.dem.GZ":            "match.dem",
		"match.zst":               "match.dem",
		"match.dem":               "match.dem",
		".zst":                    "demo.dem",
	}
	for in, want := range cases {
		if got := baseDemoName(in); got != want {
			t.Errorf("baseDemoName(%q) = %q, erwartet %q", in, got, want)
		}
	}
}

func TestMergeSegments(t *testing.T) {
	got := mergeSegments([][2]int{{500, 700}, {100, 300}, {320, 400}, {1000, 900}}, 50, 150, 650)
	want := [][2]int{{150, 400}, {500, 650}}
	if len(got) != len(want) {
		t.Fatalf("mergeSegments = %v, erwartet %v", got, want)
	}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("mergeSegments = %v, erwartet %v", got, want)
		}
	}
}

func TestRatings(t *testing.T) {
	c := Counters{"rounds": 20, "kills": 20, "deaths": 14, "assists": 4, "flashAssists": 1, "kastRounds": 15, "dmg": 1700,
		"k1": 8, "k2": 3, "k3": 2}
	r2, r1, kast, adr := ratings(c)
	if math.Abs(kast-75) > 1e-9 || math.Abs(adr-85) > 1e-9 {
		t.Fatalf("KAST/ADR falsch: %v %v", kast, adr)
	}
	if r2 < 1.0 || r2 > 1.4 || r1 < 1.0 || r1 > 1.4 {
		t.Fatalf("Ratings unplausibel: %v %v", r2, r1)
	}
	if r2, _, _, _ := ratings(Counters{}); r2 != 0 {
		t.Fatalf("leere Zähler sollen 0 ergeben")
	}
}

func fakeDemo(t *testing.T, dir string) (string, []byte) {
	data := make([]byte, 300_000)
	rand.New(rand.NewSource(1)).Read(data)
	copy(data, magicCS2)
	p := filepath.Join(dir, "plain.dem")
	if err := os.WriteFile(p, data, 0o644); err != nil {
		t.Fatal(err)
	}
	return p, data
}

func TestPrepareDemo(t *testing.T) {
	dir := t.TempDir()
	plain, data := fakeDemo(t, dir)

	got, packed, cleanup, err := prepareDemo(plain, dir)
	if err != nil || got != plain || packed != "" {
		t.Fatalf("unverpackt: %v %q %q", err, got, packed)
	}
	cleanup()

	var gz bytes.Buffer
	w := gzip.NewWriter(&gz)
	w.Write(data)
	w.Close()
	var zs bytes.Buffer
	zw, _ := zstd.NewWriter(&zs)
	zw.Write(data)
	zw.Close()

	for name, content := range map[string][]byte{"1-abc.dem.gz": gz.Bytes(), "1-abc.dem.zst": zs.Bytes()} {
		p := filepath.Join(dir, name)
		os.WriteFile(p, content, 0o644)
		os.WriteFile(filepath.Join(dir, "1-abc.dem.info"), []byte("info"), 0o644)
		out, packed, cleanup, err := prepareDemo(p, dir)
		if err != nil {
			t.Fatalf("%s: %v", name, err)
		}
		if filepath.Base(out) != "1-abc.dem" || packed == "" {
			t.Errorf("%s: Name %q, gepackt %q", name, filepath.Base(out), packed)
		}
		back, _ := os.ReadFile(out)
		if !bytes.Equal(back, data) {
			t.Errorf("%s: Inhalt weicht ab", name)
		}
		if _, err := os.Stat(out + ".info"); err != nil {
			t.Errorf("%s: .info-Datei fehlt", name)
		}
		cleanup()
		if _, err := os.Stat(filepath.Dir(out)); !os.IsNotExist(err) {
			t.Errorf("%s: Zwischenordner nicht entfernt", name)
		}
	}

	bad := filepath.Join(dir, "bad.dem")
	os.WriteFile(bad, []byte("kein demo"), 0o644)
	if _, _, _, err := prepareDemo(bad, dir); errorCode(err) != "FORMAT" {
		t.Errorf("falsches Format: %v", err)
	}
	if _, _, _, err := prepareDemo(filepath.Join(dir, "fehlt.dem"), dir); errorCode(err) != "NOT_FOUND" {
		t.Errorf("fehlende Datei: %v", err)
	}
	// gzip, das keine Demo enthält
	var notDemo bytes.Buffer
	w = gzip.NewWriter(&notDemo)
	w.Write([]byte("hallo"))
	w.Close()
	p := filepath.Join(dir, "x.dem.gz")
	os.WriteFile(p, notDemo.Bytes(), 0o644)
	if _, _, _, err := prepareDemo(p, dir); errorCode(err) != "FORMAT" {
		t.Errorf("gzip ohne Demo: %v", err)
	}
}

// Echte Demos (awpy-fixtures, github.com/pnxenopoulos/awpy-fixtures) mit Werten von csstats.gg, FACEIT und HLTV.
// Aufruf: FRAGDESK_DEMOS=<Ordner> go test -run Fixtures
type expect struct {
	kills, deaths, hs int
	adr, kast         float64
}

var fixtures = map[string]struct {
	mode, mapName  string
	scoreA, scoreB int
	players        map[string]expect
}{
	"valve-de_mirage-472514809.dem": {"premier", "de_mirage", 0, 13, map[string]expect{
		"organner.pl free trial": {kills: 27, deaths: 0, hs: 11, adr: 183.2, kast: -1},
		"bargota22":              {kills: 4, deaths: 13, hs: 1, adr: -1, kast: 23.1},
	}},
	"faceit-de_mirage-6de46e85.dem": {"faceit", "de_mirage", 16, 14, map[string]expect{
		"babaya0310": {kills: 21, deaths: 25, hs: 7, adr: 82.8, kast: 70.0},
		"cntoz":      {kills: 19, deaths: 19, hs: 8, adr: 63.9, kast: -1},
	}},
	// ESEA läuft über FACEIT-Server: die Quelle ist FACEIT
	"esea-de_dust2-2c5bdd12.dem": {"faceit", "de_dust2", 6, 13, map[string]expect{
		"jared": {kills: 10, deaths: 11, hs: 5, adr: 42.9, kast: 73.7},
		"1umar": {kills: 16, deaths: 10, hs: 2, adr: 83.6, kast: -1},
	}},
	// Profi-Match (HLTV): ADR von dgt weicht um 0,7 ab (HLTV zählt Schaden am Rundenende anders) und wird nicht geprüft
	"hltv-de_inferno-2395492.dem": {"other", "de_inferno", 10, 13, map[string]expect{
		"dgt":  {kills: 20, deaths: 7, hs: -1, adr: -1, kast: 95.7},
		"Jame": {kills: 12, deaths: 12, hs: -1, adr: 52.7, kast: -1},
	}},
}

func TestFixtures(t *testing.T) {
	root := os.Getenv("FRAGDESK_DEMOS")
	if root == "" {
		t.Skip("FRAGDESK_DEMOS nicht gesetzt")
	}
	for file, fx := range fixtures {
		matches, _ := filepath.Glob(filepath.Join(root, "*", file))
		if len(matches) == 0 {
			t.Logf("%s fehlt, übersprungen", file)
			continue
		}
		out, err := analyzeFile(matches[0], t.TempDir(), "", func(string) {})
		if err != nil {
			t.Fatalf("%s: %v", file, err)
		}
		if out.Match.Mode != fx.mode || out.Match.Map != fx.mapName || out.Match.ScoreA != fx.scoreA || out.Match.ScoreB != fx.scoreB {
			t.Errorf("%s: Match %+v", file, out.Match)
		}
		byName := map[string]*PlayerOut{}
		for _, p := range out.Players {
			byName[p.Name] = p
		}
		for name, e := range fx.players {
			p := byName[name]
			if p == nil {
				t.Errorf("%s: Spieler %s fehlt", file, name)
				continue
			}
			s := p.Stats
			if int(s["kills"]) != e.kills || int(s["deaths"]) != e.deaths || (e.hs >= 0 && int(s["hsKills"]) != e.hs) {
				t.Errorf("%s/%s: K/D/HS %v/%v/%v, erwartet %d/%d/%d", file, name, s["kills"], s["deaths"], s["hsKills"], e.kills, e.deaths, e.hs)
			}
			if e.adr >= 0 && math.Abs(p.Adr-e.adr) > 0.6 {
				t.Errorf("%s/%s: ADR %.1f, erwartet %.1f", file, name, p.Adr, e.adr)
			}
			if e.kast >= 0 && math.Abs(p.Kast-e.kast) > 0.6 {
				t.Errorf("%s/%s: KAST %.1f, erwartet %.1f", file, name, p.Kast, e.kast)
			}
			// Seiten müssen sich zum Gesamtwert addieren
			for _, k := range []string{"kills", "deaths", "dmg", "rounds", "shots"} {
				if math.Abs(p.StatsT[k]+p.StatsCT[k]-s[k]) > 0.01 {
					t.Errorf("%s/%s: %s T+CT=%v, gesamt %v", file, name, k, p.StatsT[k]+p.StatsCT[k], s[k])
				}
			}
			if len(p.Rounds) != len(out.Rounds) {
				t.Errorf("%s/%s: %d Runden, Match hat %d", file, name, len(p.Rounds), len(out.Rounds))
			}
		}
		if len(out.Highlights) == 0 {
			t.Errorf("%s: keine Highlights", file)
		}
		for _, h := range out.Highlights {
			for _, s := range h.Segments {
				if s[1] <= s[0] {
					t.Errorf("%s: leerer Abschnitt in %s", file, h.ID)
				}
			}
		}
	}
}
