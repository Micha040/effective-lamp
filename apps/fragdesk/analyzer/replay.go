package main

// Daten für die 2D-Ansicht einer Runde: Positionen, Blickrichtung, HP und Waffe aller Spieler rund 8-mal pro Sekunde,
// dazu Flugbahnen und Wirkung der Granaten, Kills, Schüsse und die Bombe. Eigenes kompaktes Binärformat ("FDRP"),
// das FragDesk bei Bedarf erzeugt (rund 1–2 MB je Match): allein (-replay) oder beim Auswerten (-out … -replay …) im
// selben Durchgang wie Heatmap und Zielen (extras.go), dann ohne die Demo ein weiteres Mal zu lesen. Der Aufbau ist in
// src/core/replay.h beschrieben; Konstanten und Reihenfolge müssen dazu passen.

import (
	"bufio"
	"encoding/binary"
	"errors"
	"math"
	"os"
	"path/filepath"
	"slices"
	"strconv"
	"time"

	dem "github.com/markus-wa/demoinfocs-golang/v4/pkg/demoinfocs"
	"github.com/markus-wa/demoinfocs-golang/v4/pkg/demoinfocs/common"
	"github.com/markus-wa/demoinfocs-golang/v4/pkg/demoinfocs/events"
)

const (
	replayVersion  = 1
	replaySampleHz = 8
)

// Spieler-Flags je Abtastung
const (
	pfCT       = 1 << 0
	pfBomb     = 1 << 1
	pfDefusing = 1 << 2
	pfPlanting = 1 << 3
	pfDucking  = 1 << 4
	pfScoped   = 1 << 5
	pfFlashed  = 1 << 6
	pfKit      = 1 << 7
)

// Granatenarten (Flugbahnen und Wirkung)
const (
	grSmoke      = 1
	grFlash      = 2
	grHE         = 3
	grMolotov    = 4
	grIncendiary = 5
	grDecoy      = 6
)

// Wirkung einer Granate
const (
	fxSmoke = 1
	fxFire  = 2
	fxFlash = 3
	fxHE    = 4
	fxDecoy = 5
)

// Kill-Flags
const (
	kfHeadshot = 1 << 0
	kfWallbang = 1 << 1
	kfNoScope  = 1 << 2
	kfSmoke    = 1 << 3
	kfBlind    = 1 << 4
)

// Bombe
const (
	bombPlanted  = 1
	bombDefused  = 2
	bombExploded = 3
)

const noPlayer = 0xFF

type vec3 struct{ x, y, z int16 }

type replayPlayer struct {
	steamID uint64
	name    string
}

type grenadeTrack struct {
	kind    uint8
	thrower uint8
	ct      bool
	points  []trackPoint
}

type trackPoint struct {
	tick int32
	pos  vec3
}

type replayEffect struct {
	kind       uint8
	thrower    uint8
	ct         bool
	start, end int32
	pos        vec3
	hull       [][2]int16
	hullArea   float64
}

type replayKill struct {
	tick                                 int32
	killer, victim, assister, weapon, fl uint8
	killerPos, victimPos                 vec3
}

type replayShot struct {
	tick   int32
	player uint8
	weapon uint8
}

type replayBomb struct {
	tick   int32
	kind   uint8
	player uint8
	pos    vec3
}

type replayBuilder struct {
	parser     dem.Parser
	interval   int
	lastSample int
	players    []replayPlayer
	playerIdx  map[string]int
	weapons    []string
	weaponIdx  map[string]int
	frames     []byte
	nFrames    int
	tracks     map[common.UniqueID]*grenadeTrack
	trackList  []*grenadeTrack
	effects    []*replayEffect
	open       map[int]*replayEffect             // Smoke/Decoy nach Entity-ID
	fires      map[common.UniqueID]*replayEffect // Molotovs nach Inferno
	kills      []replayKill
	shots      []replayShot
	bomb       []replayBomb
	planted    bool
	plantPos   vec3
	broken     error // Fehler in einem der eigenen Handler: ab dann wird nichts mehr gesammelt
	projIDs    []int // Zwischenspeicher für frame()
}

