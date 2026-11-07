package main

import (
	"encoding/binary"
	"math"
	"os"
	"path/filepath"
	"strconv"
	"testing"
)

// Kleiner Leser für das FDRP-Format (Gegenstück in src/core/replay.cpp), nur für die Tests
type rdr struct {
	b   []byte
	o   int
	err bool
}

func (r *rdr) n(k int) []byte {
	if r.o+k > len(r.b) {
		r.err = true
		return make([]byte, k)
	}
	v := r.b[r.o : r.o+k]
	r.o += k
	return v
}
func (r *rdr) u8() uint8   { return r.n(1)[0] }
func (r *rdr) u16() uint16 { return binary.LittleEndian.Uint16(r.n(2)) }
func (r *rdr) u32() uint32 { return binary.LittleEndian.Uint32(r.n(4)) }
func (r *rdr) i16() int16  { return int16(r.u16()) }
func (r *rdr) str() string { return string(r.n(int(r.u16()))) }
func (r *rdr) vec() vec3   { return vec3{r.i16(), r.i16(), r.i16()} }

type decoded struct {
	tickrate float32
	interval int
	mapName  string
	players  []replayPlayer
	weapons  []string
	frames   int
	samples  int
	maxAbs   int
	tracks   []grenadeTrack
	effects  []replayEffect
	kills    []replayKill
	shots    []replayShot
	bomb     []replayBomb
}

func decodeReplay(t *testing.T, b []byte) decoded {
	t.Helper()
	r := &rdr{b: b}
	var d decoded
	if string(r.n(4)) != "FDRP" || r.u16() != replayVersion {
		t.Fatal("Kopf falsch")
	}
	r.u16()
	d.tickrate = math.Float32frombits(r.u32())
	d.interval = int(r.u16())
	d.mapName = r.str()
	for i, n := 0, int(r.u16()); i < n; i++ {
		d.players = append(d.players, replayPlayer{binary.LittleEndian.Uint64(r.n(8)), r.str()})
	}
	for i, n := 0, int(r.u16()); i < n; i++ {
		d.weapons = append(d.weapons, r.str())
	}
	d.frames = int(r.u32())
	end := int(r.u32()) + r.o
	for f := 0; f < d.frames; f++ {
		r.u32()
		if r.u8() != 0 {
			r.vec()
		}
		for i, n := 0, int(r.u8()); i < n; i++ {
			p := r.u8()
			v := r.vec()
			r.n(4)
			if int(p) >= len(d.players) {
				t.Fatalf("Spielerindex %d außerhalb", p)
			}
			for _, c := range []int16{v.x, v.y} {
				d.maxAbs = max(d.maxAbs, int(math.Abs(float64(c))))
			}
			d.samples++
		}
	}
	if r.o != end {
		t.Fatalf("Frames enden bei %d statt %d", r.o, end)
	}
	for i, n := 0, int(r.u32()); i < n; i++ {
		tr := grenadeTrack{kind: r.u8(), thrower: r.u8(), ct: r.u8() == 1}
		for j, m := 0, int(r.u16()); j < m; j++ {
			tr.points = append(tr.points, trackPoint{int32(r.u32()), r.vec()})
		}
		d.tracks = append(d.tracks, tr)
	}
	for i, n := 0, int(r.u32()); i < n; i++ {
		fx := replayEffect{kind: r.u8(), thrower: r.u8(), ct: r.u8() == 1, start: int32(r.u32()), end: int32(r.u32()), pos: r.vec()}
		for j, m := 0, int(r.u8()); j < m; j++ {
			fx.hull = append(fx.hull, [2]int16{r.i16(), r.i16()})
		}
		d.effects = append(d.effects, fx)
	}
	for i, n := 0, int(r.u32()); i < n; i++ {
		k := replayKill{tick: int32(r.u32()), killer: r.u8(), victim: r.u8(), assister: r.u8(), weapon: r.u8(), fl: r.u8()}
		k.killerPos, k.victimPos = r.vec(), r.vec()
		d.kills = append(d.kills, k)
	}
	for i, n := 0, int(r.u32()); i < n; i++ {
		d.shots = append(d.shots, replayShot{int32(r.u32()), r.u8(), r.u8()})
	}
	for i, n := 0, int(r.u32()); i < n; i++ {
		d.bomb = append(d.bomb, replayBomb{int32(r.u32()), r.u8(), r.u8(), r.vec()})
	}
	if string(r.n(4)) != "END!" || r.err || r.o != len(b) {
		t.Fatalf("Ende fehlt oder Rest übrig (%d von %d)", r.o, len(b))
	}
	return d
}

