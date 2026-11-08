// fragdesk-fetch – der echte Weg zu Steam (die Attrappe für Tests steht in steam-fake.js, gleiche Schnittstelle):
//   createLogin()  Anmeldung des Bots mit Benutzername/Passwort (+ Steam Guard) über steam-session (SteamClient)
//   webapi         GetNextMatchSharingCode über HTTPS (webapi.js)
//   openBot()      Bot mit dem Refresh-Token anmelden (steam-user), CS2-Lizenz, Game Coordinator (globaloffensive),
//                  Match-Infos per Freigabecode (MatchListRequestFullGameInfo) → Demo-Adresse
// Die Steam-Bibliotheken werden erst hier geladen (require bei Bedarf): Läufe ohne neue Matches brauchen sie nicht
// und bleiben klein (≈ 40 statt ≈ 170 MB Arbeitsspeicher).
'use strict';

const log = require('./log');
const webapi = require('./webapi');
const {withTimeout, sleep, decodeJwt, decodeShareCode, abort} = require('./util');
const {EResult} = require('./texts');

const APP_CS2 = 730;

const timing = {
	pollMs: 3000,          // Auftrag abfragen, während die Anmeldung auf Code/Bestätigung wartet
	loginMaxMs: 600000,    // so lange auf Code/Bestätigung warten
	loginStartMs: 60000,   // Anmeldung beginnen (Verbindung zu Steam, Passwort prüfen)
	apiGapMs: 1000,        // Abstand zwischen Anfragen an die Web-API
	botLogonMs: 60000,     // Bot mit Token anmelden
	gcConnectMs: 45000,    // auf die Begrüßung des Game Coordinators warten
	gcRequestMs: 20000,    // je Versuch auf die Match-Infos warten (2 Versuche je Code)
	gcGapMs: 1000          // Abstand zwischen Anfragen an den Game Coordinator
};

// ---- Anmeldung mit Benutzername und Passwort -------------------------------------------------------------------------
function createLogin() {
	const {LoginSession, EAuthTokenPlatformType} = require('steam-session');
	let session = new LoginSession(EAuthTokenPlatformType.SteamClient);
	// läuft ab, sobald steam-session auf eine Bestätigung wartet – wir warten selbst höchstens loginMaxMs
	session.loginTimeout = timing.loginMaxMs + 60000;
	return {
		start({user, password, code}) {
			return session.startWithCredentials({accountName: user, password, steamGuardCode: code || undefined});
		},
		submitCode(code) {
			return session.submitSteamGuardCode(code);
		},
		on(event, fn) {   // authenticated, timeout, error
			session.on(event, fn);
		},
		result() {
			return {
				refreshToken: session.refreshToken,
				accountName: session.accountName,
				steamId: session.steamID ? session.steamID.getSteamID64() : ''
			};
		},
		cancel() {
			try {
				session.cancelLoginAttempt();
			} catch (e) {
				// war schon zu
			}
		}
	};
}

// ---- Bot mit Refresh-Token ------------------------------------------------------------------------------------------
// Prüft das Token vorher selbst: steam-user würde ein kaputtes Token erst im nächsten Tick ablehnen – als
// unbehandelter Fehler, der das ganze Programm beendet.
function checkToken(token) {
	let jwt = decodeJwt(token);
	if (!jwt || jwt.iss != 'steam' || !jwt.sub || !(Array.isArray(jwt.aud) ? jwt.aud : [jwt.aud]).includes('client')) {
		let err = new Error('Das Token ist kein Refresh-Token für den Steam-Client');
		err.badToken = true;
		throw err;
	}
	return jwt;
}

// Match-Infos des Game Coordinators (wie globaloffensive sie dekodiert, 64-Bit-Zahlen als Text) → Demo-Adresse,
// Spielzeit und die .info-Datei, die CS2 neben jede selbst geladene Demo legt (CDataGCCStrike15_v2_MatchInfo)
function extractMatch(Protos, m) {
	let rounds = Array.isArray(m.roundstatsall) ? m.roundstatsall : [];
	let last = rounds.length ? rounds[rounds.length - 1] : m.roundstats_legacy;
	let url = last && typeof last.map == 'string' && last.map ? last.map : '';
	let matchtime = Number(m.matchtime) || 0;
	if (!url) {
		return {status: 'nourl', matchtime};
	}
	let info = null;
	try {
		let T = Protos.CDataGCCStrike15_v2_MatchInfo;
		info = Buffer.from(T.encode(T.fromObject(m)).finish());
	} catch (e) {
		log.debug(`.info nicht erzeugt: ${e.message}`);
	}
	return {status: 'ok', url, matchtime, info};
}

