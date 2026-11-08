// fragdesk-fetch – ein Lauf („fragdesk-fetch run“), wie in der Spezifikation:
//   1. Sperre nehmen (sonst sofort Ende), tmp/ leeren, Auftrag holen (GET /api/v1/local/fetch/job)
//   2. Anmeldung angefragt → login.js
//   3. ohne Refresh-Token oder Web-API-Schlüssel: hier Schluss
//   4. je Konto GetNextMatchSharingCode bis "n/a" oder 10 neue Codes (webapi.js)
//   5. neue Codes: Bot einmal anmelden (Token-Erneuerung → /token), CS2-Lizenz falls nötig, Game Coordinator,
//      je Code die Match-Infos → Demo-Adresse; dann abmelden
//   6. je Konto und Match nacheinander: Demo laden, fdanalyze, json/heat/replay hochladen, /state; Fehlversuche
//      zählen (höchstens 3 je Code, dann überspringen), knownCode nur nach Erfolg oder Überspringen weiter
//   7. tmp leeren, Sperre frei; hat sich wake im Lauf geändert: einmal von vorn
// Spielen mehrere Konten dasselbe Match, wird die Demo nur einmal geladen und ausgewertet.
'use strict';

const fs = require('fs');
const path = require('path');
const log = require('./log');
const webapi = require('./webapi');
const {interactiveLogin} = require('./login');
const {download, DownloadError} = require('./download');
const {analyze, AnalyzeError} = require('./analyze');
const {Lock, readWake, emptyDir, hashOf, HelperState} = require('./state');
const {botErrorText, rawReason} = require('./texts');
const {now, sleep, fmtBytes, fmtDuration, fmtDate, decodeShareCode, decodeJwt, maskCode, SHARE_RE, abort} = require('./util');

const MAX_FAILURES = 3;              // Fehlversuche je Freigabecode, dann überspringen
const MAX_NEW_CODES = 10;            // je Konto und Lauf
const FRESH_SECONDS = 3 * 3600;      // so jung: fehlende Demo heißt „noch nicht bereit“, nicht „gelöscht“
const SOFT_LIMIT_MS = 100 * 60000;   // danach kein neues Match mehr anfangen (systemd beendet nach 2 h)
const ACCOUNT_RE = /^[0-9a-f]{8}$/;
const STEAMID_RE = /^7656\d{13}$/;
const AUTHCODE_RE = /^[A-Z0-9]{4}-[A-Z0-9]{5}-[A-Z0-9]{4}$/;

const plural = (n, one, many) => `${n} ${n == 1 ? one : many}`;

class Runner {
	constructor({api, steam, data, analyzer, maxDemoBytes}) {
		this.api = api;
		this.steam = steam;
		this.data = data;
		this.analyzer = analyzer;
		this.maxDemoBytes = maxDemoBytes || 1 << 30;
		this.fetchDir = path.join(data, 'fetch');
		this.tmp = path.join(this.fetchDir, 'tmp');
		this.started = Date.now();
		this.state = new HelperState(path.join(this.fetchDir, 'state.json'));
	}

	// ---- ganzer Lauf --------------------------------------------------------------------------------------------------
	async run() {
		let lock = new Lock(path.join(this.fetchDir, 'lock'));
		if (!lock.acquire()) {
			log.info('Ein anderer Lauf ist noch aktiv – nichts zu tun.');
			return 0;
		}
		try {
			try {
				emptyDir(this.tmp);
			} catch (e) {
				throw new Error(`${this.tmp} lässt sich nicht leeren (${e.code || e.message}) – lief der Helfer einmal als root? ` +
					`Abhilfe: chown -R fragdesk:fragdesk ${this.fetchDir}`);
			}
			for (let pass = 1; pass <= 2; pass++) {
				let wake = readWake(path.join(this.fetchDir, 'wake'));
				await this.pass();
				if (readWake(path.join(this.fetchDir, 'wake')) == wake) {
					break;
				}
				if (pass == 1) {
					log.info('Der Server hat den Helfer während des Laufs neu geweckt – noch ein Durchgang.');
				}
			}
			return 0;
		} finally {
			try {
				emptyDir(this.tmp);
			} catch (e) {
				log.warn(`tmp lässt sich nicht leeren: ${e.message}`);
			}
			this.state.save();
			lock.release();
		}
	}

