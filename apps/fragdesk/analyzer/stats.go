package main

// Zähler pro Spieler (gesamt, als T, als CT) und pro Waffe.
//
// Runden/Ergebnis: rounds, roundsWon
// Kills:   kills, hsKills, assists, flashAssists, teamKills, suicides, deaths, deathsHs, teamDeaths, deathsBlind
//          k1…k5 (Runden mit genau 1…4 bzw. 5+ Kills), kRifle, kSniper, kSmg, kShotgun, kMg, kPistol, kKnife,
//          kGrenade, kZeus, kOther, kWallbang, kNoscope, kSmoke, kBlind (selbst geblendet), kOnBlind (Opfer geblendet),
//          kAirborne, kOnAirborne, kCollateral (zweiter Kill mit derselben Kugel), kLong, kOnEco, kUnderdog,
//          distSum, distMax (Meter), heKills, fireKills
// Eröffnung: openKills, openDeaths, openKillsWon, openDeathsWon, openDeathsTraded
// Trades:  tradeKills, tradedDeaths
// KAST:    kastRounds, survived
// Zeit:    aliveSec, deathSec, firstKillSec, firstKillRounds
// Clutch:  clutch1…clutch5, clutchWon1…clutchWon5, clutchKills, clutchSaved
// Schaden: dmg, armorDmg, utilDmg, heDmg, heHits, fireDmg, teamDmg, teamUtilDmg, dmgTaken, teamDmgTaken
// Treffer: hits, hitHead, hitNeck, hitChest, hitStomach, hitArms, hitLegs, hitOther
// Schüsse: shots, shotsHit, firstShots, firstShotsHit, sprayShots, sprayShotsHit, movingShots, movingShotsHit,
//          standingShots, standingShotsHit
// Granaten: flashThrown, smokeThrown, heThrown, fireThrown, decoyThrown
// Flashes: enemiesFlashed, enemyFlashDur, flashesEffective, flashLeadKills, teamFlashed, teamFlashDur, selfFlashed,
//          selfFlashDur, gotFlashed, gotFlashedDur
// Bombe:   plants, plantsA, plantsB, plantStarts, defuses, defusesVsAlive, defuseStarts
// Economy: spent, equipSum, moneySum, pistolRounds, ecoRounds, semiRounds, forceRounds, fullRounds und …Won
// Sonst:   mvps, score

import (
	"math"
	"sort"
	"strconv"

	"github.com/akiver/cs-demo-analyzer/pkg/api"
	"github.com/akiver/cs-demo-analyzer/pkg/api/constants"
	"github.com/markus-wa/demoinfocs-golang/v4/pkg/demoinfocs/common"
	"github.com/markus-wa/demoinfocs-golang/v4/pkg/demoinfocs/events"
)

const (
	effectiveFlashSec = 1.0 // eine Flash "wirkt", wenn sie mindestens einen Gegner so lange blendet
	longKillMeters    = 30  // weiter Kill (Scharfschützengewehre: doppelt so weit)
)

type bucket struct{ all, t, ct Counters }

func newBucket() *bucket { return &bucket{Counters{}, Counters{}, Counters{}} }

func (b *bucket) add(side common.Team, key string, v float64) {
	if v == 0 {
		return
	}
	b.all[key] += v
	switch side {
	case common.TeamTerrorists:
		b.t[key] += v
	case common.TeamCounterTerrorists:
		b.ct[key] += v
	}
}

func (b *bucket) inc(side common.Team, key string) { b.add(side, key, 1) }

func (b *bucket) max(side common.Team, key string, v float64) {
	upd := func(c Counters) {
		if v > c[key] {
			c[key] = v
		}
	}
	upd(b.all)
	switch side {
	case common.TeamTerrorists:
		upd(b.t)
	case common.TeamCounterTerrorists:
		upd(b.ct)
	}
}

type weaponAcc struct {
	typ constants.WeaponType
	c   Counters
}

type playerCtx struct {
	p        *api.Player
	id       uint64
	sid      string
	team     string
	b        *bucket
	weapons  map[constants.WeaponName]*weaponAcc
	deathsBy map[string]int
	rounds   map[int]*PlayerRound
	opp      map[uint64]*OpponentOut
}

