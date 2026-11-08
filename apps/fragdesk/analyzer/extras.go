package main

// Zweiter Durchgang über die Demo, parallel zu cs-demo-analyzer (der eigene Handler nicht zulässt):
//
//   - Heatmap-Daten als eigene Datei ("FDHM", Aufbau in src/core/heat.h): Aufenthaltsorte aller Spieler einmal pro
//     Sekunde (nur während der Runde), Kills mit den Positionen von Schütze und Opfer, Granaten mit Wurf- und
//     Landepunkt.
//   - Crosshair-Placement und Zeit bis zum Treffer: Sobald ein Gegner für einen Spieler sichtbar wird (laut Spiel –
//     dieselbe Info, mit der es das Radar füllt), zählt der Winkel zwischen Blickrichtung und Kopf des Gegners. Trifft
//     der Spieler ihn danach innerhalb von 2 Sekunden, zählt die Zeit bis zu diesem ersten Treffer.
//
// Gemessen wird nur, wenn der Spieler eine Schusswaffe in der Hand hat, nicht geblendet ist und der Gegner beim
// Auftauchen auf seinem Bildschirm lag (sonst misst der Winkel das Umdrehen, nicht die Fadenkreuz-Position).

import (
	"bufio"
	"encoding/binary"
	"errors"
	"math"
	"os"
	"strconv"

	"github.com/akiver/cs-demo-analyzer/pkg/api/constants"
	"github.com/golang/geo/r3"
	dem "github.com/markus-wa/demoinfocs-golang/v4/pkg/demoinfocs"
	"github.com/markus-wa/demoinfocs-golang/v4/pkg/demoinfocs/common"
	"github.com/markus-wa/demoinfocs-golang/v4/pkg/demoinfocs/events"
)

const (
	heatVersion    = 1
	heatSampleSec  = 1.0  // Aufenthaltsorte: Abstand der Abtastungen
	aimFreshSec    = 1.0  // so lange muss ein Gegner unsichtbar gewesen sein, damit sein Auftauchen neu zählt
	aimMaxTTDSec   = 2.0  // spätere Treffer zählen nicht als Reaktion
	aimGoodDeg     = 5.0  // „Fadenkreuz am Kopf“
	aimScreenYaw   = 53.0 // ungefähr der Bildschirmrand bei 16:9 (waagerecht) …
	aimScreenPitch = 37.0 // … und senkrecht
)

// Flags in der Heatmap-Datei
const (
	hkKillerCT = 1 << 0 // Kill: Schütze ist CT
	hkVictimCT = 1 << 1 // Kill: Opfer ist CT
	hkHeadshot = 1 << 2
	hnCT       = 1 << 0 // Granate: Werfer ist CT
	hnNoThrow  = 1 << 1 // Granate: Wurf nicht gesehen (Wurfpunkt = Landepunkt)
	hsCT       = 1 << 0 // Aufenthalt: Spieler ist CT
)

type heatKill struct {
	tick                       int32
	killer, victim, fl, weapon uint8
	killerPos, victimPos       vec3
}

type heatNade struct {
	tick, land        int32
	kind, thrower, fl uint8
	from, to          vec3
}

// Zähler für Crosshair-Placement und Reaktionszeit eines Spielers (wie die übrigen Zähler: gesamt, T, CT, je Waffe)
type aimCounters struct {
	all, t, ct Counters
	weapons    map[string]Counters
}

type aimPair struct {
	vis    bool
	lostAt int  // Tick, an dem der Gegner zuletzt unsichtbar wurde (0 = in dieser Runde noch nicht gesehen)
	start  int  // Tick des gemessenen Auftauchens (0 = keine offene Messung)
	open   bool // wartet auf den ersten Treffer
}

type extrasBuilder struct {
	parser     dem.Parser
	tickrate   float64
	players    []replayPlayer
	playerIdx  map[string]int
	weapons    []string
	weaponIdx  map[string]int
	kills      []heatKill
	nades      []heatNade
	throws     map[common.UniqueID]*heatNade
	frames     []byte
	nFrames    int
	lastSample int
	pairs      map[[2]int]*aimPair
	coach      map[int]bool
	aim        map[uint64]*aimCounters
}