	async reportRun(phase, text) {
		try {
			await this.api.run(phase, text);
		} catch (err) {
			if (err.cancelled) {
				throw err;
			}
			log.warn(`Laufstand nicht beim Server angekommen: ${err.message}`);
		}
	}

	// ---- ein Durchgang --------------------------------------------------------------------------------------------------
	async pass() {
		let job = await this.api.job();
		let bot = job.bot && typeof job.bot == 'object' ? job.bot : {};
		let accounts = Array.isArray(job.accounts) ? job.accounts : [];
		log.addSecret(bot.refreshToken);
		log.addSecret(bot.apiKey);
		for (let a of accounts) {
			log.addSecret(a && a.authCode);
		}
		this.stats = {accounts: accounts.length, fetched: 0, failed: 0, skipped: 0, problem: ''};
		log.info(`Auftrag: ${plural(accounts.length, 'Konto', 'Konten')}${job.login ? ', Anmeldung des Bots angefragt' : ''}.`);
		await this.reportRun('start', accounts.length ? `Läuft – prüft ${plural(accounts.length, 'Konto', 'Konten')}` : 'Läuft');
		let endText = null;
		try {
			endText = await this.work(job, bot, accounts);
		} catch (err) {
			if (err.cancelled) {
				endText = 'Abgebrochen (Dienst wurde beendet)';
				throw err;
			}
			endText = `Fehler: ${err.message}`;
			throw err;
		} finally {
			this.state.save();
			if (endText != null) {
				log.info(`Lauf: ${endText}`);
				await this.reportRun('end', endText).catch(() => {});
			}
		}
	}

	async work(job, bot, accounts) {
		let refreshToken = typeof bot.refreshToken == 'string' ? bot.refreshToken : '';
		let apiKey = typeof bot.apiKey == 'string' ? bot.apiKey.trim() : '';

		// 2. Anmeldung mit Benutzername und Passwort (nur mit Passwort – das gibt der Server genau einmal heraus)
		if (job.login && typeof job.login == 'object' && job.login.password) {
			let r = await interactiveLogin({steam: this.steam, api: this.api, login: job.login});
			if (r) {
				refreshToken = r.refreshToken;
			} else if (!refreshToken) {
				return 'Anmeldung des Steam-Bots fehlgeschlagen';
			}
		} else if (job.login) {
			log.debug('Auftrag enthält eine Anmeldung ohne Passwort – ignoriert.');
		}

		// 3. ohne Bot nichts zu tun
		if (!refreshToken) {
			return 'Kein Steam-Bot angemeldet – nichts zu tun';
		}
		if (!apiKey) {
			return 'Der Web-API-Schlüssel des Bots fehlt – nichts zu tun';
		}
		if (accounts.length == 0) {
			return 'Kein Konto hat das automatische Laden eingeschaltet';
		}
		this.state.prune(accounts.map((a) => a && a.id).filter(Boolean));

		// 4. neue Freigabecodes je Konto
		let work = await this.walkAll(accounts, apiKey);

		// 5. Match-Infos beim Game Coordinator
		let codes = [...new Set(work.flatMap((w) => w.codes))];
		let gc = {infos: new Map(), error: ''};
		if (codes.length) {
			gc = await this.gameCoordinator(refreshToken, codes);
		}

		// 6. Matches laden, auswerten, hochladen
		this.cache = new Map();
		for (let w of work) {
			abort.check();
			await this.account(w, gc);
		}
		return this.summary(gc);
	}

	summary(gc) {
		let s = this.stats;
		let parts = [`${plural(s.accounts, 'Konto', 'Konten')} geprüft`];
		parts.push(s.fetched ? plural(s.fetched, 'neues Match', 'neue Matches') : 'nichts Neues');
		if (s.skipped) {
			parts.push(`${s.skipped} übersprungen`);
		}
		if (s.failed) {
			parts.push(plural(s.failed, 'Fehler', 'Fehler'));
		}
		if (gc.error) {
			parts.push(gc.error);
		} else if (s.problem) {
			parts.push(s.problem);
		}
		return parts.join(' · ');
	}