func (pc *playerCtx) weapon(name constants.WeaponName, t constants.WeaponType) Counters {
	w := pc.weapons[name]
	if w == nil {
		if t == "" || t == constants.WeaponTypeUnknown {
			t = weaponTypeOf(name)
		}
		w = &weaponAcc{typ: t, c: Counters{}}
		pc.weapons[name] = w
	}
	return w.c
}

func (pc *playerCtx) opponent(id uint64) *OpponentOut {
	o := pc.opp[id]
	if o == nil {
		o = &OpponentOut{SteamID: strconv.FormatUint(id, 10)}
		pc.opp[id] = o
	}
	return o
}

func (pc *playerCtx) round(n int) *PlayerRound {
	r := pc.rounds[n]
	if r == nil {
		r = &PlayerRound{N: n}
		pc.rounds[n] = r
	}
	return r
}

type statsBuilder struct {
	m        *api.Match
	players  map[uint64]*playerCtx
	tickrate float64
	econ     map[[2]uint64]*api.PlayerEconomy
	rounds   map[int]*api.Round
}

func sideStr(t common.Team) string {
	switch t {
	case common.TeamTerrorists:
		return "T"
	case common.TeamCounterTerrorists:
		return "CT"
	}
	return ""
}

func econKey(t constants.EconomyType) string {
	switch t {
	case constants.EconomyTypePistol:
		return "pistol"
	case constants.EconomyTypeEco:
		return "eco"
	case constants.EconomyTypeSemi:
		return "semi"
	case constants.EconomyTypeForceBuy:
		return "force"
	case constants.EconomyTypeFull:
		return "full"
	}
	return ""
}

func hitKey(g events.HitGroup) string {
	switch g {
	case events.HitGroupHead:
		return "hitHead"
	case events.HitGroupNeck:
		return "hitNeck"
	case events.HitGroupChest:
		return "hitChest"
	case events.HitGroupStomach:
		return "hitStomach"
	case events.HitGroupLeftArm, events.HitGroupRightArm:
		return "hitArms"
	case events.HitGroupLeftLeg, events.HitGroupRightLeg:
		return "hitLegs"
	}
	return "hitOther"
}

func newStatsBuilder(m *api.Match) *statsBuilder {
	sb := &statsBuilder{
		m:        m,
		players:  map[uint64]*playerCtx{},
		tickrate: m.TickRate,
		econ:     map[[2]uint64]*api.PlayerEconomy{},
		rounds:   map[int]*api.Round{},
	}
	if sb.tickrate <= 0 {
		sb.tickrate = 64
	}
	for id, p := range m.PlayersBySteamID {
		if id == 0 || p == nil || p.Team == nil {
			continue
		}
		sb.players[id] = &playerCtx{
			p: p, id: id, sid: strconv.FormatUint(id, 10), team: string(p.Team.Letter), b: newBucket(),
			weapons: map[constants.WeaponName]*weaponAcc{}, deathsBy: map[string]int{},
			rounds: map[int]*PlayerRound{}, opp: map[uint64]*OpponentOut{},
		}
	}
	for _, e := range m.PlayerEconomies {
		sb.econ[[2]uint64{uint64(e.RoundNumber), e.SteamID64}] = e
	}
	for _, r := range m.Rounds {
		sb.rounds[r.Number] = r
	}
	return sb
}

// Seite eines Spielers in einer Runde (über sein Team).
func (sb *statsBuilder) side(pc *playerCtx, r *api.Round) common.Team {
	switch pc.team {
	case string(constants.TeamLetterA):
		return r.TeamASide
	case string(constants.TeamLetterB):
		return r.TeamBSide
	}
	return common.TeamUnassigned
}

func (sb *statsBuilder) secs(ticks int) float64 { return float64(ticks) / sb.tickrate }

func freezeEnd(r *api.Round) int {
	if r.FreezeTimeEndTick > 0 && r.FreezeTimeEndTick >= r.StartTick {
		return r.FreezeTimeEndTick
	}
	return r.StartTick
}

// Index des Eröffnungskills einer Runde in kills (wie cs-demo-analyzer: Selbstmorde, Teamkills und Kills eines
// Spielers, der einen Bot steuert, zählen nicht).
func openingIndex(kills []*api.Kill) int {
	for i, k := range kills {
		if k.IsKillerControllingBot || k.KillerSteamID64 == 0 || k.IsSuicide() || k.IsTeamKill() {
			continue
		}
		return i
	}
	return -1
}