func newReplayBuilder(p dem.Parser) *replayBuilder {
	return &replayBuilder{
		parser:    p,
		playerIdx: map[string]int{},
		weaponIdx: map[string]int{"": 0},
		weapons:   []string{""},
		tracks:    map[common.UniqueID]*grenadeTrack{},
		open:      map[int]*replayEffect{},
		fires:     map[common.UniqueID]*replayEffect{},
	}
}

// on: Handler, der eine Panik der 2D-Daten abfängt. Im gemeinsamen Durchgang mit Heatmap und Zielen kostet ein Fehler
// hier so nur die 2D-Daten, nicht den ganzen Durchgang.
func on[E any](rb *replayBuilder, fn func(E)) func(E) {
	return func(e E) {
		if rb.broken != nil {
			return
		}
		defer func() {
			if r := recover(); r != nil {
				rb.broken = coded("CORRUPT", "2D-Daten abgebrochen: %v", r)
			}
		}()
		fn(e)
	}
}

// buildReplay liest die Demo einmal komplett und schreibt die Rundendaten nach target.
func buildReplay(demoPath, target string) (err error) {
	f, err := os.Open(demoPath)
	if err != nil {
		return coded("READ", "%v", err)
	}
	defer f.Close()

	p := dem.NewParser(bufio.NewReaderSize(f, 1<<20))
	defer p.Close()
	rb := newReplayBuilder(p)
	rb.register()

	defer func() {
		if r := recover(); r != nil {
			err = coded("CORRUPT", "Auswertung abgebrochen: %v", r)
		}
	}()
	return rb.finish(p.ParseToEnd(), target)
}

// finish: nach dem Lesen der Demo (perr: Ergebnis von ParseToEnd) die Datei schreiben – oder sagen, warum nicht
func (rb *replayBuilder) finish(perr error, target string) error {
	if rb.broken != nil {
		return rb.broken
	}
	if perr != nil && !errors.Is(perr, dem.ErrUnexpectedEndOfDemo) && rb.nFrames == 0 {
		return coded("CORRUPT", "%v", perr)
	}
	// abgeschnittene Demo: alles bis dahin ist trotzdem brauchbar
	if rb.nFrames == 0 {
		return coded("EMPTY", "keine Spielerpositionen gefunden")
	}
	return rb.write(target)
}

func (rb *replayBuilder) tick() int { return rb.parser.GameState().IngameTick() }

func clamp16(v float64) int16 {
	if v > math.MaxInt16 {
		return math.MaxInt16
	}
	if v < math.MinInt16 {
		return math.MinInt16
	}
	return int16(math.Round(v))
}

func pos3(x, y, z float64) vec3 { return vec3{clamp16(x), clamp16(y), clamp16(z)} }

func playerKey(p *common.Player) string {
	if p.SteamID64 != 0 && !p.IsBot {
		return "s" + strconv.FormatUint(p.SteamID64, 10)
	}
	return "b" + p.Name
}

// Trainer erkennen wie cs-demo-analyzer: Manche Anbieter (z. B. PRACC, ESEA) führen sie als Spieler im Team, die bis
// zum Ende der Kaufzeit „leben“ – auf der Karte und in der Spielerliste haben sie nichts zu suchen.
func isCoach(p *common.Player) bool {
	if p == nil || p.Entity == nil {
		return false
	}
	intProp := func(name string) (int, bool) {
		v, ok := p.Entity.PropertyValue(name)
		if !ok {
			return 0, false
		}
		if !v.S2 {
			return v.IntVal, true
		}
		switch x := v.Any.(type) {
		case int32:
			return int(x), true
		case uint32:
			return int(x), true
		case int64:
			return int(x), true
		case uint64:
			return int(x), true
		}
		return 0, false
	}
	if color, ok := intProp("m_iCompTeammateColor"); ok && color > 4 {
		return true
	}
	if team, ok := intProp("m_iCoachingTeam"); ok {
		t := common.Team(team)
		return t == common.TeamTerrorists || t == common.TeamCounterTerrorists
	}
	return false
}