type extrasResult struct {
	heat      []byte // Inhalt der .heat-Datei
	aim       map[uint64]*aimCounters
	replayErr error // nur mit replayTo: warum die 2D-Daten fehlen (nil = geschrieben)
}

// buildExtras liest die (entpackte) Demo und sammelt Heatmap-Daten und die Zähler fürs Zielen. Mit replayTo sammelt
// derselbe Durchgang auch die 2D-Daten und schreibt sie dorthin – ohne die Demo ein drittes Mal zu lesen. Das Ergebnis
// ist nie nil: auch bei einem Fehler (err) steht in replayErr, was aus den 2D-Daten wurde.
func buildExtras(demoPath, replayTo string) (res *extrasResult, err error) {
	res = &extrasResult{}
	fail := func(e error) (*extrasResult, error) {
		res.heat, res.aim = nil, nil
		if replayTo != "" {
			res.replayErr = e
		}
		return res, e
	}
	f, err := os.Open(demoPath)
	if err != nil {
		return fail(coded("READ", "%v", err))
	}
	defer f.Close()
	p := dem.NewParser(bufio.NewReaderSize(f, 1<<20))
	defer p.Close()
	eb := &extrasBuilder{
		parser:    p,
		playerIdx: map[string]int{},
		weaponIdx: map[string]int{"": 0},
		weapons:   []string{""},
		throws:    map[common.UniqueID]*heatNade{},
		pairs:     map[[2]int]*aimPair{},
		coach:     map[int]bool{},
		aim:       map[uint64]*aimCounters{},
	}
	eb.register()
	var rb *replayBuilder
	if replayTo != "" {
		rb = newReplayBuilder(p)
		rb.register()
	}
	defer func() {
		if r := recover(); r != nil {
			res, err = fail(coded("CORRUPT", "Zusatzdaten abgebrochen: %v", r))
		}
	}()
	perr := p.ParseToEnd()
	if rb != nil {
		res.replayErr = rb.finish(perr, replayTo)
	}
	if perr != nil && !errors.Is(perr, dem.ErrUnexpectedEndOfDemo) && eb.nFrames == 0 {
		res.heat, res.aim = nil, nil
		return res, coded("CORRUPT", "%v", perr)
	}
	res.heat, res.aim = eb.encode(p.Header().MapName), eb.aim
	return res, nil
}

func (eb *extrasBuilder) tick() int { return eb.parser.GameState().IngameTick() }

func (eb *extrasBuilder) rate() float64 {
	if eb.tickrate == 0 {
		tr := eb.parser.TickRate()
		if tr <= 0 || math.IsNaN(tr) {
			return 64 // noch unbekannt: nicht merken
		}
		eb.tickrate = tr
	}
	return eb.tickrate
}

func (eb *extrasBuilder) isCoach(p *common.Player) bool {
	c, ok := eb.coach[p.UserID]
	if !ok {
		c = isCoach(p)
		eb.coach[p.UserID] = c
	}
	return c
}

func (eb *extrasBuilder) player(p *common.Player) uint8 {
	if p == nil || eb.isCoach(p) {
		return noPlayer
	}
	key := playerKey(p)
	if i, ok := eb.playerIdx[key]; ok {
		if p.Name != "" {
			eb.players[i].name = p.Name
		}
		return uint8(i)
	}
	if len(eb.players) >= noPlayer {
		return noPlayer
	}
	id := uint64(0)
	if !p.IsBot {
		id = p.SteamID64
	}
	eb.players = append(eb.players, replayPlayer{id, p.Name})
	eb.playerIdx[key] = len(eb.players) - 1
	return uint8(len(eb.players) - 1)
}

func (eb *extrasBuilder) weapon(e *common.Equipment) uint8 {
	if e == nil {
		return 0
	}
	name := e.Type.String()
	if i, ok := eb.weaponIdx[name]; ok {
		return uint8(i)
	}
	if len(eb.weapons) >= 0xFF {
		return 0
	}
	eb.weapons = append(eb.weapons, name)
	eb.weaponIdx[name] = len(eb.weapons) - 1
	return uint8(len(eb.weapons) - 1)
}

func posOf(v r3.Vector) vec3 { return pos3(v.X, v.Y, v.Z) }