func (sb *statsBuilder) build() {
	m := sb.m
	killsByRound := map[int][]*api.Kill{}
	for _, k := range m.Kills {
		killsByRound[k.RoundNumber] = append(killsByRound[k.RoundNumber], k)
	}

	for _, r := range m.Rounds {
		freeze := freezeEnd(r)
		pistolRound := r.TeamAEconomyType == constants.EconomyTypePistol
		for _, pc := range sb.players {
			side := sb.side(pc, r)
			if side == common.TeamUnassigned {
				continue
			}
			pr := pc.round(r.Number)
			pr.Side = sideStr(side)
			pr.Won = r.WinnerSide == side
			pc.b.inc(side, "rounds")
			if pr.Won {
				pc.b.inc(side, "roundsWon")
			}
			if e := sb.econ[[2]uint64{uint64(r.Number), pc.id}]; e != nil {
				pr.Equip, pr.Spent, pr.Money = e.EquipmentValue, e.MoneySpent, e.StartMoney
				pr.Econ = econKey(e.Type)
				pc.b.add(side, "spent", float64(e.MoneySpent))
				pc.b.add(side, "equipSum", float64(e.EquipmentValue))
				pc.b.add(side, "moneySum", float64(e.StartMoney))
				pc.b.add(side, "econRounds", 1)
				if pr.Econ != "" {
					pc.b.inc(side, pr.Econ+"Rounds")
					if pr.Won {
						pc.b.inc(side, pr.Econ+"Won")
					}
				}
			}
		}

		rk := killsByRound[r.Number]
		open := openingIndex(rk)
		for i, k := range rk {
			sb.kill(k, r, i == open, pistolRound)
		}
		sb.collaterals(rk)

		for _, pc := range sb.players {
			side := sb.side(pc, r)
			if side == common.TeamUnassigned {
				continue
			}
			pr := pc.round(r.Number)
			survived := !pr.Dead
			if survived {
				pc.b.inc(side, "survived")
			}
			if pr.Kills > 0 || pr.Assists > 0 || pr.FlashAssists > 0 || survived || pr.Traded {
				pr.Kast = true
				pc.b.inc(side, "kastRounds")
			}
			switch {
			case pr.Kills >= 5:
				pc.b.inc(side, "k5")
			case pr.Kills > 0:
				pc.b.inc(side, "k"+strconv.Itoa(pr.Kills))
			}
			end := r.EndTick
			if pr.Dead && pr.DeathTick < end {
				end = pr.DeathTick
			}
			alive := math.Max(0, sb.secs(end-freeze))
			pr.AliveSec = math.Round(alive*10) / 10
			pc.b.add(side, "aliveSec", alive)
			if pr.Dead && pr.DeathTick >= freeze {
				pc.b.add(side, "deathSec", sb.secs(pr.DeathTick-freeze))
			}
			if pr.FirstKillT > 0 {
				pc.b.add(side, "firstKillSec", math.Max(0, sb.secs(pr.FirstKillT-freeze)))
				pc.b.inc(side, "firstKillRounds")
			}
		}
	}

	sb.damages()
	sb.shots()
	sb.flashes()
	sb.bombs()
	sb.clutches()

	for _, pc := range sb.players {
		pc.b.all["mvps"] = float64(pc.p.MvpCount)
		pc.b.all["score"] = float64(pc.p.Score)
	}
}

func (sb *statsBuilder) econType(round int, id uint64) *api.PlayerEconomy {
	return sb.econ[[2]uint64{uint64(round), id}]
}