	// ---- 4. Web-API -------------------------------------------------------------------------------------------------
	async walkAll(accounts, apiKey) {
		let work = [];
		let globalError = null;
		let gap = this.steam.timing.apiGapMs;
		let asked = 0;
		for (let acc of accounts) {
			abort.check();
			let w = {acc, codes: [], error: null};
			work.push(w);
			let bad = this.badAccount(acc);
			if (bad) {
				w.error = {scope: 'account', text: bad};
				w.invalid = true;
				log.warn(`Konto ${acc && acc.id}: ${bad}`);
				continue;
			}
			if (globalError) {
				w.error = globalError;
				continue;
			}
			let key = hashOf(acc.steamId, acc.authCode, acc.knownCode);
			let blocked = this.state.blocked(acc.id, key);
			if (blocked) {
				w.error = {scope: 'account', text: blocked.text};
				log.info(`Konto ${acc.id}: übersprungen – ${blocked.text} (fragt erst wieder, wenn sich die Angaben ändern)`);
				continue;
			}
			let full = this.state.full(acc.id);
			if (full) {
				w.error = {scope: 'full', text: full.text};
				log.info(`Konto ${acc.id}: Speicher auf dem Server war voll – nächster Versuch ab ${fmtDate(full.at + 3600)}.`);
				continue;
			}
			if (asked++ > 0 && gap > 0) {
				await sleep(gap);
			}
			log.info(`Konto ${acc.id}: suche neue Matches nach ${maskCode(acc.knownCode)} …`);
			let r = await webapi.walk(this.steam.webapi, {apiKey, steamId: acc.steamId, authCode: acc.authCode, knownCode: acc.knownCode}, {max: MAX_NEW_CODES, gapMs: gap});
			w.codes = r.codes;
			w.error = r.error;
			if (r.codes.length) {
				log.info(`Konto ${acc.id}: ${plural(r.codes.length, 'neues Match', 'neue Matches')}${r.more ? ' (mehr beim nächsten Lauf)' : ''}.`);
			} else if (!r.error) {
				log.info(`Konto ${acc.id}: nichts Neues.`);
			}
			if (r.error) {
				log.warn(`Konto ${acc.id}: ${r.error.text}`);
				if (r.error.scope == 'account') {
					this.state.block(acc.id, key, r.error.status, r.error.text);
				} else if (r.error.scope == 'key' || r.error.scope == 'global') {
					globalError = r.error;
					this.stats.problem = r.error.scope == 'key' ? 'Web-API-Schlüssel ungültig' : 'Steam-Web-API nicht erreichbar';
				}
			}
		}
		return work;
	}

	badAccount(acc) {
		if (!acc || typeof acc != 'object' || !ACCOUNT_RE.test(acc.id || '')) {
			return 'ungültiger Eintrag im Auftrag';
		}
		if (!STEAMID_RE.test(acc.steamId || '')) {
			return 'Im Profil fehlt die SteamID – erst in FragDesk am PC den Spieler wählen.';
		}
		if (!AUTHCODE_RE.test(acc.authCode || '')) {
			return 'Der Authentifizierungscode hat nicht die Form XXXX-XXXXX-XXXX.';
		}
		if (!SHARE_RE.test(acc.knownCode || '') || !decodeShareCode(acc.knownCode)) {
			return 'Der letzte bekannte Freigabecode hat nicht die Form CSGO-xxxxx-xxxxx-xxxxx-xxxxx-xxxxx.';
		}
		return '';
	}

