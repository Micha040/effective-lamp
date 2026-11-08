// fragdesk-fetch – Attrappe für Steam (FRAGDESK_FETCH_FAKE=<json-datei>): feste Antworten statt Anmeldung, Web-API und
// Game Coordinator, damit sich die ganze Kette (Server ↔ Helfer ↔ fdanalyze ↔ Import) ohne Steam prüfen lässt.
// Gleiche Schnittstelle wie steam-real.js. Aufbau der Datei (alles optional):
//
// {
//   "timing": {"pollMs": 100, "loginMaxMs": 5000, "apiGapMs": 0, "gcRequestMs": 200, …},  // Zeiten wie steam-real.js
//   "record": "/tmp/aufrufe.jsonl",       // jede Anfrage an die Attrappe als JSON-Zeile (für Tests)
//   "login": {                            // Anmeldung mit Benutzername/Passwort
//     "user": "bot", "password": "pw",    // andere Angaben → „Benutzername oder Passwort falsch“
//     "error": "RateLimitExceeded",       // Anmeldung scheitert sofort mit diesem EResult
//     "actions": ["email"],               // email | app | confirm | emailconfirm; leer = ohne Steam Guard
//     "detail": "example.com",            // Domain der E-Mail-Adresse
//     "code": "ABCDE",                    // der richtige Code
//     "confirmAfterMs": 300,              // confirm/emailconfirm: so lange, bis bestätigt wird (ohne: nie)
//     "accountName": "fragbot", "steamId": "76561199000000001", "tokenDays": 200
//   },
//   "apiKey": "0123…",                    // anderer Web-API-Schlüssel → 403 „verify your key=“
//   "webapi": {                           // GetNextMatchSharingCode je SteamID des Spielers ("*" = alle anderen)
//     "76561190000000001": {
//       "chain": ["CSGO-…", "CSGO-…"],     // Match-Verlauf alt → neu; knownCode nicht darin → 412, letzter → 202
//       "authCode": "AAAA-BBBBB-CCCC",    // anderer Authentifizierungscode → 403
//       "status": 503,                    // jede Anfrage so beantworten
//       "statusAt": {"CSGO-…": 500},      // Antwort für einen bestimmten knownCode
//       "network": "ETIMEDOUT"            // keine Verbindung
//     }
//   },
//   "bot": {
//     "error": "AccessDenied",            // Anmeldung mit dem Token scheitert mit diesem EResult
//     "renewToken": true,                 // Steam erneuert das Token (→ /token)
//     "license": "granted" | "owned" | "error",
//     "playingBlocked": true,             // auf dem Konto läuft gerade ein Spiel woanders
//     "gc": "ok" | "silent" | "down"      // silent: keine Begrüßung, antwortet aber; down: antwortet gar nicht
//   },
//   "gc": {                               // Match-Infos je Freigabecode
//     "CSGO-…": {"url": "http://127.0.0.1:7711/x.dem.bz2", "matchtime": 1762000000},
//     "CSGO-…": {"url": "…", "matchtime": 1762000000, "info": true},   // dazu die .info-Datei wie bei Valve
//     "CSGO-…": {"url": null, "matchtime": 1700000000},   // keine Demo-Adresse (gelöscht)
//     "CSGO-…": "timeout" | "empty"                         // keine Antwort / leere Liste
//   }
// }
'use strict';

const {EventEmitter} = require('events');
const fs = require('fs');
const log = require('./log');
const {sleep, HttpError, decodeJwt, decodeShareCode} = require('./util');
const {EResult} = require('./texts');
const {checkToken, extractMatch} = require('./steam-real');

function eresultError(name) {
	let code = typeof name == 'number' ? name : EResult[name] || EResult.Fail;
	let err = new Error(typeof name == 'string' ? name : String(code));
	err.eresult = code;
	return err;
}