func (sb *statsBuilder) kill(k *api.Kill, r *api.Round, opening, pistolRound bool) {
	killer := sb.players[k.KillerSteamID64]
	victim := sb.players[k.VictimSteamID64]
	assister := sb.players[k.AssisterSteamID64]
	suicide := k.IsSuicide()
	teamKill := !suicide && k.IsTeamKill()
	kSide, vSide := k.KillerSide, k.VictimSide
	weapon := string(k.WeaponName)

	if victim != nil && !k.IsVictimControllingBot {
		disconnect := suicide && k.WeaponName == constants.WeaponWorld
		if !disconnect {
			b := victim.b
			pr := victim.round(k.RoundNumber)
			b.inc(vSide, "deaths")
			pr.Dead = true
			pr.DeathTick = k.Tick
			pr.KillerWeapon = weapon
			if !suicide && k.KillerSteamID64 != 0 {
				pr.Killer = strconv.FormatUint(k.KillerSteamID64, 10)
			}
			switch {
			case suicide || k.KillerSteamID64 == 0:
				b.inc(vSide, "suicides")
			case teamKill:
				b.inc(vSide, "teamDeaths")
			default:
				victim.deathsBy[weapon]++
				if k.IsHeadshot {
					b.inc(vSide, "deathsHs")
				}
				if k.IsVictimBlinded {
					b.inc(vSide, "deathsBlind")
				}
				if killer != nil {
					victim.opponent(killer.id).Deaths++
				}
				if k.IsTradeDeath {
					b.inc(vSide, "tradedDeaths")
					pr.Traded = true
				}
				if opening {
					b.inc(vSide, "openDeaths")
					pr.Open = "D"
					if r.WinnerSide == vSide {
						b.inc(vSide, "openDeathsWon")
					}
					if k.IsTradeDeath {
						b.inc(vSide, "openDeathsTraded")
					}
					if killer != nil {
						victim.opponent(killer.id).OpenDeaths++
					}
				}
			}
		}
	}

	if killer != nil && !k.IsKillerControllingBot && !suicide {
		b := killer.b
		if teamKill {
			b.inc(kSide, "teamKills")
		} else {
			pr := killer.round(k.RoundNumber)
			pr.Kills++
			if pr.FirstKillT == 0 {
				pr.FirstKillT = k.Tick
			}
			b.inc(kSide, "kills")
			b.inc(kSide, killCategory(k.WeaponType, k.WeaponName))
			w := killer.weapon(k.WeaponName, k.WeaponType)
			w["kills"]++
			if k.IsHeadshot {
				b.inc(kSide, "hsKills")
				pr.HsKills++
				w["hsKills"]++
			}
			if k.PenetratedObjects > 0 {
				b.inc(kSide, "kWallbang")
				w["kWallbang"]++
			}
			if k.IsNoScope && isSniper(k.WeaponName) {
				b.inc(kSide, "kNoscope")
				w["kNoscope"]++
			}
			if k.IsThroughSmoke {
				b.inc(kSide, "kSmoke")
			}
			if k.IsKillerBlinded {
				b.inc(kSide, "kBlind")
			}
			if k.IsVictimBlinded {
				b.inc(kSide, "kOnBlind")
			}
			if k.IsKillerAirborne {
				b.inc(kSide, "kAirborne")
			}
			if k.IsVictimAirborne {
				b.inc(kSide, "kOnAirborne")
			}
			if k.IsTradeKill {
				b.inc(kSide, "tradeKills")
			}
			switch {
			case k.WeaponName == constants.WeaponHEGrenade:
				b.inc(kSide, "heKills")
			case isFire(k.WeaponName):
				b.inc(kSide, "fireKills")
			}
			dist := float64(k.Distance)
			if dist > 0 {
				b.add(kSide, "distSum", dist)
				b.max(kSide, "distMax", dist)
				w["distSum"] += dist
				if isLongKill(k) {
					b.inc(kSide, "kLong")
				}
			}
			ke, ve := sb.econType(k.RoundNumber, k.KillerSteamID64), sb.econType(k.RoundNumber, k.VictimSteamID64)
			if ve != nil && !pistolRound && (ve.Type == constants.EconomyTypeEco || ve.Type == constants.EconomyTypeSemi) {
				b.inc(kSide, "kOnEco")
			}
			if ke != nil && ve != nil && ve.EquipmentValue >= ke.EquipmentValue+1500 {
				b.inc(kSide, "kUnderdog")
			}
			if victim != nil {
				killer.opponent(victim.id).Kills++
			}
			if opening {
				b.inc(kSide, "openKills")
				pr.Open = "K"
				w["openKills"]++
				if r.WinnerSide == kSide {
					b.inc(kSide, "openKillsWon")
				}
				if victim != nil {
					killer.opponent(victim.id).OpenKills++
				}
			}
		}
	}

	if assister != nil && !k.IsAssisterControllingBot && k.AssisterSide != vSide && k.AssisterSteamID64 != k.KillerSteamID64 {
		pr := assister.round(k.RoundNumber)
		if k.IsAssistedFlash {
			assister.b.inc(k.AssisterSide, "flashAssists")
			pr.FlashAssists++
		} else {
			assister.b.inc(k.AssisterSide, "assists")
			pr.Assists++
		}
	}
}