// Index des Spielers (höchstens 254 verschiedene; danach "unbekannt")
func (rb *replayBuilder) player(p *common.Player) uint8 {
	if p == nil || isCoach(p) {
		return noPlayer
	}
	key := playerKey(p)
	if i, ok := rb.playerIdx[key]; ok {
		if p.Name != "" {
			rb.players[i].name = p.Name // zuletzt bekannter Name
		}
		return uint8(i)
	}
	if len(rb.players) >= noPlayer {
		return noPlayer
	}
	id := uint64(0)
	if !p.IsBot {
		id = p.SteamID64
	}
	rb.players = append(rb.players, replayPlayer{id, p.Name})
	rb.playerIdx[key] = len(rb.players) - 1
	return uint8(len(rb.players) - 1)
}

func (rb *replayBuilder) weapon(e *common.Equipment) uint8 {
	if e == nil {
		return 0
	}
	name := e.String()
	if i, ok := rb.weaponIdx[name]; ok {
		return uint8(i)
	}
	if len(rb.weapons) >= 255 {
		return 0
	}
	rb.weapons = append(rb.weapons, name)
	rb.weaponIdx[name] = len(rb.weapons) - 1
	return uint8(len(rb.weapons) - 1)
}

func grenadeKind(t common.EquipmentType) uint8 {
	switch t {
	case common.EqSmoke:
		return grSmoke
	case common.EqFlash:
		return grFlash
	case common.EqHE:
		return grHE
	case common.EqMolotov:
		return grMolotov
	case common.EqIncendiary:
		return grIncendiary
	case common.EqDecoy:
		return grDecoy
	}
	return 0
}

