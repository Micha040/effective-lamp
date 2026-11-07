package main

// Ausgabeformat von fdanalyze (Schema 1). Die App liest es in src/core/match.cpp.
//
// Zähler ("stats", "statsT", "statsCT", Waffen) sind frei benannte Zahlen: Die App summiert sie über
// beliebig viele Matches auf. Schlüssel, die auf "Max" enden, werden stattdessen über das Maximum
// zusammengefasst. Bedeutung der einzelnen Zähler: siehe stats.go.

const schemaVersion = 1

type Counters map[string]float64

type Output struct {
	Schema     int          `json:"schema"`
	Analyzer   string       `json:"analyzer"`
	Csda       string       `json:"csda"`
	Checksum   string       `json:"checksum"`
	Demo       DemoInfo     `json:"demo"`
	Match      MatchInfo    `json:"match"`
	Teams      []TeamOut    `json:"teams"`
	Rounds     []RoundOut   `json:"rounds"`
	Players    []*PlayerOut `json:"players"`
	Kills      []KillOut    `json:"kills"`
	Clutches   []ClutchOut  `json:"clutches"`
	Highlights []Highlight  `json:"highlights"`
	Warnings   []string     `json:"warnings,omitempty"`
}

type DemoInfo struct {
	Path     string `json:"path"`
	File     string `json:"file"`
	Size     int64  `json:"size"`
	Modified string `json:"modified"`
	Packed   string `json:"packed,omitempty"` // gz, bz2, zst
}

type MatchInfo struct {
	Source      string  `json:"source"` // valve, faceit, esea, …
	Mode        string  `json:"mode"`   // premier, competitive, wingman, faceit, other
	Map         string  `json:"map"`
	Date        string  `json:"date"` // RFC 3339, UTC
	Duration    float64 `json:"duration"`
	Tickrate    float64 `json:"tickrate"`
	TickCount   int     `json:"tickCount"`
	ServerName  string  `json:"serverName"`
	MaxRounds   int     `json:"maxRounds"`
	Overtimes   int     `json:"overtimes"`
	Ranked      bool    `json:"ranked,omitempty"`
	ShareCode   string  `json:"shareCode,omitempty"`
	BuildNumber int     `json:"buildNumber,omitempty"`
	ScoreA      int     `json:"scoreA"`
	ScoreB      int     `json:"scoreB"`
	Winner      string  `json:"winner"` // "A", "B" oder "" (Unentschieden)
}

type TeamOut struct {
	Letter          string `json:"letter"`
	Name            string `json:"name"`
	Score           int    `json:"score"`
	ScoreFirstHalf  int    `json:"scoreFirstHalf"`
	ScoreSecondHalf int    `json:"scoreSecondHalf"`
	StartSide       string `json:"startSide"`
}

type RoundOut struct {
	N           int    `json:"n"`
	Start       int    `json:"start"`
	FreezeEnd   int    `json:"freezeEnd"`
	End         int    `json:"end"`
	EndOfficial int    `json:"endOfficial"`
	Overtime    int    `json:"overtime,omitempty"`
	Winner      string `json:"winner"`     // A/B
	WinnerSide  string `json:"winnerSide"` // T/CT
	Reason      string `json:"reason"`     // bomb, defuse, elimination, time, surrender, other
	ScoreA      int    `json:"scoreA"`
	ScoreB      int    `json:"scoreB"`
	SideA       string `json:"sideA"`
	EconA       string `json:"econA"`
	EconB       string `json:"econB"`
	EquipA      int    `json:"equipA"`
	EquipB      int    `json:"equipB"`
	SpentA      int    `json:"spentA"`
	SpentB      int    `json:"spentB"`
	MoneyA      int    `json:"moneyA"`
	MoneyB      int    `json:"moneyB"`
	DurationMs  int64  `json:"durationMs"`
	Bomb        string `json:"bomb,omitempty"` // planted, exploded, defused
	Site        string `json:"site,omitempty"`
	Planter     string `json:"planter,omitempty"`
	Defuser     string `json:"defuser,omitempty"`
	FirstKill   int    `json:"firstKill"` // Index in kills oder -1
}

type PlayerOut struct {
	SteamID   string  `json:"steamId"`
	Name      string  `json:"name"`
	Team      string  `json:"team"` // A/B
	Slot      int     `json:"slot"` // für spec_player
	Result    string  `json:"result"`
	RankType  int     `json:"rankType"`
	RankOld   int     `json:"rankOld"`
	RankNew   int     `json:"rankNew"`
	Wins      int     `json:"wins"`
	Crosshair string  `json:"crosshair,omitempty"`
	MVPs      int     `json:"mvps"`
	Score     int     `json:"score"`
	Rating    float64 `json:"rating"`  // HLTV 2.0 (Näherung)
	Rating1   float64 `json:"rating1"` // HLTV 1.0
	Kast      float64 `json:"kast"`
	Adr       float64 `json:"adr"`

	Stats    Counters       `json:"stats"`
	StatsT   Counters       `json:"statsT"`
	StatsCT  Counters       `json:"statsCT"`
	Weapons  []WeaponOut    `json:"weapons"`
	DeathsBy map[string]int `json:"deathsBy"`
	Rounds   []PlayerRound  `json:"rounds"`
	Opp      []OpponentOut  `json:"opponents"`
}