func isLongKill(k *api.Kill) bool {
	limit := float32(longKillMeters)
	if isSniper(k.WeaponName) {
		limit *= 2
	}
	return k.Distance >= limit
}

// Mehrere Kills mit derselben Kugel (gleicher Tick, gleiche Waffe): jeder weitere zählt als Kollateral.
func (sb *statsBuilder) collaterals(kills []*api.Kill) {
	type key struct {
		killer uint64
		tick   int
		weapon constants.WeaponName
	}
	count := map[key]int{}
	side := map[key]common.Team{}
	for _, k := range kills {
		if k.KillerSteamID64 == 0 || k.IsKillerControllingBot || k.IsSuicide() || k.IsTeamKill() || !isBulletWeapon(k.WeaponType) {
			continue
		}
		kk := key{k.KillerSteamID64, k.Tick, k.WeaponName}
		count[kk]++
		side[kk] = k.KillerSide
	}
	for kk, n := range count {
		if n > 1 {
			if pc := sb.players[kk.killer]; pc != nil {
				pc.b.add(side[kk], "kCollateral", float64(n-1))
			}
		}
	}
}

func (sb *statsBuilder) damages() {
	for _, d := range sb.m.Damages {
		if d.AttackerSteamID64 == 0 || d.VictimSteamID64 == 0 || d.AttackerSteamID64 == d.VictimSteamID64 {
			continue
		}
		// Schaden nach Rundenende (Exit-Frags) zählt wie bei FACEIT nicht.
		if r := sb.rounds[d.RoundNumber]; r != nil && r.EndTick > 0 && d.Tick > r.EndTick {
			continue
		}
		att := sb.players[d.AttackerSteamID64]
		vic := sb.players[d.VictimSteamID64]
		enemy := d.AttackerSide != d.VictimSide
		hp := float64(d.HealthDamage)
		if att != nil && !d.IsAttackerControllingBot {
			side := d.AttackerSide
			b := att.b
			if enemy {
				pr := att.round(d.RoundNumber)
				pr.Damage += d.HealthDamage
				b.add(side, "dmg", hp)
				b.add(side, "armorDmg", float64(d.ArmorDamage))
				w := att.weapon(d.WeaponName, d.WeaponType)
				w["dmg"] += hp
				if d.WeaponType == constants.WeaponTypeGrenade {
					pr.UtilDamage += d.HealthDamage
					b.add(side, "utilDmg", hp)
					switch {
					case d.WeaponName == constants.WeaponHEGrenade:
						b.add(side, "heDmg", hp)
						b.inc(side, "heHits")
					case isFire(d.WeaponName):
						b.add(side, "fireDmg", hp)
					}
				}
				if isBulletWeapon(d.WeaponType) {
					b.inc(side, "hits")
					hk := hitKey(d.HitGroup)
					b.inc(side, hk)
					w["hits"]++
					w[hk]++
				}
				if vic != nil {
					att.opponent(vic.id).DmgDealt += d.HealthDamage
				}
			} else {
				b.add(side, "teamDmg", hp)
				if d.WeaponType == constants.WeaponTypeGrenade {
					b.add(side, "teamUtilDmg", hp)
				}
			}
		}
		if vic != nil && !d.IsVictimControllingBot {
			if enemy {
				vic.b.add(d.VictimSide, "dmgTaken", hp)
				if att != nil {
					vic.opponent(att.id).DmgTaken += d.HealthDamage
				}
			} else {
				vic.b.add(d.VictimSide, "teamDmgTaken", hp)
			}
		}
	}
}

