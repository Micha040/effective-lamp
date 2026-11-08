package main

import (
	"errors"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/akiver/cs-demo-analyzer/pkg/api"
	"github.com/akiver/cs-demo-analyzer/pkg/api/constants"
	"github.com/markus-wa/demoinfocs-golang/v4/pkg/demoinfocs/events"
)

// analyzeFile wertet eine Demo aus und baut die Ausgabe für die App. Mit replayTo entstehen im selben Lauf die Daten der
// 2D-Ansicht (Datei replayTo); geht das schief, bleibt die Auswertung gültig (Hinweis in Warnings, wie bei der Heatmap).
func analyzeFile(path, tempDir, replayTo string, step func(string)) (*Output, error) {
	abs, err := filepath.Abs(path)
	if err == nil {
		path = abs
	}
	st, err := os.Stat(path)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return nil, coded("NOT_FOUND", "%s", path)
		}
		return nil, coded("READ", "%v", err)
	}

	step("unpack")
	demoPath, packed, cleanup, err := prepareDemo(path, tempDir)
	if err != nil {
		return nil, err
	}
	defer cleanup()

	step("parse")
	// Heatmap-Daten, Zielen und (mit replayTo) die 2D-Daten im zweiten Durchgang, gleichzeitig mit cs-demo-analyzer
	// (beide lesen nur)
	extrasDone := make(chan *extrasResult, 1)
	var extrasErr error
	go func() {
		res, err := buildExtras(demoPath, replayTo)
		if err != nil {
			extrasErr = err
		}
		extrasDone <- res
	}()
	m, err := analyzeSafely(demoPath)
	ex := <-extrasDone // auch bei Fehlern abwarten: cleanup() löscht gleich die entpackte Demo
	if err != nil {
		return nil, err
	}
	if len(m.Rounds) == 0 {
		return nil, coded("EMPTY", "keine vollständige Runde gefunden")
	}

	step("stats")
	out := &Output{
		Schema:   schemaVersion,
		Analyzer: version,
		Csda:     csdaVersion,
		Checksum: m.Checksum,
		Demo: DemoInfo{
			Path: path, File: filepath.Base(path), Size: st.Size(),
			Modified: st.ModTime().UTC().Format(time.RFC3339), Packed: packed,
		},
	}
	fillMatch(out, m)
	sb := newStatsBuilder(m)
	sb.build()
	fillKills(out, m)
	fillRounds(out, m)
	fillPlayers(out, m, sb)
	if extrasErr != nil {
		out.Warnings = append(out.Warnings, "Heatmap und Zielen: "+extrasErr.Error())
	} else {
		mergeAim(out, ex.aim)
		out.heat = ex.heat
	}
	if replayTo != "" && ex.replayErr != nil {
		out.Warnings = append(out.Warnings, "2D-Daten: "+ex.replayErr.Error())
	}
	out.Highlights = buildHighlights(out, sb.tickrate)
	if out.Highlights == nil {
		out.Highlights = []Highlight{}
	}
	return out, nil
}

// Die Auswertung darf bei einer kaputten Demo nicht das ganze Programm mitreißen.
func analyzeSafely(demoPath string) (m *api.Match, err error) {
	defer func() {
		if r := recover(); r != nil {
			m, err = nil, coded("CORRUPT", "Auswertung abgebrochen: %v", r)
		}
	}()
	m, err = api.AnalyzeDemo(demoPath, api.AnalyzeDemoOptions{})
	if err != nil {
		msg := err.Error()
		switch {
		case strings.Contains(msg, "CS2POVDemosNotSupported"):
			return nil, coded("POV", "%v", err)
		case strings.Contains(msg, "UnknownSource"):
			// Unbekannte Quelle: als Valve-Demo versuchen (eigene Server, Scrims, …).
			m, err = api.AnalyzeDemo(demoPath, api.AnalyzeDemoOptions{Source: constants.DemoSourceValve})
			if err != nil {
				return nil, coded("SOURCE", "%v", err)
			}
		case strings.Contains(msg, "NotSupported"):
			return nil, coded("SOURCE", "%v", err)
		default:
			return nil, coded("CORRUPT", "%v", err)
		}
	}
	return m, nil
}