func (eb *extrasBuilder) register() {
	p := eb.parser
	p.RegisterEventHandler(func(events.FrameDone) { eb.frame() })
	p.RegisterEventHandler(func(events.RoundStart) { clear(eb.pairs) })

	p.RegisterEventHandler(func(e events.Kill) {
		if e.Victim == nil || eb.isCoach(e.Victim) || eb.parser.GameState().IsWarmupPeriod() {
			return
		}
		k := heatKill{tick: int32(eb.tick()), killer: eb.player(e.Killer), victim: eb.player(e.Victim), weapon: eb.weapon(e.Weapon),
			victimPos: posOf(e.Victim.Position())}
		k.killerPos = k.victimPos
		if e.Killer != nil {
			k.killerPos = posOf(e.Killer.Position())
			if e.Killer.Team == common.TeamCounterTerrorists {
				k.fl |= hkKillerCT
			}
		}
		if e.Victim.Team == common.TeamCounterTerrorists {
			k.fl |= hkVictimCT
		}
		if e.IsHeadshot {
			k.fl |= hkHeadshot
		}
		eb.kills = append(eb.kills, k)
	})

	// Granaten: Wurf merken, beim Platzen (Molotov: beim Aufschlagen) mit Landepunkt ablegen
	p.RegisterEventHandler(func(e events.GrenadeProjectileThrow) {
		pr := e.Projectile
		if pr == nil || pr.WeaponInstance == nil || eb.parser.GameState().IsWarmupPeriod() {
			return
		}
		kind := grenadeKind(pr.WeaponInstance.Type)
		if kind == 0 {
			return
		}
		thrower := pr.Thrower
		if thrower == nil {
			thrower = pr.Owner
		}
		n := &heatNade{tick: int32(eb.tick()), kind: kind, thrower: eb.player(thrower)}
		if thrower != nil {
			n.from = posOf(thrower.Position())
			if thrower.Team == common.TeamCounterTerrorists {
				n.fl |= hnCT
			}
		} else {
			n.from = posOf(pr.Position())
			n.fl |= hnNoThrow
		}
		eb.throws[pr.UniqueID()] = n
	})
	land := func(e events.GrenadeEvent) {
		if eb.parser.GameState().IsWarmupPeriod() {
			return
		}
		var n *heatNade
		if e.Projectile != nil {
			n = eb.throws[e.Projectile.UniqueID()]
			delete(eb.throws, e.Projectile.UniqueID())
		}
		if n == nil { // Wurf nicht gesehen
			kind := grenadeKind(e.GrenadeType)
			if kind == 0 {
				return
			}
			n = &heatNade{tick: int32(eb.tick()), kind: kind, thrower: eb.player(e.Thrower), fl: hnNoThrow, from: posOf(e.Position)}
			if e.Thrower != nil && e.Thrower.Team == common.TeamCounterTerrorists {
				n.fl |= hnCT
			}
		}
		n.land = int32(eb.tick())
		n.to = posOf(e.Position)
		eb.addNade(*n)
	}
	p.RegisterEventHandler(func(e events.SmokeStart) { land(e.GrenadeEvent) })
	p.RegisterEventHandler(func(e events.FlashExplode) { land(e.GrenadeEvent) })
	p.RegisterEventHandler(func(e events.HeExplode) { land(e.GrenadeEvent) })
	p.RegisterEventHandler(func(e events.DecoyStart) { land(e.GrenadeEvent) })
	p.RegisterEventHandler(func(e events.GrenadeProjectileDestroy) {
		pr := e.Projectile
		if pr == nil {
			return
		}
		n := eb.throws[pr.UniqueID()]
		delete(eb.throws, pr.UniqueID())
		if n == nil || (n.kind != grMolotov && n.kind != grIncendiary) {
			return // andere Granaten sind schon gelandet (oder wurden mit der Runde entfernt)
		}
		n.land = int32(eb.tick())
		n.to = posOf(pr.Position())
		eb.addNade(*n)
	})

	// erster Treffer nach dem Auftauchen
	p.RegisterEventHandler(func(e events.PlayerHurt) {
		a, b := e.Attacker, e.Player
		if a == nil || b == nil || a.Team == b.Team || e.Weapon == nil || !isGun(e.Weapon) {
			return
		}
		st := eb.pairs[[2]int{a.UserID, b.UserID}]
		if st == nil || !st.open {
			return
		}
		st.open = false
		dt := eb.tick() - st.start
		if dt < 0 || float64(dt) > aimMaxTTDSec*eb.rate() {
			return
		}
		ms := float64(dt) / eb.rate() * 1000
		eb.count(a, e.Weapon, func(c Counters, class string) {
			c["ttdN"]++
			c["ttdSum"] += ms
			if class != "" {
				c["ttdN"+class]++
				c["ttdSum"+class] += ms
			}
		})
	})
}