func (sb *statsBuilder) shots() {
	type hk struct {
		id     uint64
		tick   int
		weapon constants.WeaponName
	}
	hit := map[hk]bool{}
	for _, d := range sb.m.Damages {
		if d.AttackerSteamID64 != 0 && d.VictimSteamID64 != d.AttackerSteamID64 && isBulletWeapon(d.WeaponType) {
			hit[hk{d.AttackerSteamID64, d.Tick, d.WeaponName}] = true
		}
	}
	for _, s := range sb.m.Shots {
		pc := sb.players[s.PlayerSteamID64]
		if pc == nil || s.IsPlayerControllingBot {
			continue
		}
		side := s.PlayerSide
		if key := throwKey(s.WeaponName); key != "" {
			pc.b.inc(side, key)
			continue
		}
		t := weaponTypeOf(s.WeaponName)
		if !isBulletWeapon(t) {
			continue
		}
		isHit := hit[hk{s.PlayerSteamID64, s.Tick, s.WeaponName}] || hit[hk{s.PlayerSteamID64, s.Tick + 1, s.WeaponName}]
		w := pc.weapon(s.WeaponName, t)
		count := func(key string) {
			pc.b.inc(side, key)
			w[key]++
			if isHit {
				pc.b.inc(side, key+"Hit")
				w[key+"Hit"]++
			}
		}
		count("shots")
		switch {
		case s.RecoilIndex < 1.5:
			count("firstShots")
		case s.RecoilIndex >= 4:
			count("sprayShots")
		}
		if math.Hypot(s.PlayerVelocityX, s.PlayerVelocityY) > accurateSpeed(s.WeaponName) {
			count("movingShots")
		} else {
			count("standingShots")
		}
	}
}

func (sb *statsBuilder) flashes() {
	type group struct {
		flasher uint64
		tick    int
	}
	type flash struct {
		tick    int
		end     int
		flasher uint64
		side    common.Team
	}
	best := map[group]float64{}
	groupSide := map[group]common.Team{}
	onVictim := map[uint64][]flash{}
	for _, f := range sb.m.PlayersFlashed {
		dur := float64(f.Duration)
		if vic := sb.players[f.FlashedSteamID64]; vic != nil && !f.IsFlashedControllingBot && f.FlashedSide != f.FlasherSide {
			vic.b.inc(f.FlashedSide, "gotFlashed")
			vic.b.add(f.FlashedSide, "gotFlashedDur", dur)
		}
		fl := sb.players[f.FlasherSteamID64]
		if fl == nil || f.IsFlasherControllingBot {
			continue
		}
		side := f.FlasherSide
		switch {
		case f.FlashedSteamID64 == f.FlasherSteamID64:
			fl.b.inc(side, "selfFlashed")
			fl.b.add(side, "selfFlashDur", dur)
		case f.FlashedSide == f.FlasherSide:
			fl.b.inc(side, "teamFlashed")
			fl.b.add(side, "teamFlashDur", dur)
		default:
			if dur <= 0 {
				continue
			}
			fl.b.inc(side, "enemiesFlashed")
			fl.b.add(side, "enemyFlashDur", dur)
			fl.round(f.RoundNumber).Flashed++
			g := group{f.FlasherSteamID64, f.Tick}
			if dur > best[g] {
				best[g] = dur
			}
			groupSide[g] = side
			onVictim[f.FlashedSteamID64] = append(onVictim[f.FlashedSteamID64], flash{
				tick: f.Tick, end: f.Tick + int(dur*sb.tickrate), flasher: f.FlasherSteamID64, side: side,
			})
		}
	}
	for g, dur := range best {
		if dur >= effectiveFlashSec {
			sb.players[g.flasher].b.inc(groupSide[g], "flashesEffective")
		}
	}
	// Kills an Gegnern, die gerade von dir geblendet waren (egal wer aus deinem Team trifft).
	for _, k := range sb.m.Kills {
		if k.KillerSteamID64 == 0 || k.IsSuicide() || k.IsTeamKill() {
			continue
		}
		var credit *flash
		for i := range onVictim[k.VictimSteamID64] {
			f := &onVictim[k.VictimSteamID64][i]
			if f.side == k.KillerSide && f.tick <= k.Tick && k.Tick <= f.end+int(0.5*sb.tickrate) {
				if credit == nil || f.tick > credit.tick {
					credit = f
				}
			}
		}
		if credit != nil {
			if pc := sb.players[credit.flasher]; pc != nil {
				pc.b.inc(credit.side, "flashLeadKills")
			}
		}
	}
}