// Ein Token, das aussieht wie das von Steam (JWT mit iss/sub/aud/exp) – ohne gültige Unterschrift
function fakeToken(steamId, days) {
	let b64 = (o) => Buffer.from(JSON.stringify(o)).toString('base64').replace(/=+$/, '').replace(/\+/g, '-').replace(/\//g, '_');
	let exp = Math.floor(Date.now() / 1000) + Math.round((days || 200) * 86400);
	return [b64({typ: 'JWT', alg: 'EdDSA'}), b64({iss: 'steam', sub: steamId, aud: ['client', 'web'], exp, fake: Math.random().toString(36).slice(2)}), 'attrappe'].join('.');
}

function load(file) {
	let fx;
	try {
		fx = JSON.parse(fs.readFileSync(file, 'utf8'));
	} catch (e) {
		throw new Error(`FRAGDESK_FETCH_FAKE: ${file} ist nicht lesbar (${e.message})`);
	}
	let timing = Object.assign({
		pollMs: 100, loginMaxMs: 5000, loginStartMs: 5000, apiGapMs: 0, botLogonMs: 5000,
		gcConnectMs: 500, gcRequestMs: 200, gcGapMs: 0
	}, fx.timing || {});

	function record(what, extra) {
		if (!fx.record) {
			return;
		}
		try {
			fs.appendFileSync(fx.record, JSON.stringify(Object.assign({what, t: Date.now()}, extra || {})) + '\n');
		} catch (e) {
			log.warn(`Attrappe: ${fx.record} nicht beschreibbar (${e.message})`);
		}
	}

	// ---- Anmeldung -------------------------------------------------------------------------------------------------
	function createLogin() {
		let f = fx.login || {};
		let em = new EventEmitter();
		let timer = null;
		let done = false;
		let user = '';
		let actions = [];
		let finish = () => {
			if (!done) {
				done = true;
				em.emit('authenticated');
			}
		};
		return {
			async start(creds) {
				record('login-start', {user: creds.user, withCode: !!creds.code});
				user = creds.user;
				await sleep(5);
				if (f.error) {
					throw eresultError(f.error);
				}
				if ((f.password && creds.password !== f.password) || (f.user && creds.user !== f.user)) {
					throw eresultError('InvalidPassword');
				}
				let map = {email: {type: 2, detail: f.detail || 'example.com'}, app: {type: 3}, confirm: {type: 4}, emailconfirm: {type: 5}};
				actions = (f.actions || []).map((a) => map[a]).filter(Boolean);
				if (actions.length == 0 || (creds.code && creds.code === f.code)) {
					setImmediate(finish);
					return {actionRequired: false};
				}
				if (actions.some((a) => a.type == 4 || a.type == 5) && typeof f.confirmAfterMs == 'number') {
					timer = setTimeout(finish, f.confirmAfterMs);
				}
				return {actionRequired: true, validActions: actions};
			},
			async submitCode(code) {
				record('login-code', {right: code === f.code});
				await sleep(5);
				if (code !== f.code) {
					throw eresultError(actions.some((a) => a.type == 2) ? 'InvalidLoginAuthCode' : 'TwoFactorCodeMismatch');
				}
				setImmediate(finish);
			},
			on(event, fn) {
				em.on(event, fn);
			},
			result() {
				let steamId = f.steamId || '76561199000000001';
				return {refreshToken: fakeToken(steamId, f.tokenDays), accountName: f.accountName || user, steamId};
			},
			cancel() {
				clearTimeout(timer);
			}
		};
	}

	// ---- Web-API -----------------------------------------------------------------------------------------------------
	async function webapi({apiKey, steamId, authCode, knownCode}) {
		record('webapi', {steamId, knownCode});
		await sleep(1);
		let w = fx.webapi || {};
		if (fx.apiKey && apiKey !== fx.apiKey) {
			return {status: 403, body: Buffer.from('<html><head><title>Forbidden</title></head><body><h1>Forbidden</h1>Access is denied. Retrying will not help. Please verify your <pre>key=</pre> parameter.</body></html>')};
		}
		let acc = w[steamId] || w['*'];
		if (!acc) {
			return {status: 403, body: Buffer.alloc(0)};
		}
		if (acc.network) {
			throw new HttpError('Attrappe: keine Verbindung', acc.network);
		}
		if (acc.status) {
			return {status: acc.status, body: Buffer.alloc(0)};
		}
		if (acc.authCode && authCode !== acc.authCode) {
			return {status: 403, body: Buffer.alloc(0)};
		}
		if (acc.statusAt && acc.statusAt[knownCode]) {
			return {status: acc.statusAt[knownCode], body: Buffer.alloc(0)};
		}
		let chain = acc.chain || [];
		let i = chain.indexOf(knownCode);
		if (i < 0) {
			return {status: 412, body: Buffer.alloc(0)};
		}
		if (i == chain.length - 1) {
			return {status: 202, body: Buffer.from('{"result":{"nextcode":"n/a"}}')};
		}
		return {status: 200, body: Buffer.from(JSON.stringify({result: {nextcode: chain[i + 1]}}))};
	}

	// ---- Bot und Game Coordinator --------------------------------------------------------------------------------------
	async function openBot({refreshToken, onToken}) {
		let b = fx.bot || {};
		record('bot-logon');
		await sleep(5);
		if (b.error) {
			throw eresultError(b.error);
		}
		let jwt = checkToken(refreshToken);
		if (b.renewToken) {
			setImmediate(() => onToken(fakeToken(jwt.sub, 200)));
		}
		return {
			steamId: jwt.sub,
			get playingBlocked() {
				return !!b.playingBlocked;
			},
			lost: false,
			lastError: () => null,
			async waitPlayingState() {
				return {blocked: !!b.playingBlocked, appid: b.playingBlocked ? 730 : 0};
			},
			async ensureLicense() {
				record('license');
				if (b.license == 'error') {
					throw new Error('Request timed out');
				}
				return b.license == 'granted' ? 'granted' : 'owned';
			},
			async connectGC() {
				record('gc-connect');
				await sleep(5);
				return !b.gc || b.gc == 'ok';
			},
			async requestMatch(code) {
				record('gc-request', {code});
				let g = (fx.gc || {})[code];
				if (b.gc == 'down' || g == null || g === 'timeout') {
					await sleep(timing.gcRequestMs * 2);
					return {status: 'timeout'};
				}
				await sleep(2);
				if (g === 'empty') {
					return {status: 'empty'};
				}
				if (!g.url) {
					return {status: 'nourl', matchtime: g.matchtime || 0};
				}
				if (g.info) {   // .info wie vom echten Game Coordinator (gleicher Code wie steam-real.js)
					let d = decodeShareCode(code);
					let Protos = require('globaloffensive/protobufs/generated/_load.js');
					return extractMatch(Protos, {
						matchid: d.matchId, matchtime: g.matchtime || 0, watchablematchinfo: {tv_port: Number(d.token)},
						roundstatsall: [{reservationid: d.outcomeId, map: g.url}]
					});
				}
				return {status: 'ok', url: g.url, matchtime: g.matchtime || 0, info: null};
			},
			async close() {
				record('bot-logoff');
			}
		};
	}

	return {name: 'Attrappe', timing, createLogin, webapi, openBot, checkToken, fakeToken};
}

module.exports = {load, fakeToken, decodeJwt};