func (rb *replayBuilder) register() {
	p := rb.parser
	// alle Handler über on(): eine Panik kostet nur die 2D-Daten (siehe on)
	p.RegisterEventHandler(on(rb, func(events.FrameDone) { rb.frame() }))

	p.RegisterEventHandler(on(rb, func(e events.SmokeStart) { rb.effectStart(fxSmoke, e.GrenadeEvent) }))
	p.RegisterEventHandler(on(rb, func(e events.SmokeExpired) { rb.effectEnd(e.GrenadeEntityID) }))
	p.RegisterEventHandler(on(rb, func(e events.DecoyStart) { rb.effectStart(fxDecoy, e.GrenadeEvent) }))
	p.RegisterEventHandler(on(rb, func(e events.DecoyExpired) { rb.effectEnd(e.GrenadeEntityID) }))
	p.RegisterEventHandler(on(rb, func(e events.FlashExplode) { rb.effectStart(fxFlash, e.GrenadeEvent) }))
	p.RegisterEventHandler(on(rb, func(e events.HeExplode) { rb.effectStart(fxHE, e.GrenadeEvent) }))
	p.RegisterEventHandler(on(rb, func(e events.InfernoStart) {
		if e.Inferno == nil {
			return
		}
		pos := e.Inferno.Entity.Position()
		fx := &replayEffect{kind: fxFire, thrower: rb.player(e.Inferno.Thrower()), start: int32(rb.tick()), pos: pos3(pos.X, pos.Y, pos.Z)}
		if t := e.Inferno.Thrower(); t != nil {
			fx.ct = t.Team == common.TeamCounterTerrorists
		}
		rb.effects = append(rb.effects, fx)
		rb.fires[e.Inferno.UniqueID()] = fx
	}))
	p.RegisterEventHandler(on(rb, func(e events.InfernoExpired) {
		if e.Inferno == nil {
			return
		}
		if fx := rb.fires[e.Inferno.UniqueID()]; fx != nil {
			fx.end = int32(rb.tick())
			delete(rb.fires, e.Inferno.UniqueID())
		}
	}))

	p.RegisterEventHandler(on(rb, func(e events.Kill) {
		if e.Victim == nil || isCoach(e.Victim) { // Trainer „sterben“ bei manchen Anbietern nach der Kaufzeit
			return
		}
		k := replayKill{tick: int32(rb.tick()), killer: rb.player(e.Killer), victim: rb.player(e.Victim), assister: rb.player(e.Assister),
			weapon: rb.weapon(e.Weapon)}
		if e.IsHeadshot {
			k.fl |= kfHeadshot
		}
		if e.PenetratedObjects > 0 {
			k.fl |= kfWallbang
		}
		if e.NoScope {
			k.fl |= kfNoScope
		}
		if e.ThroughSmoke {
			k.fl |= kfSmoke
		}
		if e.AttackerBlind {
			k.fl |= kfBlind
		}
		v := e.Victim.Position()
		k.victimPos = pos3(v.X, v.Y, v.Z)
		if e.Killer != nil {
			kp := e.Killer.Position()
			k.killerPos = pos3(kp.X, kp.Y, kp.Z)
		} else {
			k.killerPos = k.victimPos
		}
		rb.kills = append(rb.kills, k)
	}))

	p.RegisterEventHandler(on(rb, func(e events.WeaponFire) {
		if e.Shooter == nil || e.Weapon == nil || isCoach(e.Shooter) {
			return
		}
		switch e.Weapon.Class() {
		case common.EqClassGrenade, common.EqClassEquipment:
			return
		}
		if e.Weapon.Type == common.EqKnife || e.Weapon.Type == common.EqBomb {
			return
		}
		rb.shots = append(rb.shots, replayShot{int32(rb.tick()), rb.player(e.Shooter), rb.weapon(e.Weapon)})
	}))

	p.RegisterEventHandler(on(rb, func(e events.BombPlanted) {
		pos := rb.parser.GameState().Bomb().Position()
		if e.Player != nil {
			pos = e.Player.Position()
		}
		rb.planted = true
		rb.plantPos = pos3(pos.X, pos.Y, pos.Z)
		rb.bomb = append(rb.bomb, replayBomb{int32(rb.tick()), bombPlanted, rb.player(e.Player), rb.plantPos})
	}))
	p.RegisterEventHandler(on(rb, func(e events.BombDefused) {
		rb.bomb = append(rb.bomb, replayBomb{int32(rb.tick()), bombDefused, rb.player(e.Player), rb.plantPos})
		rb.planted = false
	}))
	p.RegisterEventHandler(on(rb, func(e events.BombExplode) {
		rb.bomb = append(rb.bomb, replayBomb{int32(rb.tick()), bombExploded, noPlayer, rb.plantPos})
		rb.planted = false
	}))
	p.RegisterEventHandler(on(rb, func(events.RoundStart) {
		rb.planted = false
		// Granaten der alten Runde sind weg
		for _, fx := range rb.open {
			if fx.end == 0 {
				fx.end = int32(rb.tick())
			}
		}
		rb.open = map[int]*replayEffect{}
		for _, fx := range rb.fires {
			if fx.end == 0 {
				fx.end = int32(rb.tick())
			}
		}
		rb.fires = map[common.UniqueID]*replayEffect{}
	}))
}

func (rb *replayBuilder) effectStart(kind uint8, e events.GrenadeEvent) {
	fx := &replayEffect{kind: kind, thrower: rb.player(e.Thrower), start: int32(rb.tick()), pos: pos3(e.Position.X, e.Position.Y, e.Position.Z)}
	// Manche Ereignisse kommen doppelt (Spielereignis und nachgebildetes Source-1-Ereignis): gleiche Art, gleicher Ort, kurz danach
	for i := len(rb.effects) - 1; i >= 0 && i >= len(rb.effects)-12; i-- {
		o := rb.effects[i]
		if o.kind == kind && fx.start-o.start <= 8 && abs16(o.pos.x-fx.pos.x) < 32 && abs16(o.pos.y-fx.pos.y) < 32 {
			return
		}
	}
	if e.Thrower != nil {
		fx.ct = e.Thrower.Team == common.TeamCounterTerrorists
	}
	if kind == fxFlash || kind == fxHE {
		fx.end = fx.start // Augenblick
	} else {
		rb.open[e.GrenadeEntityID] = fx
	}
	rb.effects = append(rb.effects, fx)
}