// addNade legt eine gelandete Granate ab. Manche Ereignisse kommen doppelt (Flashes: einmal mit, einmal ohne Projektil):
// gleiche Art, höchstens 8 Ticks und 32 Einheiten auseinander ist dieselbe Granate – die mit bekanntem Wurf gewinnt.
func (eb *extrasBuilder) addNade(n heatNade) {
	for i := len(eb.nades) - 1; i >= 0 && i >= len(eb.nades)-16; i-- {
		o := &eb.nades[i]
		if o.kind != n.kind || abs32(o.land-n.land) > 8 || math.Hypot(float64(o.to.x-n.to.x), float64(o.to.y-n.to.y)) > 32 {
			continue
		}
		if o.fl&hnNoThrow != 0 && n.fl&hnNoThrow == 0 {
			*o = n
		}
		return
	}
	eb.nades = append(eb.nades, n)
}

func abs32(v int32) int32 {
	if v < 0 {
		return -v
	}
	return v
}

// Schusswaffe (keine Granate, kein Messer, keine Bombe, kein Zeus)?
func isGun(e *common.Equipment) bool {
	return isBulletWeapon(weaponTypeOf(constants.WeaponName(e.Type.String())))
}

// Waffenart für eigene Zähler (leer = keine eigene Art)
func aimClass(e *common.Equipment) string {
	switch weaponTypeOf(constants.WeaponName(e.Type.String())) {
	case constants.WeaponTypeRifle:
		return "Rifle"
	case constants.WeaponTypeSniper:
		return "Sniper"
	case constants.WeaponTypePistol:
		return "Pistol"
	case constants.WeaponTypeSMG:
		return "Smg"
	}
	return ""
}

// Zähler des Spielers a fortschreiben: gesamt, seine Seite und die Waffe (nur Menschen)
func (eb *extrasBuilder) count(a *common.Player, w *common.Equipment, add func(c Counters, class string)) {
	if a.IsBot || a.SteamID64 == 0 || eb.isCoach(a) {
		return
	}
	ac := eb.aim[a.SteamID64]
	if ac == nil {
		ac = &aimCounters{all: Counters{}, t: Counters{}, ct: Counters{}, weapons: map[string]Counters{}}
		eb.aim[a.SteamID64] = ac
	}
	class := aimClass(w)
	add(ac.all, class)
	switch a.Team {
	case common.TeamTerrorists:
		add(ac.t, class)
	case common.TeamCounterTerrorists:
		add(ac.ct, class)
	}
	name := w.Type.String()
	wc := ac.weapons[name]
	if wc == nil {
		wc = Counters{}
		ac.weapons[name] = wc
	}
	add(wc, "")
}

// Bits der Spieler, für die p gerade sichtbar ist (Bit = Entity-ID des Controllers − 1)
func spottedMask(p *common.Player) uint64 {
	pawn := p.PlayerPawnEntity()
	if pawn == nil {
		return 0
	}
	part := func(name string) uint64 {
		v, ok := pawn.PropertyValue(name)
		if !ok || v.Any == nil {
			return 0
		}
		switch x := v.Any.(type) {
		case uint64:
			return x & 0xFFFFFFFF
		case uint32:
			return uint64(x)
		case int64:
			return uint64(x) & 0xFFFFFFFF
		case int32:
			return uint64(uint32(x))
		}
		return 0
	}
	return part("m_entitySpottedState.m_bSpottedByMask.0000") | part("m_entitySpottedState.m_bSpottedByMask.0001")<<32
}

func wrap180(d float64) float64 {
	d = math.Mod(d+180, 360)
	if d < 0 {
		d += 360
	}
	return d - 180
}

