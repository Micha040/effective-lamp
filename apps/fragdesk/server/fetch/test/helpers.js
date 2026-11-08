// Testhelfer für fragdesk-fetch: ein nachgebauter fragdesk-server (nur /api/v1/local/fetch/* und /api/v1/health, mit
// denselben Prüfungen wie in der Spezifikation), ein falsches fdanalyze, Ports nur aus 7700–7749, Protokoll mitlesen.
'use strict';

const crypto = require('crypto');
const fs = require('fs');
const http = require('http');
const os = require('os');
const path = require('path');

// andere Ports, falls 7700–7749 auf diesem Rechner belegt sind: FRAGDESK_FETCH_TEST_PORTS=7800-7849
const PORTS = (process.env.FRAGDESK_FETCH_TEST_PORTS || '').match(/^(\d{4,5})-(\d{4,5})$/);
const PORT_FROM = PORTS ? Number(PORTS[1]) : 7700;
const PORT_TO = PORTS && Number(PORTS[2]) >= PORT_FROM ? Number(PORTS[2]) : PORTS ? PORT_FROM : 7749;

// Ohne npm ci fehlen die Steam-Bibliotheken: die Tests, die sie brauchen, werden dann mit diesem Hinweis übersprungen
const noModules = fs.existsSync(path.join(__dirname, '..', 'node_modules', 'globaloffensive')) ? false : 'node_modules fehlt (npm ci im Ordner server/fetch)';

// Freien Port aus 7700–7749 nehmen (Testdateien laufen parallel: bei Belegung den nächsten). Der Port kommt aus
// server.address() – ein listen()-Rückruf eines fehlgeschlagenen Versuchs bliebe sonst hängen und meldete den
// belegten Port.
function listenFree(server) {
	let port = PORT_FROM + Math.floor(Math.random() * (PORT_TO - PORT_FROM + 1));
	return new Promise((resolve, reject) => {
		let tries = 0;
		let cleanup = () => {
			server.off('error', onError);
			server.off('listening', onListening);
		};
		let onError = (e) => {
			if (e.code == 'EADDRINUSE' && ++tries <= PORT_TO - PORT_FROM) {
				port = port >= PORT_TO ? PORT_FROM : port + 1;
				server.listen(port, '127.0.0.1');
			} else {
				cleanup();
				reject(e);
			}
		};
		let onListening = () => {
			cleanup();
			resolve(server.address().port);
		};
		server.on('error', onError);
		server.on('listening', onListening);
		server.listen(port, '127.0.0.1');
	});
}

function tmpDir(name) {
	return fs.mkdtempSync(path.join(os.tmpdir(), `fragdesk-fetch-${name}-`));
}

function readBody(req) {
	return new Promise((resolve) => {
		let chunks = [];
		req.on('data', (c) => chunks.push(c));
		req.on('end', () => resolve(Buffer.concat(chunks)));
	});
}

// ---- nachgebauter fragdesk-server --------------------------------------------------------------------------------------
// job: {bot, login, accounts}; login.password und login.code gibt er je genau einmal heraus (wie der echte Server).
class FakeServer {
	constructor({key = 'testschluessel-1234567890', job = {}} = {}) {
		this.key = key;
		this.job = Object.assign({bot: {refreshToken: '', apiKey: ''}, login: null, accounts: []}, job);
		this.calls = [];            // {method, path, body}
		this.logins = [];           // Körper von POST /login
		this.tokens = [];           // POST /token
		this.runs = [];             // POST /run
		this.states = {};           // Konto → [Körper]
		this.files = {};            // Konto → {Prüfsumme → {json, heat, replay}}
		this.sources = {};          // Konto → {Prüfsumme → Körper von PUT …/source}
		this.full = new Set();      // Konten mit vollem Speicher (507)
		this.fetched = {};          // Konto → Summe von fetched
		this.pendingCode = '';      // vom Admin eingegebener Code (einmal heraus)
		this.reject = {};           // "json"/"heat"/"replay" → Status, den PUT liefern soll
		this.down = false;          // true: 503 für alles
		this.server = http.createServer((req, res) => this.handle(req, res));
	}