	// ---- 5. Bot und Game Coordinator --------------------------------------------------------------------------------------
	async gameCoordinator(refreshToken, codes) {
		let infos = new Map();
		let t = this.steam.timing;
		let bot = null;
		let tokenPosts = [];
		log.info(`Steam: melde den Bot an (${plural(codes.length, 'Match', 'Matches')} beim Game Coordinator abfragen) …`);
		try {
			bot = await this.steam.openBot({
				refreshToken,
				onToken: (token) => {
					log.addSecret(token);
					let jwt = decodeJwt(token);
					let until = jwt ? Number(jwt.exp) || 0 : 0;
					log.info(`Steam hat das Token des Bots erneuert (gültig bis ${fmtDate(until, true)}).`);
					tokenPosts.push(this.api.token({refreshToken: token, tokenUntil: until}).catch((err) => {
						log.error(`Das erneuerte Token ist nicht beim Server angekommen: ${err.message}`);
					}));
				}
			});
		} catch (err) {
			if (err.cancelled) {
				throw err;
			}
			let m = botErrorText(err);
			log.error(`Steam: ${m.text} (${rawReason(err)})`);
			if (m.tokenGone) {
				await this.api.login({state: 'error', hint: m.text}).catch((e) => log.warn(`Anmeldestand nicht beim Server angekommen: ${e.message}`));
			}
			return {infos, error: m.tokenGone ? 'Steam-Bot muss neu angemeldet werden' : 'Steam nicht erreichbar', text: m.text};
		}
		let error = '';
		let text = '';
		try {
			log.info(`Steam: Bot angemeldet (${bot.steamId}).`);
			let playing = await bot.waitPlayingState();
			if (playing && playing.blocked) {
				text = 'Auf dem Steam-Konto des Bots läuft gerade ein Spiel auf einem anderen Gerät – nächster Versuch beim nächsten Lauf.';
				log.warn(`Steam: ${text}`);
				return {infos, error: 'Bot-Konto spielt gerade woanders', text};
			}
			let lic = this.state.data.license;
			if (!lic[bot.steamId]) {
				try {
					let r = await bot.ensureLicense();
					log.info(r == 'granted' ? 'Steam: CS2 für den Bot zur Bibliothek hinzugefügt.' : 'Steam: Der Bot hat CS2 schon.');
					lic[bot.steamId] = now();
				} catch (err) {
					if (err.cancelled) {
						throw err;
					}
					log.warn(`Steam: CS2-Lizenz ließ sich nicht prüfen (${rawReason(err)}) – versuche es trotzdem.`);
				}
			}
			let welcomed = await bot.connectGC();
			log.info(welcomed ? 'Game Coordinator: verbunden.' : 'Game Coordinator: keine Begrüßung – frage trotzdem an.');
			let silent = 0;
			let answered = 0;
			for (let code of codes) {
				abort.check();
				if (silent >= 2 && answered == 0) {
					text = 'Der Game Coordinator von CS2 antwortet nicht – nächster Versuch beim nächsten Lauf.' +
						(welcomed ? '' : ' (Hat das Bot-Konto CS2 in der Bibliothek?)');
					error = 'Game Coordinator antwortet nicht';
					log.warn(text);
					break;
				}
				let r = await bot.requestMatch(code);
				infos.set(code, r);
				let id = (decodeShareCode(code) || {}).matchId;
				if (r.status == 'timeout') {
					silent++;
					log.warn(`Match ${id}: keine Antwort vom Game Coordinator.`);
				} else {
					answered++;
					let when = r.matchtime ? `, gespielt am ${fmtDate(r.matchtime, true)}` : '';
					if (r.status == 'ok') {
						let host = '?';
						try {
							host = new URL(r.url).host;
						} catch (e) {
							host = '?';
						}
						log.info(`Match ${id}: Demo-Adresse erhalten (${host}${when}).`);
					} else if (r.status == 'nourl') {
						log.info(`Match ${id}: keine Demo-Adresse${when}.`);
					} else {
						log.warn(`Match ${id}: Valve liefert keine Match-Infos.`);
					}
				}
				if (t.gcGapMs > 0) {
					await sleep(t.gcGapMs);
				}
			}
			if (answered == 0 && silent > 0 && (!welcomed || silent >= 2)) {
				// Game Coordinator ganz stumm: Valves Problem, nicht das der Matches – zählt bei keinem als Fehlversuch.
				// (Hat er begrüßt und schweigt nur zu einem einzelnen Code, zählt das: sonst hinge so ein Code ewig.)
				infos.clear();
				if (!error) {
					text = 'Der Game Coordinator von CS2 antwortet nicht – nächster Versuch beim nächsten Lauf.';
					error = 'Game Coordinator antwortet nicht';
					log.warn(text);
				}
			}
		} catch (err) {
			// z. B. Verbindung zu Steam weg oder woanders angemeldet: schon beantwortete Codes gelten, der Rest wartet
			if (err.cancelled) {
				throw err;
			}
			let m = botErrorText(err);
			text = m.text;
			error = m.tokenGone ? 'Steam-Bot muss neu angemeldet werden' : 'Verbindung zu Steam verloren';
			log.error(`Steam: ${m.text} (${rawReason(err)})`);
			if (m.tokenGone) {
				await this.api.login({state: 'error', hint: m.text}).catch((e) => log.warn(`Anmeldestand nicht beim Server angekommen: ${e.message}`));
			}
		} finally {
			await bot.close();
			await Promise.all(tokenPosts);
			log.info('Steam: Bot abgemeldet.');
		}
		return {infos, error, text};
	}

