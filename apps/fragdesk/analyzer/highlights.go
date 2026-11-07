package main

import (
	"fmt"
	"sort"

	"github.com/akiver/cs-demo-analyzer/pkg/api/constants"
)

// Highlight-Erkennung: pro Spieler und Runde werden Kills, gewonnene Clutches und besondere Kills bewertet.
// Ab minHighlightScore wird die Runde als Moment vorgeschlagen. Die Clip-Abschnitte decken jeden Kill
// (4 s davor, 2 s danach) und bei Clutches den ganzen Clutch ab; nahe Abschnitte werden zusammengelegt.

const (
	minHighlightScore = 30
	preKillSec        = 4.0
	postKillSec       = 2.0
	mergeGapSec       = 3.0
	maxClutchSec      = 45.0
)

var killScore = []int{0, 5, 15, 45, 70, 100}
var clutchScore = []int{0, 20, 40, 60, 80, 100}

func buildHighlights(out *Output, tickrate float64) []Highlight {
	type key struct {
		player string
		round  int
	}
	killsBy := map[key][]KillOut{}
	for _, k := range out.Kills {
		if k.Killer == "" || k.Killer == k.Victim || k.TeamKill || k.KillerSide == k.VictimSide {
			continue
		}
		kk := key{k.Killer, k.Round}
		killsBy[kk] = append(killsBy[kk], k)
	}
	clutchBy := map[key]ClutchOut{}
	for _, c := range out.Clutches {
		if c.Won {
			kk := key{c.Player, c.Round}
			if old, ok := clutchBy[kk]; !ok || c.Vs > old.Vs {
				clutchBy[kk] = c
			}
		}
	}
	rounds := map[int]RoundOut{}
	for _, r := range out.Rounds {
		rounds[r.N] = r
	}
	// Spieler, die gerade einen Bot steuern, stehen in kills mit der eigenen SteamID; das ist gewollt.
	keys := map[key]bool{}
	for k := range killsBy {
		keys[k] = true
	}
	for k := range clutchBy {
		keys[k] = true
	}

	ticks := func(sec float64) int { return int(sec * tickrate) }
	var list []Highlight
	for kk := range keys {
		kills := killsBy[kk]
		sort.Slice(kills, func(i, j int) bool { return kills[i].Tick < kills[j].Tick })
		clutch, isClutch := clutchBy[kk]
		r, ok := rounds[kk.round]
		if !ok {
			continue
		}

		n := len(kills)
		score := killScore[min(n, 5)]
		var tags []string
		switch {
		case n >= 5:
			tags = append(tags, "ace")
		case n == 4:
			tags = append(tags, "4k")
		case n == 3:
			tags = append(tags, "3k")
		case n == 2:
			tags = append(tags, "2k")
		}
		if isClutch {
			score += clutchScore[min(clutch.Vs, 5)]
			tags = append(tags, "clutch")
		}
		special := map[string]int{}
		weapons := map[string]bool{}
		hsAll := n >= 3
		for i, k := range kills {
			weapons[k.Weapon] = true
			name := constants.WeaponName(k.Weapon)
			if k.NoScope && isSniper(name) {
				special["noscope"] += 30
			}
			if k.Penetrated > 0 {
				special["wallbang"] += 20
			}
			if k.Smoke {
				special["smoke"] += 15
			}
			if k.KillerBlind {
				special["blind"] += 25
			}
			if k.KillerAir {
				special["air"] += 25
				if isSniper(name) {
					special["air"] += 15
				}
			}
			if name == constants.WeaponKnife {
				special["knife"] += 35
			}
			if name == constants.WeaponZeus {
				special["zeus"] += 20
			}
			if k.Distance >= longKillMeters && (!isSniper(name) || k.Distance >= 2*longKillMeters) {
				special["long"] += 10
			}
			if i > 0 && kills[i-1].Tick == k.Tick && kills[i-1].Weapon == k.Weapon && isBulletWeapon(constants.WeaponType(k.WeaponType)) {
				special["collateral"] += 40
			}
			if !k.Headshot {
				hsAll = false
			}
		}
		if hsAll {
			special["headshots"] += 10
		}
		// Schnelle Kills: drei innerhalb von 2 s oder zwei innerhalb von 0,8 s.
		for i := 2; i < n; i++ {
			if kills[i].Tick-kills[i-2].Tick <= ticks(2.0) {
				special["quick"] = 20
				break
			}
		}
		if special["quick"] == 0 {
			for i := 1; i < n; i++ {
				if kills[i].Tick-kills[i-1].Tick <= ticks(0.8) {
					special["quick"] = 8
					break
				}
			}
		}
		specialNames := make([]string, 0, len(special))
		for name, v := range special {
			score += v
			specialNames = append(specialNames, name)
		}
		sort.Strings(specialNames)
		tags = append(tags, specialNames...)
		if score < minHighlightScore {
			continue
		}

		// Abschnitte
		lo := r.FreezeEnd - ticks(1)
		if lo < r.Start {
			lo = r.Start
		}
		hi := r.EndOfficial
		if hi <= 0 || hi < r.End {
			hi = r.End + ticks(2)
		}
		var segs [][2]int
		for _, k := range kills {
			segs = append(segs, [2]int{k.Tick - ticks(preKillSec), k.Tick + ticks(postKillSec)})
		}
		if isClutch {
			end := r.End + ticks(1)
			start := clutch.Tick - ticks(2)
			if end-start > ticks(maxClutchSec) {
				// Lange Clutches: Anfang und Ende zeigen, die Kills liegen ohnehin in eigenen Abschnitten.
				segs = append(segs, [2]int{start, start + ticks(6)}, [2]int{end - ticks(5), end})
			} else {
				segs = append(segs, [2]int{start, end})
			}
		}
		segs = mergeSegments(segs, ticks(mergeGapSec), lo, hi)
		if len(segs) == 0 {
			continue
		}

		ws := make([]string, 0, len(weapons))
		for w := range weapons {
			ws = append(ws, w)
		}
		sort.Strings(ws)
		kt := make([]int, len(kills))
		for i, k := range kills {
			kt[i] = k.Tick
		}
		side := ""
		if n > 0 {
			side = kills[0].KillerSide
		} else if isClutch {
			side = clutch.Side
		}
		h := Highlight{
			ID: fmt.Sprintf("%s-r%d", kk.player, kk.round), Player: kk.player, Round: kk.round, Side: side,
			Score: score, Kills: n, Tags: tags, Weapons: ws, Segments: segs, KillTicks: kt,
		}
		if isClutch {
			h.Clutch = clutch.Vs
		}
		list = append(list, h)
	}
	sort.Slice(list, func(i, j int) bool {
		if list[i].Score != list[j].Score {
			return list[i].Score > list[j].Score
		}
		if list[i].Round != list[j].Round {
			return list[i].Round < list[j].Round
		}
		return list[i].Player < list[j].Player
	})
	return list
}

func mergeSegments(segs [][2]int, gap, lo, hi int) [][2]int {
	for i := range segs {
		if segs[i][0] < lo {
			segs[i][0] = lo
		}
		if segs[i][1] > hi {
			segs[i][1] = hi
		}
	}
	sort.Slice(segs, func(i, j int) bool { return segs[i][0] < segs[j][0] })
	var out [][2]int
	for _, s := range segs {
		if s[1] <= s[0] {
			continue
		}
		if len(out) > 0 && s[0] <= out[len(out)-1][1]+gap {
			if s[1] > out[len(out)-1][1] {
				out[len(out)-1][1] = s[1]
			}
			continue
		}
		out = append(out, s)
	}
	return out
}