func (rb *replayBuilder) effectEnd(entityID int) {
	if fx := rb.open[entityID]; fx != nil {
		fx.end = int32(rb.tick())
		delete(rb.open, entityID)
	}
}

func hullArea(pts [][2]int16) float64 {
	a := 0.0
	for i := range pts {
		j := (i + 1) % len(pts)
		a += float64(pts[i][0])*float64(pts[j][1]) - float64(pts[j][0])*float64(pts[i][1])
	}
	return math.Abs(a) / 2
}

// Eine Abtastung: alle lebenden Spieler, fliegende Granaten, Feuerflächen, liegende Bombe
func (rb *replayBuilder) frame() {
	gs := rb.parser.GameState()
	tick := gs.IngameTick()
	if tick <= 0 || gs.IsWarmupPeriod() {
		return
	}
	if rb.interval == 0 {
		tr := rb.parser.TickRate()
		if tr <= 0 || math.IsNaN(tr) {
			tr = 64
		}
		rb.interval = max(1, int(math.Round(tr/replaySampleHz)))
	}
	if rb.lastSample != 0 && tick-rb.lastSample < rb.interval && tick >= rb.lastSample {
		return
	}
	rb.lastSample = tick

	var carrier *common.Player
	bomb := gs.Bomb()
	if bomb != nil {
		carrier = bomb.Carrier
	}

	type sample struct {
		idx                uint8
		pos                vec3
		yaw, hp, wep, flag uint8
	}
	var list []sample
	for _, pl := range gs.Participants().Playing() {
		if pl == nil || !pl.IsAlive() || (pl.Team != common.TeamTerrorists && pl.Team != common.TeamCounterTerrorists) || isCoach(pl) {
			continue
		}
		idx := rb.player(pl)
		if idx == noPlayer {
			continue
		}
		p := pl.Position()
		yaw := float64(pl.ViewDirectionX())
		s := sample{idx: idx, pos: pos3(p.X, p.Y, p.Z), yaw: uint8(int(math.Round(math.Mod(yaw+360, 360)/360*256)) & 0xFF),
			hp: uint8(min(255, max(0, pl.Health()))), wep: rb.weapon(pl.ActiveWeapon())}
		if pl.Team == common.TeamCounterTerrorists {
			s.flag |= pfCT
		}
		if carrier == pl {
			s.flag |= pfBomb
		}
		if pl.IsDefusing {
			s.flag |= pfDefusing
		}
		if pl.IsPlanting {
			s.flag |= pfPlanting
		}
		if pl.IsDucking() {
			s.flag |= pfDucking
		}
		if pl.IsScoped() {
			s.flag |= pfScoped
		}
		if pl.FlashDurationTimeRemaining() > 300*time.Millisecond {
			s.flag |= pfFlashed
		}
		if pl.HasDefuseKit() {
			s.flag |= pfKit
		}
		list = append(list, s)
	}
	if len(list) == 0 {
		return
	}

	// Bombe: 0 = getragen/unbekannt, 1 = liegt, 2 = gelegt
	bombState := uint8(0)
	var bp vec3
	if rb.planted {
		bombState, bp = 2, rb.plantPos
	} else if bomb != nil && carrier == nil {
		pos := bomb.Position()
		if pos.X != 0 || pos.Y != 0 || pos.Z != 0 {
			bombState, bp = 1, pos3(pos.X, pos.Y, pos.Z)
		}
	}

	b := rb.frames
	b = binary.LittleEndian.AppendUint32(b, uint32(int32(tick)))
	b = append(b, bombState)
	if bombState != 0 {
		b = appendVec(b, bp)
	}
	b = append(b, uint8(len(list)))
	for _, s := range list {
		b = append(b, s.idx)
		b = appendVec(b, s.pos)
		b = append(b, s.yaw, s.hp, s.wep, s.flag)
	}
	rb.frames = b
	rb.nFrames++

	// Granaten im Flug – nach Entity-ID, damit die Reihenfolge der Flugbahnen (und so die Datei) bei jedem Lauf gleich ist
	projs := gs.GrenadeProjectiles()
	rb.projIDs = rb.projIDs[:0]
	for id := range projs {
		rb.projIDs = append(rb.projIDs, id)
	}
	slices.Sort(rb.projIDs)
	for _, id := range rb.projIDs {
		proj := projs[id]
		if proj == nil || proj.WeaponInstance == nil {
			continue
		}
		kind := grenadeKind(proj.WeaponInstance.Type)
		if kind == 0 {
			continue
		}
		tr := rb.tracks[proj.UniqueID()]
		if tr == nil {
			thrower := proj.Thrower
			if thrower == nil {
				thrower = proj.Owner
			}
			tr = &grenadeTrack{kind: kind, thrower: rb.player(thrower)}
			if thrower != nil {
				tr.ct = thrower.Team == common.TeamCounterTerrorists
			}
			rb.tracks[proj.UniqueID()] = tr
			rb.trackList = append(rb.trackList, tr)
		}
		p := proj.Position()
		pt := trackPoint{int32(tick), pos3(p.X, p.Y, p.Z)}
		// liegende Granaten (Smoke, Decoy) nicht weiter aufzeichnen: nur Punkte, an denen sie sich bewegt hat
		if n := len(tr.points); n > 0 {
			last := tr.points[n-1].pos
			if abs16(last.x-pt.pos.x) < 2 && abs16(last.y-pt.pos.y) < 2 && abs16(last.z-pt.pos.z) < 2 {
				continue
			}
		}
		if len(tr.points) < 400 {
			tr.points = append(tr.points, pt)
		}
	}

	// Feuerfläche: die größte Ausdehnung merken
	for _, inf := range gs.Infernos() {
		fx := rb.fires[inf.UniqueID()]
		if fx == nil {
			continue
		}
		hull := inf.Fires().Active().ConvexHull2D()
		if len(hull) < 3 {
			continue
		}
		pts := make([][2]int16, 0, len(hull))
		for _, h := range hull {
			pts = append(pts, [2]int16{clamp16(h.X), clamp16(h.Y)})
		}
		if a := hullArea(pts); a > fx.hullArea && len(pts) <= 255 {
			fx.hull, fx.hullArea = pts, a
		}
	}
}

