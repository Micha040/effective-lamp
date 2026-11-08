package main

import (
	"math"
	"os"
	"path/filepath"
	"strconv"
	"testing"

	"github.com/golang/geo/r3"
)

// Gegenstück zu src/core/heat.cpp, nur für die Tests
type heatDecoded struct {
	tickrate float32
	mapName  string
	players  []replayPlayer
	weapons  []string
	kills    []heatKill
	nades    []heatNade
	frames   int
	samples  int
}

func decodeHeat(t *testing.T, b []byte) heatDecoded {
	t.Helper()
	r := &rdr{b: b}
	var d heatDecoded
	if string(r.n(4)) != "FDHM" || r.u16() != heatVersion {
		t.Fatal("Kopf falsch")
	}
	r.u16()
	d.tickrate = math.Float32frombits(r.u32())
	d.mapName = r.str()
	for i, n := 0, int(r.u16()); i < n; i++ {
		id := uint64(r.u32()) | uint64(r.u32())<<32
		d.players = append(d.players, replayPlayer{id, r.str()})
	}
	for i, n := 0, int(r.u16()); i < n; i++ {
		d.weapons = append(d.weapons, r.str())
	}
	for i, n := 0, int(r.u32()); i < n && !r.err; i++ {
		k := heatKill{tick: int32(r.u32())}
		k.killer, k.victim, k.fl, k.weapon = r.u8(), r.u8(), r.u8(), r.u8()
		k.killerPos, k.victimPos = r.vec(), r.vec()
		d.kills = append(d.kills, k)
	}
	for i, n := 0, int(r.u32()); i < n && !r.err; i++ {
		g := heatNade{tick: int32(r.u32()), land: int32(r.u32())}
		g.kind, g.thrower, g.fl = r.u8(), r.u8(), r.u8()
		g.from, g.to = r.vec(), r.vec()
		d.nades = append(d.nades, g)
	}
	nf, fb := int(r.u32()), int(r.u32())
	end := r.o + fb
	for i := 0; i < nf && !r.err; i++ {
		r.u32()
		n := int(r.u8())
		for k := 0; k < n; k++ {
			if p := r.u8(); int(p) >= len(d.players) {
				t.Fatalf("Spieler %d unbekannt", p)
			}
			r.u8()
			r.vec()
		}
		d.samples += n
		d.frames++
	}
	if r.err || r.o != end || string(r.n(4)) != "END!" || r.o != len(b) {
		t.Fatal("Datei beschädigt")
	}
	return d
}

func TestAimAngles(t *testing.T) {
	eye := r3.Vector{X: 0, Y: 0, Z: 64}
	near := func(a, b float64) bool { return math.Abs(a-b) < 0.05 }
	// genau drauf
	if tot, y, p := aimAngles(eye, r3.Vector{X: 1000, Y: 0, Z: 64}, 0, 0); !near(tot, 0) || !near(y, 0) || !near(p, 0) {
		t.Fatalf("drauf: %v %v %v", tot, y, p)
	}
	// 10° links daneben (Yaw läuft gegen den Uhrzeigersinn)
	head := r3.Vector{X: 1000, Y: 1000 * math.Tan(10*math.Pi/180), Z: 64}
	if tot, y, p := aimAngles(eye, head, 0, 0); !near(tot, 10) || !near(y, 10) || !near(p, 0) {
		t.Fatalf("waagerecht: %v %v %v", tot, y, p)
	}
	// Kopf 5° tiefer, Blick waagerecht: senkrecht 5° (Pitch positiv = nach unten)
	head = r3.Vector{X: 1000, Y: 0, Z: 64 - 1000*math.Tan(5*math.Pi/180)}
	if tot, y, p := aimAngles(eye, head, 0, 0); !near(tot, 5) || !near(y, 0) || !near(p, 5) {
		t.Fatalf("senkrecht: %v %v %v", tot, y, p)
	}
	if tot, _, p := aimAngles(eye, head, 0, 5); !near(tot, 0) || !near(p, 0) {
		t.Fatalf("Blick nach unten: %v %v", tot, p)
	}
	// Spielwerte über 180 (355 = 5° nach oben) und Yaw über die Grenze ±180
	if _, y, _ := aimAngles(eye, r3.Vector{X: -1000, Y: -10, Z: 64}, 179, 0); y > 2 {
		t.Fatalf("über die Grenze: %v", y)
	}
	head = r3.Vector{X: 1000, Y: 0, Z: 64 + 1000*math.Tan(5*math.Pi/180)}
	if tot, _, _ := aimAngles(eye, head, 0, 355); !near(tot, 0) {
		t.Fatalf("355: %v", tot)
	}
	// Gegner hinter mir
	if tot, y, _ := aimAngles(eye, r3.Vector{X: -1000, Y: 0, Z: 64}, 0, 0); !near(tot, 180) || !near(y, 180) {
		t.Fatalf("hinten: %v %v", tot, y)
	}
}

