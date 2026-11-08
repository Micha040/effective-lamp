// fragdesk-fetch – spricht mit fragdesk-server über 127.0.0.1 (/api/v1/local/fetch/*), angemeldet mit dem Inhalt von
// <data>/local.key in der Kopfzeile X-FragDesk-Local (wie „fragdesk-server invite/users“). Schreibende Anfragen tragen
// zusätzlich X-FragDesk: 1 (sonst lehnt der Server sie ab). Fehler: {"error": "deutscher Text"}.
'use strict';

const http = require('http');
const log = require('./log');
const {request, sleep, netText} = require('./util');

class LocalError extends Error {
	// status 0 = keine Verbindung; fatal = der Lauf kann so nicht weitergehen
	constructor(message, status, fatal) {
		super(message);
		this.status = status;
		this.fatal = fatal;
	}
}

class LocalApi {
	constructor({server, key, timeoutMs = 60000, retries = 3, retryMs = 2000}) {
		let u = new URL(server);
		if (u.protocol != 'http:') {
			throw new LocalError('--server muss mit http:// beginnen (z. B. http://127.0.0.1:7420)', 0, true);
		}
		this.base = u.origin;
		this.key = key;
		this.timeoutMs = timeoutMs;
		this.retries = retries;
		this.retryMs = retryMs;
		// eine Verbindung je Anfrage (wie „fragdesk-server invite“): der Server schließt ruhende Verbindungen nach 20 s
		this.agent = new http.Agent({keepAlive: false});
		log.addSecret(key);
	}

	close() {
		this.agent.destroy();
	}

	// → {status, json}; wiederholt bei keiner Verbindung und 503 (Server startet neu), sonst kein Wiederholen
	async call(method, path, body, contentType) {
		let headers = {'X-FragDesk-Local': this.key, 'Accept': 'application/json', 'Connection': 'close'};
		if (method != 'GET') {
			headers['X-FragDesk'] = '1';
		}
		let data = null;
		if (body != null) {
			data = Buffer.isBuffer(body) ? body : Buffer.from(JSON.stringify(body));
			headers['Content-Type'] = contentType || 'application/json';
		}
		let lastErr = null;
		for (let attempt = 0; attempt <= this.retries; attempt++) {
			if (attempt > 0) {
				await sleep(this.retryMs * attempt);
			}
			let res;
			try {
				res = await request(this.base + path, {method, headers, body: data, timeoutMs: this.timeoutMs, agent: this.agent});
			} catch (err) {
				if (err.cancelled) {
					throw err;
				}
				lastErr = new LocalError(`Der FragDesk-Server antwortet nicht (${this.base}: ${netText(err)}). Läuft er? systemctl status fragdesk-server`, 0, true);
				continue;
			}
			let json = null;
			try {
				json = res.body.length ? JSON.parse(res.body.toString('utf8')) : null;
			} catch (e) {
				json = null;
			}
			if (res.status == 503) {
				lastErr = new LocalError('Der FragDesk-Server startet gerade neu.', 503, true);
				continue;
			}
			return {status: res.status, json};
		}
		throw lastErr;
	}

	errorText(res, fallback) {
		if (res.json && typeof res.json.error == 'string' && res.json.error) {
			return res.json.error;
		}
		let known = {413: 'zu groß für den Server', 507: 'Speicher auf dem Server voll', 500: 'Fehler im Server'}[res.status];
		return `${known || fallback} (Status ${res.status})`;
	}

	// Fehler, bei denen kein Lauf möglich ist (falscher Schlüssel, zu alter Server)
	checkCommon(res, what) {
		if (res.status == 403 || res.status == 401) {
			throw new LocalError(`Der FragDesk-Server lehnt den Helfer ab: ${this.errorText(res, 'nicht erlaubt')} – stimmt --data (local.key)?`, res.status, true);
		}
		if (res.status == 429) {
			throw new LocalError('Der FragDesk-Server sperrt den Helfer gerade (zu viele falsche Schlüssel) – in 15 Minuten wieder.', 429, true);
		}
		if (res.status == 404 && what == 'job') {
			throw new LocalError('Der FragDesk-Server kennt das automatische Laden von Demos noch nicht – bitte den Server aktualisieren (install.sh noch einmal ausführen).', 404, true);
		}
	}

	async job() {
		let res = await this.call('GET', '/api/v1/local/fetch/job');
		this.checkCommon(res, 'job');
		if (res.status != 200 || !res.json || typeof res.json != 'object') {
			throw new LocalError('Auftrag vom Server nicht lesbar: ' + this.errorText(res, 'unerwartete Antwort'), res.status, true);
		}
		return res.json;
	}

	async post(path, body, what) {
		let res = await this.call('POST', path, body);
		this.checkCommon(res, what);
		if (res.status != 200) {
			throw new LocalError(`${what}: ${this.errorText(res, 'abgelehnt')}`, res.status, res.status >= 500);
		}
		return res.json;
	}

	login(body) {
		return this.post('/api/v1/local/fetch/login', body, 'Anmeldestand an den Server');
	}

	token(body) {
		return this.post('/api/v1/local/fetch/token', body, 'Neues Token an den Server');
	}

	run(phase, text) {
		return this.post('/api/v1/local/fetch/run', {phase, text}, 'Laufstand an den Server');
	}

	// Anfragen für ein Konto: 401/403/404 heißt hier „Konto weg oder Laden ausgeschaltet“ (der Schlüssel hat beim
	// Auftrag ja noch gestimmt) – der Aufrufer überspringt dann das Konto. → {status, error}
	async forAccount(method, path, body, contentType) {
		let res = await this.call(method, path, body, contentType);
		return {status: res.status, error: res.status == 200 ? '' : this.errorText(res, 'abgelehnt'), json: res.json};
	}

	state(account, body) {
		return this.forAccount('POST', `/api/v1/local/fetch/${account}/state`, body);
	}

	// Ergebnis eines Matches hochladen; 200 = gespeichert. Der Aufrufer entscheidet über 507/413/400.
	putMatchFile(account, checksum, part, data) {
		let type = part == 'json' ? 'application/json' : 'application/octet-stream';
		return this.forAccount('PUT', `/api/v1/local/fetch/${account}/match/${checksum}/${part}`, data, type);
	}

	// Nach dem Import: woher die Demo kam ({url, matchtime, shareCode}) – damit lädt FragDesk am PC sie bei Bedarf selbst
	// bei Valve (solange Valve sie aufhebt). Ein älterer Server kennt das nicht (404): kein Fehler des Matches.
	putMatchSource(account, checksum, body) {
		return this.forAccount('PUT', `/api/v1/local/fetch/${account}/match/${checksum}/source`, body);
	}
}

module.exports = {LocalApi, LocalError};