func abs16(v int16) int16 {
	if v < 0 {
		return -v
	}
	return v
}

func appendVec(b []byte, v vec3) []byte {
	b = binary.LittleEndian.AppendUint16(b, uint16(v.x))
	b = binary.LittleEndian.AppendUint16(b, uint16(v.y))
	return binary.LittleEndian.AppendUint16(b, uint16(v.z))
}

func appendStr(b []byte, s string) []byte {
	if len(s) > 0xFFFF {
		s = s[:0xFFFF]
	}
	b = binary.LittleEndian.AppendUint16(b, uint16(len(s)))
	return append(b, s...)
}

func (rb *replayBuilder) write(target string) error {
	tr := rb.parser.TickRate()
	if tr <= 0 || math.IsNaN(tr) {
		tr = 64
	}
	b := rb.encode(tr, rb.parser.Header().MapName)
	if dir := filepath.Dir(target); dir != "" {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			return coded("WRITE", "%v", err)
		}
	}
	tmp := target + ".part"
	if err := os.WriteFile(tmp, b, 0o644); err != nil {
		os.Remove(tmp)
		return coded("WRITE", "%v", err)
	}
	if err := os.Rename(tmp, target); err != nil {
		os.Remove(tmp)
		return coded("WRITE", "%v", err)
	}
	return nil
}