func TestHeatEncode(t *testing.T) {
	eb := &extrasBuilder{weapons: []string{"", "AK-47"}, tickrate: 64}
	eb.players = []replayPlayer{{76561198000000001, "eins"}, {0, "Bot"}}
	eb.kills = []heatKill{{tick: 100, killer: 0, victim: 1, fl: hkKillerCT | hkHeadshot, weapon: 1, killerPos: vec3{1, 2, 3}, victimPos: vec3{-4, -5, -6}}}
	eb.nades = []heatNade{{tick: 50, land: 90, kind: grSmoke, thrower: 0, fl: hnCT, from: vec3{10, 20, 30}, to: vec3{-100, 200, 0}}}
	eb.addNade(heatNade{tick: 80, land: 92, kind: grSmoke, thrower: 1, fl: hnNoThrow, to: vec3{-90, 205, 0}}) // doppelt
	eb.addNade(heatNade{tick: 80, land: 95, kind: grFlash, thrower: 1, to: vec3{-90, 205, 0}})                // andere Art
	eb.frames = append(eb.frames, 64, 0, 0, 0, 2, 0, hsCT, 1, 0, 2, 0, 3, 0, 1, 0, 4, 0, 5, 0, 6, 0)
	eb.nFrames = 1
	d := decodeHeat(t, eb.encode("de_mirage"))
	if d.mapName != "de_mirage" || d.tickrate != 64 || len(d.players) != 2 || d.players[0].steamID != 76561198000000001 ||
		len(d.weapons) != 2 || d.weapons[1] != "AK-47" {
		t.Fatalf("Kopf: %+v", d)
	}
	if len(d.kills) != 1 || d.kills[0] != eb.kills[0] {
		t.Fatalf("Kills: %+v", d.kills)
	}
	if len(d.nades) != 2 || d.nades[0] != eb.nades[0] || d.nades[1].kind != grFlash {
		t.Fatalf("Granaten: %+v", d.nades)
	}
	if d.frames != 1 || d.samples != 2 {
		t.Fatalf("Abtastungen: %d %d", d.frames, d.samples)
	}
	if heatPath(`C:\x\tmp-1.json`) != `C:\x\tmp-1.heat` {
		t.Fatal(heatPath(`C:\x\tmp-1.json`))
	}
}

func TestExtrasFixtures(t *testing.T) {
	root := os.Getenv("FRAGDESK_DEMOS")
	if root == "" {
		t.Skip("FRAGDESK_DEMOS nicht gesetzt")
	}
	for file, fx := range fixtures {
		matches, _ := filepath.Glob(filepath.Join(root, "*", file))
		if len(matches) == 0 {
			continue
		}
		out, err := analyzeFile(matches[0], t.TempDir(), "", func(string) {})
		if err != nil {
			t.Fatalf("%s: %v", file, err)
		}
		if out.Schema != 2 || len(out.heat) == 0 {
			t.Fatalf("%s: Schema %d, %d Bytes Heatmap", file, out.Schema, len(out.heat))
		}
		d := decodeHeat(t, out.heat)
		if d.mapName != fx.mapName || len(d.kills) < len(out.Kills) || d.frames < 300 || len(d.nades) < 50 {
			t.Errorf("%s: Karte %s, %d Kills (Auswertung %d), %d Abtastungen, %d Granaten", file, d.mapName, len(d.kills),
				len(out.Kills), d.frames, len(d.nades))
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
			t.Errorf("%s: %d von %d Kills fehlen in der Heatmap", file, miss, len(out.Kills))
		}
		noThrow := 0
		for _, n := range d.nades {
			if n.fl&hnNoThrow != 0 {
				noThrow++
			}
		}
		if noThrow > len(d.nades)/10 {
			t.Errorf("%s: %d von %d Granaten ohne Wurf", file, noThrow, len(d.nades))
		}
		// Zielen: die meisten Spieler haben Messungen, die Werte liegen in einem menschlichen Bereich
		measured := 0
		for _, p := range out.Players {
			n := p.Stats["cpN"]
			if n < 5 {
				continue
			}
			measured++
			cp, ttd := p.Stats["cpSum"]/n, p.Stats["ttdSum"]/math.Max(1, p.Stats["ttdN"])
			if cp < 0.5 || cp > 30 || (p.Stats["ttdN"] > 0 && (ttd < 30 || ttd > 2000)) {
				t.Errorf("%s: %s Crosshair %.1f°, Reaktion %.0f ms", file, p.Name, cp, ttd)
			}
			if p.Stats["cpN"] != p.StatsT["cpN"]+p.StatsCT["cpN"] {
				t.Errorf("%s: %s T+CT passt nicht", file, p.Name)
			}
			w := 0.0
			for _, wo := range p.Weapons {
				w += wo.Stats["cpN"]
			}
			if w != p.Stats["cpN"] {
				t.Errorf("%s: %s Waffen %v statt %v", file, p.Name, w, p.Stats["cpN"])
			}
		}
		if measured < 5 {
			t.Errorf("%s: nur %d Spieler mit Messungen", file, measured)
		}
		t.Logf("%s: %d KB Heatmap, %d Abtastungen, %d Granaten, %d Spieler gemessen (%s)", file, len(out.heat)/1024, d.frames,
			len(d.nades), measured, strconv.Itoa(len(out.Kills))+" Kills")
	}
}