	async start() {
		this.port = await listenFree(this.server);
		this.url = `http://127.0.0.1:${this.port}`;
		return this;
	}

	stop() {
		return new Promise((r) => this.server.close(() => r()));
	}

	account(id) {
		return this.job.accounts.find((a) => a.id == id);
	}

	send(res, status, obj) {
		let body = Buffer.from(JSON.stringify(obj));
		res.writeHead(status, {'Content-Type': 'application/json', 'Content-Length': body.length});
		res.end(body);
	}

	async handle(req, res) {
		let body = await readBody(req);
		let p = new URL(req.url, 'http://x').pathname;
		let json = null;
		try {
			json = body.length && !p.includes('/match/') ? JSON.parse(body.toString('utf8')) : null;
		} catch (e) {
			json = null;
		}
		this.calls.push({method: req.method, path: p, body: json, size: body.length});
		if (p == '/api/v1/health') {
			return this.send(res, 200, {ok: true, version: 'test'});
		}
		if (this.down) {
			return this.send(res, 503, {error: 'Der Server startet gerade neu – gleich noch einmal versuchen.'});
		}
		if (req.method != 'GET' && req.headers['x-fragdesk'] != '1') {
			return this.send(res, 403, {error: 'Nicht erlaubt'});
		}
		if (req.headers['x-fragdesk-local'] != this.key) {
			return this.send(res, 403, {error: 'Falscher Schlüssel (local.key).'});
		}
		let m;
		if (p == '/api/v1/local/fetch/job' && req.method == 'GET') {
			let out = JSON.parse(JSON.stringify(this.job));
			if (this.job.login) {   // Passwort und Code je genau einmal
				this.job.login.password = '';
				this.job.login.code = '';
			}
			if (this.pendingCode) {
				out.login = Object.assign({user: '', password: ''}, out.login || {}, {code: this.pendingCode});
				this.pendingCode = '';
			}
			return this.send(res, 200, out);
		}
		if (p == '/api/v1/local/fetch/login' && req.method == 'POST') {
			this.logins.push(json);
			if (json && json.state == 'done' && json.refreshToken) {
				this.job.bot.refreshToken = json.refreshToken;
				this.job.login = null;
			}
			if (json && json.state == 'error') {
				this.job.login = null;
			}
			if (this.onLogin) {
				this.onLogin(json);
			}
			return this.send(res, 200, {ok: true});
		}
		if (p == '/api/v1/local/fetch/token' && req.method == 'POST') {
			this.tokens.push(json);
			this.job.bot.refreshToken = json.refreshToken;
			return this.send(res, 200, {ok: true});
		}
		if (p == '/api/v1/local/fetch/run' && req.method == 'POST') {
			this.runs.push(json);
			return this.send(res, 200, {ok: true});
		}
		if ((m = p.match(/^\/api\/v1\/local\/fetch\/([0-9a-f]{8})\/state$/)) && req.method == 'POST') {
			let acc = this.account(m[1]);
			if (!acc) {
				return this.send(res, 404, {error: 'Dieses Konto gibt es nicht.'});
			}
			(this.states[m[1]] = this.states[m[1]] || []).push(json);
			if (json.knownCode) {
				acc.knownCode = json.knownCode;
			}
			this.fetched[m[1]] = (this.fetched[m[1]] || 0) + (json.fetched || 0);
			return this.send(res, 200, {ok: true});
		}
		if ((m = p.match(/^\/api\/v1\/local\/fetch\/([0-9a-f]{8})\/match\/([0-9A-Za-z_-]{1,64})\/source$/)) && req.method == 'PUT') {
			let [, id, checksum] = m;
			if (this.reject.source) {   // z. B. 404: älterer Server ohne Demo-Quelle
				return this.send(res, this.reject.source, {error: 'Unbekannte Anfrage'});
			}
			let src = null;
			try {
				src = JSON.parse(body.toString('utf8'));
			} catch (e) {
				src = null;
			}
			if (!this.account(id) || !((this.files[id] || {})[checksum] || {}).json) {
				return this.send(res, 404, {error: 'Das Match ist (noch) nicht auf dem Server.'});
			}
			if (!src || !/^https?:\/\/[^/\s]+/.test(src.url || '')) {
				return this.send(res, 400, {error: 'url fehlt oder ist keine Adresse (http://…).'});
			}
			(this.sources[id] = this.sources[id] || {})[checksum] = src;
			return this.send(res, 200, {ok: true, until: src.matchtime ? src.matchtime + 30 * 86400 : 0});
		}
		if ((m = p.match(/^\/api\/v1\/local\/fetch\/([0-9a-f]{8})\/match\/([0-9A-Za-z_-]{1,64})\/(json|heat|replay)$/)) && req.method == 'PUT') {
			let [, id, checksum, part] = m;
			if (!this.account(id)) {
				return this.send(res, 404, {error: 'Dieses Konto gibt es nicht.'});
			}
			if (this.reject[part]) {
				return this.send(res, this.reject[part], {error: `abgelehnt (${part})`});
			}
			if (this.full.has(id)) {
				return this.send(res, 507, {error: 'Dein Speicher auf dem Server ist voll (15,0 GB von 15,0 GB belegt).'});
			}
			let files = ((this.files[id] = this.files[id] || {})[checksum] = this.files[id][checksum] || {});
			if (part == 'json') {
				let v;
				try {
					v = JSON.parse(body.toString('utf8'));
				} catch (e) {
					return this.send(res, 400, {error: 'Das Match lässt sich nicht lesen: kein JSON'});
				}
				if (v.checksum != checksum) {
					return this.send(res, 400, {error: 'Die Prüfsumme passt nicht zum Match.'});
				}
			} else {
				if (!files.json) {
					return this.send(res, 404, {error: 'Das Match ist (noch) nicht auf dem Server.'});
				}
				let magic = part == 'heat' ? 'FDHM' : 'FDRP';
				if (body.subarray(0, 4).toString() != magic) {
					return this.send(res, 400, {error: `Falsche Datei für ${part}.`});
				}
			}
			files[part] = {size: body.length, sha256: crypto.createHash('sha256').update(body).digest('hex'), data: body};
			return this.send(res, 200, {ok: true, size: body.length});
		}
		return this.send(res, 404, {error: 'Unbekannte Anfrage'});
	}
}