func (sb *statsBuilder) bombs() {
	for _, b := range sb.m.BombsPlanted {
		if pc := sb.players[b.PlanterSteamID64]; pc != nil && !b.IsPlayerControllingBot {
			pc.b.inc(common.TeamTerrorists, "plants")
			switch b.Site {
			case "A":
				pc.b.inc(common.TeamTerrorists, "plantsA")
			case "B":
				pc.b.inc(common.TeamTerrorists, "plantsB")
			}
			pc.round(b.RoundNumber).Plant = true
		}
	}
	for _, b := range sb.m.BombsDefused {
		if pc := sb.players[b.DefuserSteamID64]; pc != nil && !b.IsPlayerControllingBot {
			pc.b.inc(common.TeamCounterTerrorists, "defuses")
			if b.TerroristAliveCount > 0 {
				pc.b.inc(common.TeamCounterTerrorists, "defusesVsAlive")
			}
			pc.round(b.RoundNumber).Defuse = true
		}
	}
	for _, b := range sb.m.BombsPlantStart {
		if pc := sb.players[b.PlanterSteamID64]; pc != nil && !b.IsPlayerControllingBot {
			pc.b.inc(common.TeamTerrorists, "plantStarts")
		}
	}
	for _, b := range sb.m.BombsDefuseStart {
		if pc := sb.players[b.PlanterSteamID64]; pc != nil && !b.IsPlayerControllingBot {
			pc.b.inc(common.TeamCounterTerrorists, "defuseStarts")
		}
	}
}

func (sb *statsBuilder) clutches() {
	for _, c := range sb.m.Clutches {
		pc := sb.players[c.ClutcherSteamID64]
		if pc == nil || c.OpponentCount < 1 {
			continue
		}
		n := c.OpponentCount
		if n > 5 {
			n = 5
		}
		key := strconv.Itoa(n)
		pc.b.inc(c.Side, "clutch"+key)
		if c.HasWon {
			pc.b.inc(c.Side, "clutchWon"+key)
		} else if c.ClutcherSurvived {
			pc.b.inc(c.Side, "clutchSaved")
		}
		pc.b.add(c.Side, "clutchKills", float64(c.ClutcherKillCount))
		pr := pc.round(c.RoundNumber)
		if n > pr.Clutch {
			pr.Clutch = n
			pr.ClutchWon = c.HasWon
		}
	}
}

// Rating 2.0 (Näherung wie cs-demo-analyzer), Rating 1.0, KAST in % und ADR aus den Zählern.
func ratings(c Counters) (r2, r1, kast, adr float64) {
	rounds := c["rounds"]
	if rounds <= 0 {
		return 0, 0, 0, 0
	}
	kpr := c["kills"] / rounds
	dpr := c["deaths"] / rounds
	apr := (c["assists"] + c["flashAssists"]) / rounds
	kast = 100 * c["kastRounds"] / rounds
	adr = c["dmg"] / rounds
	impact := 2.13*kpr + 0.42*apr - 0.41
	r2 = 0.0073*kast + 0.3591*kpr - 0.5329*dpr + 0.2372*impact + 0.0032*adr + 0.1587
	if r2 < 0 {
		r2 = 0
	}
	survival := (rounds - c["deaths"]) / rounds / 0.317
	multi := (c["k1"] + 4*c["k2"] + 9*c["k3"] + 16*c["k4"] + 25*c["k5"]) / rounds / 1.277
	r1 = (kpr/0.679 + 0.7*survival + multi) / 2.7
	return r2, r1, kast, adr
}

func round2(v float64) float64 { return math.Round(v*100) / 100 }

func roundCounters(c Counters) Counters {
	for k, v := range c {
		c[k] = math.Round(v*1000) / 1000
	}
	return c
}

func (pc *playerCtx) weaponList() []WeaponOut {
	out := make([]WeaponOut, 0, len(pc.weapons))
	for name, w := range pc.weapons {
		out = append(out, WeaponOut{Name: string(name), Type: string(w.typ), Stats: roundCounters(w.c)})
	}
	sort.Slice(out, func(i, j int) bool {
		a, b := out[i].Stats, out[j].Stats
		if a["kills"] != b["kills"] {
			return a["kills"] > b["kills"]
		}
		if a["shots"] != b["shots"] {
			return a["shots"] > b["shots"]
		}
		return out[i].Name < out[j].Name
	})
	return out
}

func (pc *playerCtx) roundList() []PlayerRound {
	out := make([]PlayerRound, 0, len(pc.rounds))
	for _, r := range pc.rounds {
		if r.Side == "" {
			continue
		}
		out = append(out, *r)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].N < out[j].N })
	return out
}

func (pc *playerCtx) opponentList() []OpponentOut {
	out := make([]OpponentOut, 0, len(pc.opp))
	for _, o := range pc.opp {
		out = append(out, *o)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].SteamID < out[j].SteamID })
	return out
}