func fillMatch(out *Output, m *api.Match) {
	out.Match = MatchInfo{
		Source: string(m.Source), Mode: detectMode(m), Map: m.MapName,
		Date: m.Date.UTC().Format(time.RFC3339), Duration: round2(m.Duration.Seconds()),
		Tickrate: m.TickRate, TickCount: m.TickCount, ServerName: m.ServerName, MaxRounds: m.MaxRounds,
		Overtimes: m.OvertimeCount, Ranked: m.IsRanked, ShareCode: m.ShareCode, BuildNumber: m.BuildNumber,
		ScoreA: m.TeamA.Score, ScoreB: m.TeamB.Score,
	}
	switch {
	case m.TeamA.Score > m.TeamB.Score:
		out.Match.Winner = "A"
	case m.TeamB.Score > m.TeamA.Score:
		out.Match.Winner = "B"
	}
	startA, startB := "CT", "T"
	if len(m.Rounds) > 0 {
		startA, startB = sideStr(m.Rounds[0].TeamASide), sideStr(m.Rounds[0].TeamBSide)
	}
	for _, t := range []struct {
		team  *api.Team
		start string
	}{{m.TeamA, startA}, {m.TeamB, startB}} {
		out.Teams = append(out.Teams, TeamOut{
			Letter: string(t.team.Letter), Name: t.team.Name, Score: t.team.Score,
			ScoreFirstHalf: t.team.ScoreFirstHalf, ScoreSecondHalf: t.team.ScoreSecondHalf, StartSide: t.start,
		})
	}
}

// Modus: FACEIT nach Quelle, bei Valve über die Rangart der Spieler (11 = Premier, 6 = Wettkampf, 7 = Wingman).
func detectMode(m *api.Match) string {
	switch m.Source {
	case constants.DemoSourceFaceIt:
		return "faceit"
	case constants.DemoSourceValve:
	default:
		return "other"
	}
	count := map[int]int{}
	for _, p := range m.PlayersBySteamID {
		count[p.RankType]++
	}
	switch {
	case count[11] > 0:
		return "premier"
	case count[7] > 0:
		return "wingman"
	case count[6] > 0:
		return "competitive"
	}
	if m.GameMode == constants.GameModeCompetitive {
		return "competitive"
	}
	return "other"
}

func roundReason(r events.RoundEndReason) string {
	switch r {
	case events.RoundEndReasonTargetBombed:
		return "bomb"
	case events.RoundEndReasonBombDefused:
		return "defuse"
	case events.RoundEndReasonCTWin, events.RoundEndReasonTerroristsWin:
		return "elimination"
	case events.RoundEndReasonTargetSaved:
		return "time"
	case events.RoundEndReasonTerroristsSurrender, events.RoundEndReasonCTSurrender:
		return "surrender"
	}
	return "other"
}

func sid(id uint64) string {
	if id == 0 {
		return ""
	}
	return strconv.FormatUint(id, 10)
}

func fillKills(out *Output, m *api.Match) {
	out.Kills = make([]KillOut, 0, len(m.Kills))
	for _, k := range m.Kills {
		ko := KillOut{
			Tick: k.Tick, Round: k.RoundNumber, Killer: sid(k.KillerSteamID64), Victim: sid(k.VictimSteamID64),
			KillerSide: sideStr(k.KillerSide), VictimSide: sideStr(k.VictimSide),
			Weapon: string(k.WeaponName), WeaponType: string(k.WeaponType), Headshot: k.IsHeadshot,
			Penetrated: k.PenetratedObjects, Smoke: k.IsThroughSmoke, NoScope: k.IsNoScope && isSniper(k.WeaponName),
			KillerBlind: k.IsKillerBlinded, VictimBlind: k.IsVictimBlinded, KillerAir: k.IsKillerAirborne,
			FlashAssist: k.IsAssistedFlash, Trade: k.IsTradeKill, Traded: k.IsTradeDeath,
			TeamKill: !k.IsSuicide() && k.IsTeamKill(), Distance: round2(float64(k.Distance)),
			KX: float32(k.KillerX), KY: float32(k.KillerY), VX: float32(k.VictimX), VY: float32(k.VictimY),
		}
		if k.AssisterSteamID64 != 0 && k.AssisterSide != k.VictimSide {
			ko.Assister = sid(k.AssisterSteamID64)
		}
		out.Kills = append(out.Kills, ko)
	}
	out.Clutches = make([]ClutchOut, 0, len(m.Clutches))
	for _, c := range m.Clutches {
		out.Clutches = append(out.Clutches, ClutchOut{
			Round: c.RoundNumber, Tick: c.Tick, Player: sid(c.ClutcherSteamID64), Side: sideStr(c.Side),
			Vs: c.OpponentCount, Won: c.HasWon, Survived: c.ClutcherSurvived, Kills: c.ClutcherKillCount,
		})
	}
}