// ---- falsches fdanalyze ---------------------------------------------------------------------------------------------
// Schreibt <out>.json (Prüfsumme = SHA-256 der Demo, gekürzt), <out>.heat (FDHM…) und -replay (FDRP…).
// FAKE_ANALYZER=error|crash|slow|noheat|badjson steuert Fehler; FAKE_ANALYZER_LOG=<datei> zählt Aufrufe.
const FAKE_ANALYZER = `#!/usr/bin/env node
const fs = require('fs');
const crypto = require('crypto');
const a = process.argv.slice(2);
const arg = (n) => { const i = a.indexOf(n); return i >= 0 ? a[i + 1] : ''; };
const mode = process.env.FAKE_ANALYZER || 'ok';
if (process.env.FAKE_ANALYZER_LOG) fs.appendFileSync(process.env.FAKE_ANALYZER_LOG, JSON.stringify({args: a, mode, nice: require('os').getPriority()}) + '\\n');
if (a[0] == '-version') { console.log('fdanalyze 0.0.0-test'); process.exit(0); }
const demo = arg('-demo'), out = arg('-out'), replay = arg('-replay'), temp = arg('-temp');
console.log('STEP unpack');
if (!fs.existsSync(demo)) { console.log('ERROR NOT_FOUND ' + demo); process.exit(1); }
if (!fs.existsSync(temp)) { console.log('ERROR WRITE temp fehlt'); process.exit(1); }
if (process.env.GOMEMLIMIT != '512MiB') { console.log('ERROR INTERNAL GOMEMLIMIT fehlt'); process.exit(1); }
if (mode == 'error') { console.log('ERROR CORRUPT kaputte Testdemo'); process.exit(1); }
if (mode == 'crash') { process.kill(process.pid, 'SIGKILL'); }
if (mode == 'slow') { setTimeout(() => {}, 60000); return; }
console.log('STEP parse');
const data = fs.readFileSync(demo);
const checksum = crypto.createHash('sha256').update(data).digest('hex').slice(0, 16);
const mtime = fs.statSync(demo).mtime;
fs.mkdirSync(require('path').dirname(out), {recursive: true});
const json = {schema: 1, checksum, match: {map: 'de_test', scoreA: 13, scoreB: 7, date: mtime.toISOString().replace(/\\.\\d+Z$/, 'Z')}, warnings: []};
fs.writeFileSync(out, mode == 'badjson' ? '{kaputt' : JSON.stringify(json));
if (mode != 'noheat') fs.writeFileSync(out.replace(/\\.json$/, '.heat'), Buffer.concat([Buffer.from('FDHM'), Buffer.alloc(60, 1)]));
if (replay && mode != 'noheat') fs.writeFileSync(replay, Buffer.concat([Buffer.from('FDRP'), Buffer.alloc(100, 2)]));
console.log('STEP write');
console.log('DONE ' + out);
`;