// deps nur für Tests (nachgebauter steam-user); sonst die echten Bibliotheken
async function openBot({refreshToken, onToken}, deps = {}) {
	checkToken(refreshToken);
	const SteamUser = deps.SteamUser || require('steam-user');
	const GlobalOffensive = deps.GlobalOffensive || require('globaloffensive');
	const Protos = require('globaloffensive/protobufs/generated/_load.js');

	let user = new SteamUser({
		dataDirectory: null,        // nichts auf die Platte (der Dienst darf nur in fetch/ schreiben)
		autoRelogin: false,         // ein Lauf ist kurz; bei Verbindungsverlust lieber sauber abbrechen
		renewRefreshTokens: true,   // Steam erneuert das Token kurz vor Ablauf → Ereignis refreshToken
		enablePicsCache: false
	});
	let lastError = null;
	let disconnected = false;
	user.on('error', (err) => {
		lastError = err;
		log.debug(`steam-user: Fehler ${err.eresult || ''} ${err.message}`);
	});
	user.on('disconnected', (eresult, msg) => {
		disconnected = true;
		log.debug(`steam-user: getrennt (${eresult} ${msg || ''})`);
	});
	user.on('refreshToken', (token) => {
		try {
			onToken(token);
		} catch (e) {
			log.warn(`Neues Token: ${e.message}`);
		}
	});
	if (log.verbose) {
		user.on('debug', (msg) => log.debug('steam-user: ' + msg));
	}
	let playing = null;
	user.on('playingState', (blocked, appid) => {
		playing = {blocked, appid};
	});

	await withTimeout(new Promise((resolve, reject) => {
		user.once('loggedOn', resolve);
		user.once('error', reject);
		user.logOn({refreshToken});
	}), timing.botLogonMs, 'Steam antwortet nicht auf die Anmeldung', () => user.logOff());

	let steamId = user.steamID ? user.steamID.getSteamID64() : '';
	let csgo = null;
	let gcUp = false;
	let pending = null;   // {matchId, resolve}

	let bot = {
		steamId,
		get playingBlocked() {
			return !!(playing && playing.blocked);
		},
		get lost() {
			return disconnected || !!lastError;
		},
		lastError: () => lastError,

		// Steam schickt den Spielstatus kurz nach der Anmeldung
		async waitPlayingState(ms = 3000) {
			for (let t = 0; t < ms && !playing; t += 250) {
				await sleep(250);
			}
			return playing;
		},

		// CS2 ist kostenlos; ein neues Bot-Konto hat es aber evtl. noch nicht in der Bibliothek
		async ensureLicense() {
			let r = await user.requestFreeLicense([APP_CS2]);
			return (r.grantedAppIds || []).map(Number).includes(APP_CS2) ? 'granted' : 'owned';
		},

		// → true = Game Coordinator hat begrüßt; false = keine Antwort (Anfragen gehen trotzdem raus)
		async connectGC() {
			csgo = new GlobalOffensive(user);
			csgo.on('error', (err) => {
				lastError = err;
				log.warn(`Game Coordinator: ${err.message}`);
			});
			csgo.on('disconnectedFromGC', (reason) => {
				gcUp = false;
				log.debug(`Game Coordinator: getrennt (${reason})`);
			});
			csgo.on('matchList', (matches) => {
				if (!pending) {
					return;
				}
				let list = Array.isArray(matches) ? matches : [];
				let hit = list.find((m) => m && String(m.matchid) == pending.matchId);
				if (hit) {
					pending.resolve({match: hit});
				} else if (list.length == 0) {
					pending.resolve({empty: true});
				}
			});
			let welcome = new Promise((resolve) => csgo.once('connectedToGC', () => resolve(true)));
			user.gamesPlayed([APP_CS2]);
			gcUp = await withTimeout(welcome, timing.gcConnectMs, 'gc').catch((e) => {
				if (e.cancelled) {
					throw e;
				}
				return false;
			});
			return gcUp;
		},

		// → {status: 'ok', url, matchtime, info} | {status: 'nourl', matchtime} | {status: 'empty'} | {status: 'timeout'}
		async requestMatch(shareCode) {
			let decoded = decodeShareCode(shareCode);
			if (!decoded) {
				return {status: 'empty'};
			}
			for (let attempt = 0; attempt < 2; attempt++) {
				abort.check();
				if (lastError && !user.steamID) {
					throw lastError;
				}
				let answer = new Promise((resolve) => {
					pending = {matchId: decoded.matchId, resolve};
				});
				csgo.requestGame(shareCode);
				let got = await withTimeout(answer, timing.gcRequestMs, 'gc').catch((e) => {
					if (e.cancelled) {
						throw e;
					}
					return null;
				});
				pending = null;
				if (got && got.empty) {
					return {status: 'empty'};
				}
				if (got && got.match) {
					return extractMatch(Protos, got.match);
				}
				log.debug(`Game Coordinator: keine Antwort auf Versuch ${attempt + 1}`);
			}
			return {status: 'timeout'};
		},

		async close() {
			try {
				if (user.steamID) {
					user.gamesPlayed([]);
					await sleep(300).catch(() => {});
					let gone = new Promise((resolve) => user.once('disconnected', resolve));
					user.logOff();
					await Promise.race([gone, new Promise((r) => setTimeout(r, 5000))]);
				}
			} catch (e) {
				// egal – der Prozess endet ohnehin gleich
			}
		}
	};
	return bot;
}

module.exports = {
	name: 'Steam',
	timing,
	createLogin,
	webapi: webapi.realTransport,
	openBot,
	checkToken,
	extractMatch,
	EResult
};