	// ---- 6. ein Konto -------------------------------------------------------------------------------------------------------
	// Ist der Server weg, wirft das und beendet den Lauf – knownCode wurde dann nicht weitergezählt, der nächste Lauf
	// macht an derselben Stelle weiter (ein schon hochgeladenes Match wird einfach noch einmal ersetzt).
	async postState(acc, body) {
		let res = await this.api.state(acc.id, body);
		if (res.status == 401 || res.status == 403 || res.status == 404) {
			log.warn(`Konto ${acc.id}: vom Server abgelehnt (${res.error}) – Konto gelöscht oder Laden ausgeschaltet?`);
			return false;
		}
		if (res.status != 200) {
			log.warn(`Konto ${acc.id}: Stand nicht gespeichert (${res.error}).`);
		}
		return true;
	}

	async account(w, gc) {
		let acc = w.acc;
		if (w.invalid) {
			if (acc && ACCOUNT_RE.test(acc.id || '')) {
				await this.postState(acc, {knownCode: acc.knownCode || '', from: acc.knownCode || '', lastCheck: now(), lastError: w.error.text});
			}
			return;
		}
		let from = acc.knownCode;
		let known = acc.knownCode;
		let lastError = '';
		let fetched = 0;
		for (let code of w.codes) {
			abort.check();
			if (Date.now() - this.started > SOFT_LIMIT_MS) {
				log.warn('Der Lauf dauert schon über 100 Minuten – der Rest kommt beim nächsten Lauf.');
				break;
			}
			let info = gc.infos.get(code);
			if (!info) {   // Bot/Game Coordinator nicht erreichbar: zählt nicht als Fehlversuch
				lastError = gc.text || 'Der Game Coordinator von CS2 war nicht erreichbar – nächster Versuch beim nächsten Lauf.';
				break;
			}
			let id = (decodeShareCode(code) || {}).matchId;
			// im Protokoll mit Match-ID, in der Handy-App (lastError) nur mit Datum
			let label = `Match ${id}${info.matchtime ? ` (${fmtDate(info.matchtime)})` : ''}`;
			let userLabel = info.matchtime ? `Match vom ${fmtDate(info.matchtime)}` : `Match ${id}`;
			let r = await this.match(acc, code, info, label);
			if (r.gone) {
				return;   // Konto weg – kein Stand mehr
			}
			if (r.ok || r.skip) {
				known = code;
				this.state.clearFailures(acc.id);
				let body = {knownCode: code, from, lastCheck: now(), lastError: ''};
				if (r.ok) {
					fetched++;
					this.stats.fetched++;
					Object.assign(body, {lastNew: now(), fetched: 1});
				} else {
					this.stats.skipped++;
					log.info(`${label}: übersprungen – ${r.skip}.`);
				}
				if (!(await this.postState(acc, body))) {
					return;
				}
				from = code;
				continue;
			}
			if (r.full) {
				this.state.setFull(acc.id, r.full);
				lastError = r.full;
				this.stats.problem = this.stats.problem || 'Speicher voll';
				log.warn(`Konto ${acc.id}: ${r.full} – eine Stunde Pause für dieses Konto.`);
				break;
			}
			// Fehlschlag
			this.stats.failed++;
			if (!r.counted) {
				lastError = `${userLabel}: ${r.fail}`;
				log.warn(`${label}: ${r.fail}`);
				break;
			}
			let n = this.state.addFailure(acc.id, code);
			this.state.save();
			if (n >= MAX_FAILURES) {
				this.state.clearFailures(acc.id);
				known = code;
				lastError = `${userLabel} nach ${MAX_FAILURES} Fehlversuchen übersprungen: ${r.fail}`;
				log.warn(`${label}: nach ${MAX_FAILURES} Fehlversuchen übersprungen: ${r.fail}.`);
				if (!(await this.postState(acc, {knownCode: code, from, lastCheck: now(), lastError}))) {
					return;
				}
				from = code;
				continue;
			}
			lastError = `${userLabel}: ${r.fail} (Versuch ${n} von ${MAX_FAILURES} – der nächste Lauf versucht es wieder)`;
			log.warn(`${label}: ${r.fail} (Versuch ${n} von ${MAX_FAILURES}).`);
			break;
		}
		if (!lastError && w.error) {
			lastError = w.error.text;
		}
		if (fetched) {
			log.info(`Konto ${acc.id}: ${plural(fetched, 'neues Match', 'neue Matches')} geladen.`);
		}
		await this.postState(acc, {knownCode: known, from, lastCheck: now(), lastError});
	}