// Abweichung des Fadenkreuzes von a zum Kopf von b: gesamt, waagerecht, senkrecht (Grad)
func aimError(a, b *common.Player) (total, yaw, pitch float64) {
	return aimAngles(a.PositionEyes(), b.PositionEyes(), float64(a.ViewDirectionX()), float64(a.ViewDirectionY()))
}

// aimAngles: Winkel zwischen der Blickrichtung (Yaw, Pitch in Grad wie im Spiel: Pitch positiv = nach unten) von eye
// aus und der Richtung zu head – gesamt sowie waagerechter und senkrechter Anteil (Beträge)
func aimAngles(eye, head r3.Vector, yawA, pitchA float64) (total, yaw, pitch float64) {
	d := head.Sub(eye)
	dist := math.Hypot(d.X, d.Y)
	if dist < 1 && math.Abs(d.Z) < 1 {
		return 0, 0, 0
	}
	yawT := math.Atan2(d.Y, d.X) * 180 / math.Pi
	pitchT := -math.Atan2(d.Z, dist) * 180 / math.Pi
	if pitchA > 180 {
		pitchA -= 360
	}
	yaw = wrap180(yawA - yawT)
	pitch = pitchA - pitchT
	dir := func(yw, pt float64) r3.Vector {
		y, p := yw*math.Pi/180, pt*math.Pi/180
		return r3.Vector{X: math.Cos(p) * math.Cos(y), Y: math.Cos(p) * math.Sin(y), Z: -math.Sin(p)}
	}
	dot := dir(yawA, pitchA).Dot(dir(yawT, pitchT))
	total = math.Acos(math.Max(-1, math.Min(1, dot))) * 180 / math.Pi
	return total, math.Abs(yaw), math.Abs(pitch)
}

func (eb *extrasBuilder) frame() {
	gs := eb.parser.GameState()
	tick := gs.IngameTick()
	if tick <= 0 || gs.IsWarmupPeriod() || gs.IsFreezetimePeriod() {
		return
	}
	var alive []*common.Player
	for _, pl := range gs.Participants().Playing() {
		if pl == nil || !pl.IsAlive() || (pl.Team != common.TeamTerrorists && pl.Team != common.TeamCounterTerrorists) || eb.isCoach(pl) {
			continue
		}
		alive = append(alive, pl)
	}

	// Aufenthaltsorte, einmal pro Sekunde
	tr := eb.rate()
	if eb.lastSample == 0 || tick < eb.lastSample || float64(tick-eb.lastSample) >= heatSampleSec*tr {
		eb.lastSample = tick
		n := 0
		var rec []byte
		for _, pl := range alive {
			idx := eb.player(pl)
			if idx == noPlayer || n == 0xFF {
				continue
			}
			fl := uint8(0)
			if pl.Team == common.TeamCounterTerrorists {
				fl |= hsCT
			}
			rec = append(rec, idx, fl)
			rec = appendVec(rec, posOf(pl.Position()))
			n++
		}
		if n > 0 {
			eb.frames = binary.LittleEndian.AppendUint32(eb.frames, uint32(int32(tick)))
			eb.frames = append(eb.frames, uint8(n))
			eb.frames = append(eb.frames, rec...)
			eb.nFrames++
		}
	}

	// Sichtbarkeit: wer sieht wen gerade neu?
	fresh := int(aimFreshSec * tr)
	for _, b := range alive {
		mask := spottedMask(b)
		for _, a := range alive {
			if a.Team == b.Team {
				continue
			}
			bit := a.EntityID - 1
			vis := bit >= 0 && bit < 64 && mask&(1<<uint(bit)) != 0
			key := [2]int{a.UserID, b.UserID}
			st := eb.pairs[key]
			if st == nil {
				st = &aimPair{}
				eb.pairs[key] = st
			}
			if vis == st.vis {
				continue
			}
			st.vis = vis
			if !vis {
				st.lostAt = tick
				continue
			}
			if st.lostAt != 0 && tick-st.lostAt < fresh {
				continue // nur kurz verdeckt: dieselbe Begegnung
			}
			st.open = false
			w := a.ActiveWeapon()
			if w == nil || !isGun(w) || a.FlashDurationTimeRemaining() > 0 || a.IsDefusing || a.IsPlanting {
				continue
			}
			total, yaw, pitch := aimError(a, b)
			if yaw > aimScreenYaw || pitch > aimScreenPitch {
				continue // tauchte nicht auf dem Bildschirm auf
			}
			st.start, st.open = tick, true
			eb.count(a, w, func(c Counters, class string) {
				c["cpN"]++
				c["cpSum"] += total
				c["cpYaw"] += yaw
				c["cpPitch"] += pitch
				if total <= aimGoodDeg {
					c["cpGood"]++
				}
				if class != "" {
					c["cpN"+class]++
					c["cpSum"+class] += total
				}
			})
		}
	}
}