function writeFakeAnalyzer(dir) {
	let file = path.join(dir, 'fdanalyze-test');
	fs.writeFileSync(file, FAKE_ANALYZER, {mode: 0o755});
	return file;
}

// ---- Demo-Server (wie replayNNN.valve.net) ----------------------------------------------------------------------------
// routes: Pfad → {file} | {data} | {status} | {truncate: n} | {stall: true} | {redirect: pfad}
async function demoServer(routes) {
	let hits = {};
	let server = http.createServer((req, res) => {
		let p = new URL(req.url, 'http://x').pathname;
		hits[p] = (hits[p] || 0) + 1;
		let r = routes[p];
		if (!r) {
			res.writeHead(404);
			return res.end();
		}
		if (r.status) {
			res.writeHead(r.status);
			return res.end();
		}
		if (r.redirect) {
			res.writeHead(302, {Location: r.redirect});
			return res.end();
		}
		let data = r.data || fs.readFileSync(r.file);
		if (r.stall) {
			res.writeHead(200, {'Content-Length': data.length});
			res.write(data.subarray(0, Math.min(1024, data.length)));
			return;   // dann nichts mehr
		}
		if (r.truncate) {
			res.writeHead(200, {'Content-Length': data.length});
			res.write(data.subarray(0, r.truncate), () => res.destroy());
			return;
		}
		res.writeHead(200, r.noLength ? {} : {'Content-Length': data.length});
		res.end(data);
	});
	let port = await listenFree(server);
	return {server, port, url: (p) => `http://127.0.0.1:${port}${p}`, hits, close: () => new Promise((r) => {
		server.closeAllConnections && server.closeAllConnections();
		server.close(() => r());
	})};
}

// Protokoll mitlesen
function captureLog() {
	let log = require('../src/log');
	let lines = [];
	log.setSink((level, text) => lines.push({level, text}));
	return {
		lines,
		text: () => lines.map((l) => `${l.level}: ${l.text}`).join('\n'),
		has: (re) => lines.some((l) => re.test(l.text)),
		stop: () => log.setSink(null)
	};
}

// Gültige Freigabecodes für Tests (aus Match-ID, Outcome-ID, Token)
function shareCode(n) {
	let {encodeShareCode} = require('../src/util');
	return encodeShareCode(3717052436717584000n + BigInt(n), 3717058124424299000n + BigInt(n * 7), 1000 + n);
}

module.exports = {listenFree, tmpDir, FakeServer, writeFakeAnalyzer, demoServer, captureLog, shareCode, readBody, noModules};