	// ---- 6. ein Match für ein Konto ------------------------------------------------------------------------------------
	// → {ok} | {skip: grund} | {fail: grund, counted} | {full: text} | {gone: true}
	async match(acc, code, info, label) {
		if (info.status == 'timeout') {
			return {fail: 'Der Game Coordinator liefert zu diesem Match keine Infos', counted: true};
		}
		if (info.status == 'empty') {
			return {fail: 'Valve kennt dieses Match nicht (mehr)', counted: true};
		}
		let fresh = info.matchtime && now() - info.matchtime < FRESH_SECONDS;
		if (info.status == 'nourl') {
			return fresh ? {fail: 'Die Demo ist bei Valve noch nicht bereit', counted: true} : {skip: 'die Demo ist bei Valve schon gelöscht'};
		}
		let r = this.cache.get(code);
		if (!r) {
			r = await this.fetchAndAnalyze(code, info, label, fresh);
			this.cache.set(code, r);
		}
		if (!r.files) {
			return r;
		}
		let sizes = [];
		for (let part of ['json', 'heat', 'replay']) {
			let file = r.files[part];
			if (!file) {
				continue;
			}
			let data = fs.readFileSync(file);
			let res = await this.api.putMatchFile(acc.id, r.checksum, part, data);
			if (res.status == 200) {
				sizes.push(`${part} ${fmtBytes(data.length)}`);
				continue;
			}
			if (res.status == 507) {
				return {full: res.error || 'Dein Speicher auf dem Server ist voll'};
			}
			if (part == 'json') {
				if (res.status == 401 || res.status == 403 || res.status == 404) {
					log.warn(`Konto ${acc.id}: vom Server abgelehnt (${res.error}) – Konto gelöscht oder Laden ausgeschaltet?`);
					return {gone: true};
				}
				return {fail: `Der Server nimmt die Auswertung nicht an (${res.error})`, counted: res.status < 500};
			}
			log.warn(`${label}: ${part == 'heat' ? 'Heatmap' : '2D-Daten'} nicht angenommen (${res.error}) – das Match ist trotzdem da.`);
		}
		log.info(`${label}: hochgeladen für Konto ${acc.id} (${sizes.join(', ')}).`);
		return {ok: true};
	}