// encode: Aufbau siehe src/core/replay.h
func (rb *replayBuilder) encode(tickrate float64, mapName string) []byte {
	var b []byte
	b = append(b, "FDRP"...)
	b = binary.LittleEndian.AppendUint16(b, replayVersion)
	b = binary.LittleEndian.AppendUint16(b, 0)
	b = binary.LittleEndian.AppendUint32(b, math.Float32bits(float32(tickrate)))
	b = binary.LittleEndian.AppendUint16(b, uint16(rb.interval))
	b = appendStr(b, mapName)

	b = binary.LittleEndian.AppendUint16(b, uint16(len(rb.players)))
	for _, p := range rb.players {
		b = binary.LittleEndian.AppendUint64(b, p.steamID)
		b = appendStr(b, p.name)
	}
	b = binary.LittleEndian.AppendUint16(b, uint16(len(rb.weapons)))
	for _, w := range rb.weapons {
		b = appendStr(b, w)
	}

	b = binary.LittleEndian.AppendUint32(b, uint32(rb.nFrames))
	b = binary.LittleEndian.AppendUint32(b, uint32(len(rb.frames)))
	b = append(b, rb.frames...)

	b = binary.LittleEndian.AppendUint32(b, uint32(len(rb.trackList)))
	for _, t := range rb.trackList {
		side := uint8(0)
		if t.ct {
			side = 1
		}
		b = append(b, t.kind, t.thrower, side)
		b = binary.LittleEndian.AppendUint16(b, uint16(len(t.points)))
		for _, pt := range t.points {
			b = binary.LittleEndian.AppendUint32(b, uint32(pt.tick))
			b = appendVec(b, pt.pos)
		}
	}

	b = binary.LittleEndian.AppendUint32(b, uint32(len(rb.effects)))
	for _, fx := range rb.effects {
		side := uint8(0)
		if fx.ct {
			side = 1
		}
		b = append(b, fx.kind, fx.thrower, side)
		b = binary.LittleEndian.AppendUint32(b, uint32(fx.start))
		b = binary.LittleEndian.AppendUint32(b, uint32(fx.end))
		b = appendVec(b, fx.pos)
		b = append(b, uint8(len(fx.hull)))
		for _, h := range fx.hull {
			b = binary.LittleEndian.AppendUint16(b, uint16(h[0]))
			b = binary.LittleEndian.AppendUint16(b, uint16(h[1]))
		}
	}

	b = binary.LittleEndian.AppendUint32(b, uint32(len(rb.kills)))
	for _, k := range rb.kills {
		b = binary.LittleEndian.AppendUint32(b, uint32(k.tick))
		b = append(b, k.killer, k.victim, k.assister, k.weapon, k.fl)
		b = appendVec(b, k.killerPos)
		b = appendVec(b, k.victimPos)
	}

	b = binary.LittleEndian.AppendUint32(b, uint32(len(rb.shots)))
	for _, s := range rb.shots {
		b = binary.LittleEndian.AppendUint32(b, uint32(s.tick))
		b = append(b, s.player, s.weapon)
	}

	b = binary.LittleEndian.AppendUint32(b, uint32(len(rb.bomb)))
	for _, e := range rb.bomb {
		b = binary.LittleEndian.AppendUint32(b, uint32(e.tick))
		b = append(b, e.kind, e.player)
		b = appendVec(b, e.pos)
	}
	return append(b, "END!"...)
}

// replayFile: Demo vorbereiten (ggf. entpacken) und die Rundendaten schreiben
func replayFile(demo, target, tempDir string) error {
	if tempDir == "" {
		tempDir = filepath.Dir(target)
	}
	if err := os.MkdirAll(tempDir, 0o755); err != nil {
		return coded("WRITE", "%v", err)
	}
	path, _, cleanup, err := prepareDemo(demo, tempDir)
	if err != nil {
		return err
	}
	defer cleanup()
	return buildReplay(path, target)
}
