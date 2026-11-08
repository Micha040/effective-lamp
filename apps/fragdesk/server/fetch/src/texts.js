// fragdesk-fetch – deutsche Texte für Fehler von Steam, Valve, fdanalyze und dem eigenen Server. Diese Sätze landen
// in der Handy-App (lastError, Anmeldung des Bots) und im Protokoll – ohne Geheimnisse.
'use strict';

// EResult-Werte von Steam (Auszug, Zahlen wie in steam-user/steam-session)
const EResult = {
	OK: 1, Fail: 2, NoConnection: 3, InvalidPassword: 5, LoggedInElsewhere: 6, InvalidParam: 8, Busy: 10,
	InvalidState: 11, AccessDenied: 15, Timeout: 16, Banned: 17, AccountNotFound: 18, ServiceUnavailable: 20,
	NotLoggedOn: 21, Pending: 22, LimitExceeded: 25, Revoked: 26, Expired: 27, AlreadyRedeemed: 28,
	AccountDisabled: 43, AlreadyLoggedInElsewhere: 50, Suspended: 51, InvalidLoginAuthCode: 65,
	AccountLogonDeniedNoMail: 66, ExpiredLoginAuthCode: 71, AccountLockedDown: 73, RateLimitExceeded: 84,
	AccountLoginDeniedNeedTwoFactor: 85, AccountLoginDeniedThrottle: 87, TwoFactorCodeMismatch: 88,
	TwoFactorActivationCodeMismatch: 89, InvalidSignature: 121, TryAnotherCM: 48
};
const ENames = Object.fromEntries(Object.entries(EResult).map(([k, v]) => [v, k]));

function eresultOf(err) {
	if (!err) {
		return 0;
	}
	if (typeof err.eresult == 'number') {
		return err.eresult;
	}
	let byName = EResult[String(err.message || '').trim()];
	return byName || 0;
}

function eresultName(code) {
	return ENames[code] || (code ? 'EResult ' + code : 'unbekannt');
}

// ---- Anmeldung des Bots mit Benutzername und Passwort (steam-session) ----------------------------------------------
const LOGIN = {
	wrongPassword: 'Benutzername oder Passwort falsch.',
	wrongCode: 'Code falsch.',
	wrongCodeAgain: 'Code falsch – bitte den neuen Code eingeben.',
	codeExpired: 'Der Code ist abgelaufen – bitte den neuen Code eingeben.',
	tooMany: 'Zu viele Versuche – in einer Stunde nochmal.',
	timeout: 'Zeit abgelaufen – bitte noch einmal anmelden.',
	unreachable: 'Steam ist gerade nicht erreichbar – später noch einmal versuchen.',
	locked: 'Das Steam-Konto ist gesperrt oder deaktiviert.',
	working: 'Melde bei Steam an …',
	checkingCode: 'Prüfe den Code …'
};

function loginErrorText(err) {
	let r = eresultOf(err);
	switch (r) {
		case EResult.InvalidPassword:
		case EResult.AccountNotFound:
		case EResult.InvalidParam:
			return LOGIN.wrongPassword;
		case EResult.InvalidLoginAuthCode:
		case EResult.TwoFactorCodeMismatch:
			return LOGIN.wrongCode;
		case EResult.ExpiredLoginAuthCode:
			return LOGIN.codeExpired;
		case EResult.RateLimitExceeded:
		case EResult.AccountLoginDeniedThrottle:
		case EResult.LimitExceeded:
			return LOGIN.tooMany;
		case EResult.AccountDisabled:
		case EResult.AccountLockedDown:
		case EResult.Banned:
		case EResult.Suspended:
			return LOGIN.locked;
		case EResult.Timeout:
			return LOGIN.timeout;
		case EResult.NoConnection:
		case EResult.ServiceUnavailable:
		case EResult.TryAnotherCM:
		case EResult.Busy:
		case EResult.Fail:
			return LOGIN.unreachable;
	}
	if (err && (err.network || err.timeout || /ECONN|ETIMEDOUT|ENOTFOUND|EAI_AGAIN|socket|WebSocket|No Steam servers/i.test(String(err.message)))) {
		return LOGIN.unreachable;
	}
	return `Anmeldung bei Steam fehlgeschlagen (${r ? eresultName(r) : String((err && err.message) || err).slice(0, 120)}).`;
}