	// Demo laden und auswerten (einmal je Code und Lauf) → {checksum, files} oder ein Ergebnis wie match()
	async fetchAndAnalyze(code, info, label, fresh) {
		if (!this.analyzerBroken) {
			try {   // erst gar nicht laden, wenn fdanalyze fehlt
				fs.accessSync(this.analyzer, fs.constants.X_OK);
			} catch (e) {
				this.analyzerBroken = `fdanalyze fehlt oder ist nicht ausführbar (${this.analyzer}: ${e.code}) – bitte install.sh noch einmal ausführen.`;
				log.error(this.analyzerBroken);
			}
		}
		if (this.analyzerBroken) {
			return {fail: this.analyzerBroken, counted: false};
		}
		let id = (decodeShareCode(code) || {}).matchId || 'match';
		let dir = path.join(this.tmp, id);
		emptyDir(dir);
		let name = demoName(info.url, id);
		let demo = path.join(dir, name);
		let host = '?';
		try {
			host = new URL(info.url).host;
		} catch (e) {
			return {fail: 'Ungültige Demo-Adresse von Valve', counted: true};
		}
		log.info(`${label}: lade Demo von ${host} …`);
		let dl;
		try {
			dl = await download(info.url, demo, {
				maxBytes: this.maxDemoBytes,
				dir: this.tmp,
				onProgress: (text) => log.info(`${label}: … ${text}`)
			});
		} catch (err) {
			if (err.cancelled || !(err instanceof DownloadError)) {
				throw err;
			}
			fs.rmSync(dir, {recursive: true, force: true});
			if (err.kind == 'gone') {
				return fresh ? {fail: `Download: ${err.message}`, counted: true} : {skip: 'die Demo ist bei Valve schon gelöscht'};
			}
			if (err.kind == 'size') {
				return {skip: err.message};
			}
			if (err.kind == 'disk') {
				return {fail: `Download: ${err.message}`, counted: false};
			}
			return {fail: `Download: ${err.message}`, counted: true};
		}
		log.info(`${label}: Demo geladen (${fmtBytes(dl.bytes)} in ${fmtDuration(dl.ms)}).`);
		if (info.matchtime) {   // ohne .info nimmt fdanalyze das Dateidatum als Spieldatum
			try {
				fs.utimesSync(demo, info.matchtime, info.matchtime);
			} catch (e) {
				log.debug(`Dateidatum: ${e.message}`);
			}
		}
		if (info.info && info.info.length) {   // wie CS2: <name>.dem.info mit den Match-Infos
			try {
				fs.writeFileSync(demo.replace(/\.bz2$/, '') + '.info', info.info, {mode: 0o600});
			} catch (e) {
				log.debug(`.info: ${e.message}`);
			}
		}
		log.info(`${label}: werte aus (fdanalyze) …`);
		let res;
		try {
			res = await analyze({
				analyzer: this.analyzer,
				demo,
				outBase: path.join(dir, 'out', name.replace(/\.dem(\.bz2|\.gz|\.zst)?$/, '')),
				temp: path.join(dir, 'unpack')
			});
		} catch (err) {
			if (err.cancelled || !(err instanceof AnalyzeError)) {
				throw err;
			}
			fs.rmSync(dir, {recursive: true, force: true});
			if (err.code == 'START') {
				this.analyzerBroken = err.message;
				log.error(`fdanalyze: ${err.message}`);
				return {fail: err.message, counted: false};
			}
			return {fail: `Auswertung: ${err.message}`, counted: true};
		} finally {
			// Demo und entpackte Fassung sofort weg, nur die Ergebnisse bleiben bis zum Ende des Laufs
			fs.rmSync(demo, {force: true});
			fs.rmSync(path.join(dir, 'unpack'), {recursive: true, force: true});
		}
		let m = res.match;
		let score = Number.isFinite(m.scoreA) && Number.isFinite(m.scoreB) ? ` ${m.scoreA}:${m.scoreB}` : '';
		log.info(`${label}: ausgewertet in ${fmtDuration(res.ms)} (${m.map || '?'}${score}).`);
		for (let w of res.warnings) {
			log.warn(`${label}: ${w}`);
		}
		return {checksum: res.checksum, files: {json: res.json, heat: res.heat, replay: res.replay}};
	}
}

// http://replay183.valve.net/730/003717052436717584506_1418282143.dem.bz2 → match730_003717052436717584506_1418282143_183.dem.bz2
// (so heißt die Demo auch, wenn CS2 sie selbst lädt)
function demoName(url, matchId) {
	try {
		let u = new URL(url);
		let m = u.pathname.match(/\/(\d+)\/(\d+_\d+)\.dem(\.bz2|\.gz|\.zst)?$/);
		let region = (u.hostname.match(/^replay(\d+)\./) || [])[1];
		if (m) {
			return `match${m[1]}_${m[2]}${region ? '_' + region : ''}.dem${m[3] || ''}`;
		}
		let ext = (u.pathname.match(/\.dem(\.bz2|\.gz|\.zst)?$/) || ['.dem.bz2'])[0];
		return `match730_${matchId}${ext}`;
	} catch (e) {
		return `match730_${matchId}.dem.bz2`;
	}
}

module.exports = {Runner, demoName, MAX_FAILURES};
