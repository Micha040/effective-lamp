package main

import "github.com/akiver/cs-demo-analyzer/pkg/api/constants"

// Waffen mit Projektil, für Treffsicherheit und Treffer nach Körperzonen.
func isBulletWeapon(t constants.WeaponType) bool {
	switch t {
	case constants.WeaponTypePistol, constants.WeaponTypeSMG, constants.WeaponTypeShotgun,
		constants.WeaponTypeRifle, constants.WeaponTypeSniper, constants.WeaponTypeMachineGun:
		return true
	}
	return false
}

// Schüsse tragen nur den Waffennamen; die Art steht in dieser Tabelle.
var weaponTypes = func() map[constants.WeaponName]constants.WeaponType {
	m := map[constants.WeaponName]constants.WeaponType{}
	set := func(t constants.WeaponType, names ...constants.WeaponName) {
		for _, n := range names {
			m[n] = t
		}
	}
	set(constants.WeaponTypePistol, constants.WeaponGlock, constants.WeaponUSP, constants.WeaponP2000, constants.WeaponP250,
		constants.WeaponFiveSeven, constants.WeaponTec9, constants.WeaponCZ75, constants.WeaponDualBerettas,
		constants.WeaponDeagle, constants.WeaponRevolver)
	set(constants.WeaponTypeSMG, constants.WeaponMac10, constants.WeaponMP9, constants.WeaponMP7, constants.WeaponMP5,
		constants.WeaponUMP45, constants.WeaponP90, constants.WeaponPPBizon)
	set(constants.WeaponTypeShotgun, constants.WeaponNova, constants.WeaponXM1014, constants.WeaponMAG7, constants.WeaponSawedOff)
	set(constants.WeaponTypeMachineGun, constants.WeaponM249, constants.WeaponNegev)
	set(constants.WeaponTypeRifle, constants.WeaponAK47, constants.WeaponM4A4, constants.WeaponM4A1, constants.WeaponGalilAR,
		constants.WeaponFamas, constants.WeaponSG553, constants.WeaponAUG)
	set(constants.WeaponTypeSniper, constants.WeaponAWP, constants.WeaponScout, constants.WeaponScar20, constants.WeaponG3SG1)
	set(constants.WeaponTypeGrenade, constants.WeaponFlashbang, constants.WeaponSmoke, constants.WeaponHEGrenade,
		constants.WeaponMolotov, constants.WeaponIncendiary, constants.WeaponDecoy)
	set(constants.WeaponTypeMelee, constants.WeaponKnife)
	set(constants.WeaponTypeEquipment, constants.WeaponZeus, constants.WeaponBomb, constants.WeaponKevlar,
		constants.WeaponHelmet, constants.WeaponDefuseKit)
	set(constants.WeaponTypeWorld, constants.WeaponWorld)
	return m
}()

func weaponTypeOf(name constants.WeaponName) constants.WeaponType {
	if t, ok := weaponTypes[name]; ok {
		return t
	}
	return constants.WeaponTypeUnknown
}

func isSniper(name constants.WeaponName) bool {
	return name == constants.WeaponAWP || name == constants.WeaponScout ||
		name == constants.WeaponScar20 || name == constants.WeaponG3SG1
}

// Höchstgeschwindigkeit (Einheiten/s) mit der Waffe in der Hand. Unter 34 % davon schießt sie genau.
var maxSpeed = map[constants.WeaponName]float64{
	constants.WeaponGlock: 240, constants.WeaponUSP: 240, constants.WeaponP2000: 240, constants.WeaponP250: 240,
	constants.WeaponFiveSeven: 240, constants.WeaponTec9: 240, constants.WeaponCZ75: 240,
	constants.WeaponDualBerettas: 240, constants.WeaponDeagle: 230, constants.WeaponRevolver: 220,
	constants.WeaponMac10: 240, constants.WeaponMP9: 240, constants.WeaponMP7: 220, constants.WeaponMP5: 235,
	constants.WeaponUMP45: 230, constants.WeaponP90: 230, constants.WeaponPPBizon: 240,
	constants.WeaponAK47: 215, constants.WeaponM4A4: 225, constants.WeaponM4A1: 225, constants.WeaponGalilAR: 215,
	constants.WeaponFamas: 220, constants.WeaponSG553: 210, constants.WeaponAUG: 220,
	constants.WeaponAWP: 200, constants.WeaponScout: 230, constants.WeaponScar20: 215, constants.WeaponG3SG1: 215,
	constants.WeaponNova: 220, constants.WeaponXM1014: 215, constants.WeaponMAG7: 225, constants.WeaponSawedOff: 210,
	constants.WeaponM249: 195, constants.WeaponNegev: 150,
}

func accurateSpeed(name constants.WeaponName) float64 {
	if s, ok := maxSpeed[name]; ok {
		return 0.34 * s
	}
	return 0.34 * 230
}

// Kill-Kategorie für die Zähler kRifle, kSniper, …
func killCategory(t constants.WeaponType, name constants.WeaponName) string {
	switch t {
	case constants.WeaponTypeRifle:
		return "kRifle"
	case constants.WeaponTypeSniper:
		return "kSniper"
	case constants.WeaponTypeSMG:
		return "kSmg"
	case constants.WeaponTypeShotgun:
		return "kShotgun"
	case constants.WeaponTypeMachineGun:
		return "kMg"
	case constants.WeaponTypePistol:
		return "kPistol"
	case constants.WeaponTypeMelee:
		return "kKnife"
	case constants.WeaponTypeGrenade:
		return "kGrenade"
	}
	if name == constants.WeaponZeus {
		return "kZeus"
	}
	return "kOther"
}

// Zähler für geworfene Granaten.
func throwKey(name constants.WeaponName) string {
	switch name {
	case constants.WeaponFlashbang:
		return "flashThrown"
	case constants.WeaponSmoke:
		return "smokeThrown"
	case constants.WeaponHEGrenade:
		return "heThrown"
	case constants.WeaponMolotov, constants.WeaponIncendiary:
		return "fireThrown"
	case constants.WeaponDecoy:
		return "decoyThrown"
	}
	return ""
}

func isFire(name constants.WeaponName) bool {
	return name == constants.WeaponMolotov || name == constants.WeaponIncendiary
}