// encode: Aufbau siehe src/core/heat.h
func (eb *extrasBuilder) encode(mapName string) []byte {
	b := make([]byte, 0, 64+len(eb.frames)+len(eb.kills)*20+len(eb.nades)*23)
	b = append(b, "FDHM"...)
	b = binary.LittleEndian.AppendUint16(b, heatVersion)
	b = binary.LittleEndian.AppendUint16(b, 0)
	b = binary.LittleEndian.AppendUint32(b, math.Float32bits(float32(eb.rate())))
	b = appendStr(b, mapName)
	b = binary.LittleEndian.AppendUint16(b, uint16(len(eb.players)))
	for _, p := range eb.players {
		b = binary.LittleEndian.AppendUint64(b, p.steamID)
		b = appendStr(b, p.name)
	}
	b = binary.LittleEndian.AppendUint16(b, uint16(len(eb.weapons)))
	for _, w := range eb.weapons {
		b = appendStr(b, w)
	}
	b = binary.LittleEndian.AppendUint32(b, uint32(len(eb.kills)))
	for _, k := range eb.kills {
		b = binary.LittleEndian.AppendUint32(b, uint32(k.tick))
		b = append(b, k.killer, k.victim, k.fl, k.weapon)
		b = appendVec(b, k.killerPos)
		b = appendVec(b, k.victimPos)
	}
	b = binary.LittleEndian.AppendUint32(b, uint32(len(eb.nades)))
	for _, n := range eb.nades {
		b = binary.LittleEndian.AppendUint32(b, uint32(n.tick))
		b = binary.LittleEndian.AppendUint32(b, uint32(n.land))
		b = append(b, n.kind, n.thrower, n.fl)
		b = appendVec(b, n.from)
		b = appendVec(b, n.to)
	}
	b = binary.LittleEndian.AppendUint32(b, uint32(eb.nFrames))
	b = binary.LittleEndian.AppendUint32(b, uint32(len(eb.frames)))
	b = append(b, eb.frames...)
	return append(b, "END!"...)
}

// Zusatzdaten in die Ausgabe übernehmen: Zähler zu den Spielern (gesamt, T, CT, Waffen)
func mergeAim(out *Output, aim map[uint64]*aimCounters) {
	for _, po := range out.Players {
		id, err := strconv.ParseUint(po.SteamID, 10, 64)
		if err != nil {
			continue
		}
		ac := aim[id]
		if ac == nil {
			continue
		}
		addInto := func(dst *Counters, src Counters) {
			if *dst == nil {
				*dst = Counters{}
			}
			for k, v := range src {
				(*dst)[k] += math.Round(v*1000) / 1000
			}
		}
		addInto(&po.Stats, ac.all)
		addInto(&po.StatsT, ac.t)
		addInto(&po.StatsCT, ac.ct)
		for name, c := range ac.weapons {
			found := false
			for i := range po.Weapons {
				if po.Weapons[i].Name == name {
					addInto(&po.Weapons[i].Stats, c)
					found = true
					break
				}
			}
			if !found {
				wo := WeaponOut{Name: name, Type: string(weaponTypeOf(constants.WeaponName(name))), Stats: Counters{}}
				addInto(&wo.Stats, c)
				po.Weapons = append(po.Weapons, wo)
			}
		}
	}
}

// writeHeat schreibt die Heatmap-Daten atomar (erst .part, dann umbenennen)
func writeHeat(target string, b []byte) error {
	tmp := target + ".part"
	if err := os.WriteFile(tmp, b, 0o644); err != nil {
		os.Remove(tmp)
		return err
	}
	if err := os.Rename(tmp, target); err != nil {
		os.Remove(tmp)
		return err
	}
	return nil
}