func fillRounds(out *Output, m *api.Match) {
	firstKill := map[int]int{}
	byRound := map[int][]*api.Kill{}
	index := map[*api.Kill]int{}
	for i, k := range m.Kills {
		byRound[k.RoundNumber] = append(byRound[k.RoundNumber], k)
		index[k] = i
	}
	for n, kills := range byRound {
		if i := openingIndex(kills); i >= 0 {
			firstKill[n] = index[kills[i]]
		}
	}
	bomb := map[int]*RoundOut{}
	for _, r := range m.Rounds {
		ro := RoundOut{
			N: r.Number, Start: r.StartTick, FreezeEnd: freezeEnd(r), End: r.EndTick, EndOfficial: r.EndOfficiallyTick,
			Overtime: r.OvertimeNumber, WinnerSide: sideStr(r.WinnerSide), Reason: roundReason(r.EndReason),
			ScoreA: r.TeamAScore, ScoreB: r.TeamBScore, SideA: sideStr(r.TeamASide),
			EconA: econKey(r.TeamAEconomyType), EconB: econKey(r.TeamBEconomyType),
			EquipA: r.TeamAEquipmentValue, EquipB: r.TeamBEquipmentValue,
			SpentA: r.TeamAMoneySpent, SpentB: r.TeamBMoneySpent, DurationMs: r.Duration, FirstKill: -1,
		}
		if r.WinnerSide == r.TeamASide {
			ro.Winner = "A"
		} else if r.WinnerSide == r.TeamBSide {
			ro.Winner = "B"
		}
		if i, ok := firstKill[r.Number]; ok {
			ro.FirstKill = i
		}
		out.Rounds = append(out.Rounds, ro)
	}
	for i := range out.Rounds {
		bomb[out.Rounds[i].N] = &out.Rounds[i]
	}
	// Startgeld der Teams aus der Economy der Spieler
	for _, e := range m.PlayerEconomies {
		if ro := bomb[e.RoundNumber]; ro != nil {
			if p := m.PlayersBySteamID[e.SteamID64]; p != nil && p.Team != nil {
				if p.Team.Letter == constants.TeamLetterA {
					ro.MoneyA += e.StartMoney
				} else {
					ro.MoneyB += e.StartMoney
				}
			}
		}
	}
	for _, b := range m.BombsPlanted {
		if ro := bomb[b.RoundNumber]; ro != nil {
			ro.Bomb, ro.Site, ro.Planter = "planted", b.Site, sid(b.PlanterSteamID64)
		}
	}
	for _, b := range m.BombsExploded {
		if ro := bomb[b.RoundNumber]; ro != nil {
			ro.Bomb = "exploded"
			if ro.Site == "" {
				ro.Site = b.Site
			}
		}
	}
	for _, b := range m.BombsDefused {
		if ro := bomb[b.RoundNumber]; ro != nil {
			ro.Bomb, ro.Defuser = "defused", sid(b.DefuserSteamID64)
			if ro.Site == "" {
				ro.Site = b.Site
			}
		}
	}
}

func fillPlayers(out *Output, m *api.Match, sb *statsBuilder) {
	for _, pc := range sb.players {
		p := pc.p
		r2, r1, kast, adr := ratings(pc.b.all)
		po := &PlayerOut{
			SteamID: pc.sid, Name: p.Name, Team: pc.team, Slot: p.UserID + 1,
			RankType: p.RankType, RankOld: p.OldRank, RankNew: p.Rank, Wins: p.WinCount,
			Crosshair: p.CrosshairShareCode, MVPs: p.MvpCount, Score: p.Score,
			Rating: round2(r2), Rating1: round2(r1), Kast: round2(kast), Adr: round2(adr),
			Stats: roundCounters(pc.b.all), StatsT: roundCounters(pc.b.t), StatsCT: roundCounters(pc.b.ct),
			Weapons: pc.weaponList(), DeathsBy: pc.deathsBy, Rounds: pc.roundList(), Opp: pc.opponentList(),
		}
		switch {
		case out.Match.Winner == "":
			po.Result = "draw"
		case out.Match.Winner == pc.team:
			po.Result = "win"
		default:
			po.Result = "loss"
		}
		out.Players = append(out.Players, po)
	}
	sort.Slice(out.Players, func(i, j int) bool {
		a, b := out.Players[i], out.Players[j]
		if a.Team != b.Team {
			return a.Team < b.Team
		}
		if a.Stats["kills"] != b.Stats["kills"] {
			return a.Stats["kills"] > b.Stats["kills"]
		}
		return a.SteamID < b.SteamID
	})
}