func TestReplayEncode(t *testing.T) {
	rb := &replayBuilder{interval: 8, playerIdx: map[string]int{}, weaponIdx: map[string]int{"": 0}, weapons: []string{"", "AK-47"}}
	rb.players = []replayPlayer{{76561198000000001, "Testspieler"}, {0, "Bot Ich"}}
	// zwei Frames von Hand: Tick 100 mit beiden Spielern, Tick 108 nur mit Spieler 0 und liegender Bombe
	f := binary.LittleEndian.AppendUint32(nil, 100)
	f = append(f, 0, 2)
	f = append(f, 0)
	f = appendVec(f, vec3{-100, 200, 30})
	f = append(f, 64, 100, 1, pfCT|pfKit)
	f = append(f, 1)
	f = appendVec(f, vec3{500, -500, 0})
	f = append(f, 192, 35, 1, pfBomb)
	f = binary.LittleEndian.AppendUint32(f, 108)
	f = append(f, 1)
	f = appendVec(f, vec3{1, 2, 3})
	f = append(f, 1, 0)
	f = appendVec(f, vec3{-90, 210, 30})
	f = append(f, 70, 100, 1, pfCT)
	rb.frames, rb.nFrames = f, 2
	rb.trackList = []*grenadeTrack{{kind: grSmoke, thrower: 0, ct: true, points: []trackPoint{{100, vec3{1, 1, 1}}, {108, vec3{50, 60, 5}}}}}
	rb.effects = []*replayEffect{{kind: fxFire, thrower: 1, start: 120, end: 1400, pos: vec3{10, 20, 0}, hull: [][2]int16{{0, 0}, {10, 0}, {0, 10}}}}
	rb.kills = []replayKill{{tick: 104, killer: 0, victim: 1, assister: noPlayer, weapon: 1, fl: kfHeadshot | kfSmoke,
		killerPos: vec3{-100, 200, 30}, victimPos: vec3{500, -500, 0}}}
	rb.shots = []replayShot{{103, 0, 1}}
	rb.bomb = []replayBomb{{200, bombPlanted, 1, vec3{7, 8, 9}}}

	d := decodeReplay(t, rb.encode(128, "de_mirage"))
	if d.tickrate != 128 || d.interval != 8 || d.mapName != "de_mirage" || len(d.players) != 2 || d.players[1].name != "Bot Ich" {
		t.Fatalf("Kopf: %+v", d)
	}
	if d.frames != 2 || d.samples != 3 || len(d.tracks) != 1 || len(d.tracks[0].points) != 2 || len(d.effects) != 1 ||
		len(d.effects[0].hull) != 3 || len(d.kills) != 1 || d.kills[0].fl != kfHeadshot|kfSmoke || len(d.shots) != 1 || len(d.bomb) != 1 {
		t.Fatalf("Inhalt: %+v", d)
	}
	if d.kills[0].victimPos != (vec3{500, -500, 0}) || d.bomb[0].pos != (vec3{7, 8, 9}) {
		t.Fatalf("Positionen: %+v / %+v", d.kills[0], d.bomb[0])
	}
}

func TestReplayHelpers(t *testing.T) {
	if clamp16(40000) != math.MaxInt16 || clamp16(-40000) != math.MinInt16 || clamp16(12.6) != 13 {
		t.Fatal("clamp16")
	}
	if a := hullArea([][2]int16{{0, 0}, {10, 0}, {10, 10}, {0, 10}}); a != 100 {
		t.Fatalf("Fläche %v", a)
	}
}

// Mit echten Demos (FRAGDESK_DEMOS): vollständige Datei, sinnvolle Inhalte, Kills passen zur Auswertung
func TestReplayFixtures(t *testing.T) {
	root := os.Getenv("FRAGDESK_DEMOS")
	if root == "" {
		t.Skip("FRAGDESK_DEMOS nicht gesetzt")
	}
	for file, fx := range fixtures {
		matches, _ := filepath.Glob(filepath.Join(root, "*", file))
		if len(matches) == 0 {
			continue
		}
		target := filepath.Join(t.TempDir(), "x.replay")
		if err := replayFile(matches[0], target, t.TempDir()); err != nil {
			t.Fatalf("%s: %v", file, err)
		}
		b, _ := os.ReadFile(target)
		d := decodeReplay(t, b)
		if d.mapName != fx.mapName || d.tickrate <= 0 || d.frames < 1000 || len(d.players) < 10 || len(d.kills) < 50 ||
			len(d.effects) < 20 || len(d.tracks) < 20 || d.maxAbs > 16384 {
			t.Errorf("%s: Karte %s, Tickrate %v, %d Frames, %d Spieler, %d Kills, %d Effekte, %d Flugbahnen, max %d",
				file, d.mapName, d.tickrate, d.frames, len(d.players), len(d.kills), len(d.effects), len(d.tracks), d.maxAbs)
		}
		// Kills müssen zu den Kills der Auswertung passen (gleiche Ticks)
		out, err := analyzeFile(matches[0], t.TempDir(), func(string) {})
		if err != nil {
			t.Fatal(err)
		}
		ticks := map[int32]bool{}
		for _, k := range d.kills {
			ticks[k.tick] = true
		}
		miss := 0
		for _, k := range out.Kills {
			if !ticks[int32(k.Tick)] {
				miss++
			}
		}
		if miss > len(out.Kills)/20 {
			t.Errorf("%s: %d von %d Kills fehlen in den 2D-Daten", file, miss, len(out.Kills))
		}
		// keine Trainer oder Zuschauer: jeder Mensch in den 2D-Daten ist auch Spieler der Auswertung
		known := map[string]bool{}
		for _, p := range out.Players {
			known[p.SteamID] = true
		}
		for _, p := range d.players {
			if p.steamID != 0 && !known[strconv.FormatUint(p.steamID, 10)] {
				t.Errorf("%s: %s (%d) ist kein Spieler der Auswertung", file, p.name, p.steamID)
			}
		}
		t.Logf("%s: %d KB, %d Frames, %d Flugbahnen, %d Effekte", file, len(b)/1024, d.frames, len(d.tracks), len(d.effects))
	}
}