// ---- Anmeldung des Bots mit dem gespeicherten Token (steam-user) ---------------------------------------------------
// tokenGone: das Token gilt nicht mehr – der Admin muss den Bot neu anmelden (nicht nur ein vorübergehender Fehler)
function botErrorText(err) {
	let r = eresultOf(err);
	switch (r) {
		case EResult.InvalidPassword:
		case EResult.AccessDenied:
		case EResult.Revoked:
		case EResult.Expired:
		case EResult.InvalidSignature:
		case EResult.NotLoggedOn:
			return {text: 'Die Anmeldung des Steam-Bots gilt nicht mehr (Passwort geändert oder abgemeldet?) – der Admin muss den Bot neu anmelden.', tokenGone: true};
		case EResult.AccountDisabled:
		case EResult.AccountLockedDown:
		case EResult.Banned:
		case EResult.Suspended:
			return {text: 'Das Steam-Konto des Bots ist gesperrt oder deaktiviert.', tokenGone: true};
		case EResult.RateLimitExceeded:
		case EResult.AccountLoginDeniedThrottle:
			return {text: 'Steam lässt den Bot gerade nicht anmelden (zu viele Anmeldungen) – nächster Versuch beim nächsten Lauf.'};
		case EResult.LoggedInElsewhere:
		case EResult.AlreadyLoggedInElsewhere:
			return {text: 'Der Steam-Bot wurde abgemeldet, weil sich das Konto woanders angemeldet hat – nächster Versuch beim nächsten Lauf.'};
	}
	if (err && err.badToken) {
		return {text: 'Das gespeicherte Token des Steam-Bots ist ungültig – der Admin muss den Bot neu anmelden.', tokenGone: true};
	}
	if (err && err.timeout) {
		return {text: 'Steam antwortet nicht auf die Anmeldung des Bots – nächster Versuch beim nächsten Lauf.'};
	}
	return {text: `Steam ist gerade nicht erreichbar (${r ? eresultName(r) : String((err && err.message) || err).slice(0, 100)}) – nächster Versuch beim nächsten Lauf.`};
}

// ---- fdanalyze --------------------------------------------------------------------------------------------------------
const ANALYZER = {
	NOT_FOUND: 'Die Demo-Datei fehlt',
	READ: 'Die Demo lässt sich nicht lesen',
	WRITE: 'Die Auswertung lässt sich nicht speichern (Platte voll?)',
	FORMAT: 'Die Datei ist keine CS2-Demo',
	CSGO: 'Das ist eine CS:GO-Demo, keine CS2-Demo',
	UNPACK: 'Die Demo lässt sich nicht entpacken (Download unvollständig?)',
	POV: 'POV-Demos werden nicht unterstützt',
	SOURCE: 'Die Demo kommt aus einer unbekannten Quelle',
	CORRUPT: 'Die Demo ist beschädigt',
	EMPTY: 'Die Demo enthält keine vollständige Runde',
	INTERNAL: 'Interner Fehler bei der Auswertung'
};

// Für das Protokoll: was die Bibliothek wirklich gemeldet hat (EResult und Text), z. B. „EResult 5 InvalidPassword“
function rawReason(err) {
	if (!err) {
		return 'unbekannt';
	}
	let r = eresultOf(err);
	let msg = String(err.message || err).replace(/\s+/g, ' ').slice(0, 200);
	return r ? `EResult ${r} ${eresultName(r)}${msg && msg != eresultName(r) ? ': ' + msg : ''}` : msg;
}

module.exports = {EResult, eresultOf, eresultName, LOGIN, loginErrorText, botErrorText, ANALYZER, rawReason};
