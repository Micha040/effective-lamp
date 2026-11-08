// fragdesk-fetch – Steam-Web-API ICSGOPlayers_730/GetNextMatchSharingCode/v1: liefert zum letzten bekannten
// Freigabecode eines Spielers den nächsten (key = Web-API-Schlüssel des Bots, steamidkey = Authentifizierungscode).
// Antworten laut Valve (developer.valvesoftware.com/wiki/Counter-Strike:_Global_Offensive_Access_Match_History):
//   200 {"result":{"nextcode":"CSGO-…"}}   nächster Code      202 / "n/a"   nichts Neues
//   403 Authentifizierungscode passt nicht  412 Freigabecode passt nicht (nicht weiter fragen, bis er sich ändert)
//   429 / 503 zu viele Anfragen             500 / 504 Steam hat es nicht geschafft (später wieder)
// Ein ungültiger Web-API-Schlüssel ergibt ebenfalls 403, aber mit einer HTML-Seite „… verify your key= parameter“.
'use strict';

const log = require('./log');
const {request, sleep, netText, SHARE_RE} = require('./util');

const URL_BASE = 'https://api.steampowered.com/ICSGOPlayers_730/GetNextMatchSharingCode/v1/';

// scope: 'account' = nur dieses Konto (Angaben falsch → nicht weiter fragen, bis sie sich ändern),
//        'key' = Schlüssel des Bots ungültig (alle Konten), 'global' = Steam gerade nicht erreichbar/begrenzt (alle Konten),
//        'retry' = nur diese Anfrage (nächster Lauf)
const TEXT = {
	auth: 'Authentifizierungscode und SteamID passen nicht zusammen – bitte den Authentifizierungscode in der Handy-App prüfen.',
	known: 'Der letzte bekannte Freigabecode gehört nicht zu diesem Steam-Konto oder ist ungültig – bitte den neuesten Freigabecode in der Handy-App eintragen.',
	key: 'Der Web-API-Schlüssel des Steam-Bots gilt nicht – der Admin muss ihn neu eintragen.',
	limit: 'Steam begrenzt gerade die Anfragen – nächster Versuch beim nächsten Lauf.',
	down: 'Die Steam-Web-API ist gerade nicht erreichbar',
	odd: 'Unerwartete Antwort der Steam-Web-API'
};

// Eine Antwort deuten → {kind: 'next', code} | {kind: 'none'} | {kind: 'error', status, scope, text}
function interpret(status, body) {
	let text = Buffer.isBuffer(body) ? body.toString('utf8') : String(body || '');
	let nextcode = null;
	try {
		let v = JSON.parse(text);
		nextcode = v && v.result && typeof v.result.nextcode == 'string' ? v.result.nextcode : null;
	} catch (e) {
		nextcode = null;
	}
	if (status == 200 || status == 202) {
		if (status == 202 || nextcode == 'n/a') {
			return {kind: 'none'};
		}
		if (nextcode && SHARE_RE.test(nextcode)) {
			return {kind: 'next', code: nextcode};
		}
		return {kind: 'error', status, scope: 'retry', text: `${TEXT.odd} (${status}, ${JSON.stringify(text.slice(0, 80))}).`};
	}
	if (status == 401 || (status == 403 && /key=|verify your/i.test(text))) {
		return {kind: 'error', status, scope: 'key', text: TEXT.key};
	}
	if (status == 403) {
		return {kind: 'error', status, scope: 'account', text: TEXT.auth};
	}
	if (status == 412) {
		return {kind: 'error', status, scope: 'account', text: TEXT.known};
	}
	if (status == 429) {
		return {kind: 'error', status, scope: 'global', text: TEXT.limit};
	}
	if (status == 503) {
		return {kind: 'error', status, scope: 'global', text: `${TEXT.down} (503) – nächster Versuch beim nächsten Lauf.`};
	}
	if (status >= 500) {
		return {kind: 'error', status, scope: 'retry', text: `Steam hat die Anfrage nicht beantwortet (${status}) – nächster Versuch beim nächsten Lauf.`};
	}
	return {kind: 'error', status, scope: 'retry', text: `${TEXT.odd} (Status ${status}).`};
}

// Die echte Anfrage über HTTPS → {status, body}; Netzwerkfehler werfen
function realTransport({apiKey, steamId, authCode, knownCode}) {
	let qs = new URLSearchParams({key: apiKey, steamid: steamId, steamidkey: authCode, knowncode: knownCode});
	return request(URL_BASE + '?' + qs.toString(), {timeoutMs: 20000, maxBytes: 1 << 20, headers: {'User-Agent': 'fragdesk-fetch'}});
}

// Eine Anfrage → wie interpret(); Netzwerkfehler werden zu {kind: 'error', scope: 'global'}
async function nextCode(transport, params) {
	let res;
	try {
		res = await transport(params);
	} catch (err) {
		if (err.cancelled) {
			throw err;
		}
		return {kind: 'error', status: 0, scope: 'global', text: `${TEXT.down} (${netText(err)}) – nächster Versuch beim nächsten Lauf.`};
	}
	return interpret(res.status, res.body);
}

// Alle neuen Codes eines Kontos ab knownCode (höchstens max, gapMs Abstand) → {codes, error}
// error (falls die Kette unterwegs abbricht) wie bei interpret(); bis dahin gefundene Codes bleiben gültig.
async function walk(transport, {apiKey, steamId, authCode, knownCode}, {max = 10, gapMs = 1000} = {}) {
	let codes = [];
	let known = knownCode;
	let seen = new Set([knownCode]);
	for (let i = 0; i < max; i++) {
		if (i > 0 && gapMs > 0) {
			await sleep(gapMs);
		}
		let r = await nextCode(transport, {apiKey, steamId, authCode, knownCode: known});
		if (r.kind == 'none') {
			return {codes, error: null};
		}
		if (r.kind == 'error') {
			log.debug(`Web-API: Status ${r.status} nach ${codes.length} neuen Codes`);
			return {codes, error: r};
		}
		if (seen.has(r.code)) {   // Steam im Kreis? Lieber aufhören.
			return {codes, error: null};
		}
		seen.add(r.code);
		codes.push(r.code);
		known = r.code;
	}
	return {codes, error: null, more: true};
}

module.exports = {interpret, nextCode, walk, realTransport, TEXT, URL_BASE};