type WeaponOut struct {
	Name  string   `json:"name"`
	Type  string   `json:"type"`
	Stats Counters `json:"stats"`
}

type PlayerRound struct {
	N            int     `json:"n"`
	Side         string  `json:"side"`
	Won          bool    `json:"won,omitempty"`
	Kills        int     `json:"k,omitempty"`
	HsKills      int     `json:"hs,omitempty"`
	Assists      int     `json:"a,omitempty"`
	FlashAssists int     `json:"fa,omitempty"`
	Damage       int     `json:"dmg,omitempty"`
	UtilDamage   int     `json:"util,omitempty"`
	Dead         bool    `json:"dead,omitempty"`
	DeathTick    int     `json:"deathTick,omitempty"`
	Killer       string  `json:"killer,omitempty"`
	KillerWeapon string  `json:"killerWeapon,omitempty"`
	Traded       bool    `json:"traded,omitempty"`
	Kast         bool    `json:"kast,omitempty"`
	Open         string  `json:"open,omitempty"` // K = Eröffnungskill, D = Eröffnungstod
	Clutch       int     `json:"clutch,omitempty"`
	ClutchWon    bool    `json:"clutchWon,omitempty"`
	Equip        int     `json:"equip"`
	Spent        int     `json:"spent"`
	Money        int     `json:"money"`
	Econ         string  `json:"econ"`
	Flashed      int     `json:"flashed,omitempty"` // geblendete Gegner
	Plant        bool    `json:"plant,omitempty"`
	Defuse       bool    `json:"defuse,omitempty"`
	FirstKillT   int     `json:"firstKillTick,omitempty"`
	AliveSec     float64 `json:"alive"`
}

type OpponentOut struct {
	SteamID    string `json:"steamId"`
	Kills      int    `json:"k"`
	Deaths     int    `json:"d"`
	DmgDealt   int    `json:"dmgDealt"`
	DmgTaken   int    `json:"dmgTaken"`
	OpenKills  int    `json:"openK"`
	OpenDeaths int    `json:"openD"`
}

type KillOut struct {
	Tick        int     `json:"t"`
	Round       int     `json:"r"`
	Killer      string  `json:"k"`
	Victim      string  `json:"v"`
	Assister    string  `json:"a,omitempty"`
	KillerSide  string  `json:"ks"`
	VictimSide  string  `json:"vs"`
	Weapon      string  `json:"w"`
	WeaponType  string  `json:"wt"`
	Headshot    bool    `json:"hs,omitempty"`
	Penetrated  int     `json:"pen,omitempty"`
	Smoke       bool    `json:"smoke,omitempty"`
	NoScope     bool    `json:"noscope,omitempty"`
	KillerBlind bool    `json:"kBlind,omitempty"`
	VictimBlind bool    `json:"vBlind,omitempty"`
	KillerAir   bool    `json:"kAir,omitempty"`
	FlashAssist bool    `json:"fa,omitempty"`
	Trade       bool    `json:"trade,omitempty"`
	Traded      bool    `json:"traded,omitempty"`
	TeamKill    bool    `json:"tk,omitempty"`
	Distance    float64 `json:"dist"`
	KX          float32 `json:"kx"`
	KY          float32 `json:"ky"`
	VX          float32 `json:"vx"`
	VY          float32 `json:"vy"`
}

type ClutchOut struct {
	Round    int    `json:"r"`
	Tick     int    `json:"t"`
	Player   string `json:"p"`
	Side     string `json:"side"`
	Vs       int    `json:"vs"`
	Won      bool   `json:"won"`
	Survived bool   `json:"survived"`
	Kills    int    `json:"kills"`
}

// Ein vorgeschlagener Moment für ein Highlight-Video. Segmente sind Tick-Bereiche, die nacheinander
// aufgenommen und zu einem Clip zusammengefügt werden.
type Highlight struct {
	ID        string   `json:"id"`
	Player    string   `json:"p"`
	Round     int      `json:"r"`
	Side      string   `json:"side"`
	Score     int      `json:"score"`
	Kills     int      `json:"kills"`
	Clutch    int      `json:"clutch,omitempty"` // 1vX gewonnen
	Tags      []string `json:"tags"`
	Weapons   []string `json:"weapons"`
	Segments  [][2]int `json:"segments"`
	KillTicks []int    `json:"killTicks"`
}
