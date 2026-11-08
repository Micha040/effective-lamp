// fragdesk-fetch – kleine Helfer: Warten mit Abbruch, Zeitlimits, HTTP(S) ohne fetch (Node 18 bringt fetch nur als
// WebAssembly mit, und das gibt es mit --jitless nicht), JWT lesen, deutsche Zahlen und Zeiten, Freigabecodes.
'use strict';

const http = require('http');
const https = require('https');

// ---- Abbruch und Zeitlimits ---------------------------------------------------------------------------------------
class Cancelled extends Error {
	constructor() {
		super('Abgebrochen');
		this.cancelled = true;
	}
}

// Ein Abbruch für den ganzen Lauf (SIGTERM von systemd, Strg+C)
const abort = {
	cancelled: false,
	listeners: new Set(),
	cancel() {
		if (this.cancelled) {
			return;
		}
		this.cancelled = true;
		for (let fn of [...this.listeners]) {
			try {
				fn();
			} catch (e) {
				// egal
			}
		}
	},
	on(fn) {
		this.listeners.add(fn);
		return () => this.listeners.delete(fn);
	},
	check() {
		if (this.cancelled) {
			throw new Cancelled();
		}
	},
	reset() {   // nur für Tests
		this.cancelled = false;
		this.listeners.clear();
	}
};

function sleep(ms) {
	return new Promise((resolve, reject) => {
		if (abort.cancelled) {
			return reject(new Cancelled());
		}
		let off = null;
		let t = setTimeout(() => {
			off();
			resolve();
		}, Math.max(0, ms));
		off = abort.on(() => {
			clearTimeout(t);
			reject(new Cancelled());
		});
	});
}

class TimeoutError extends Error {
	constructor(what) {
		super(what);
		this.timeout = true;
	}
}

// promise mit Zeitlimit; onTimeout räumt ggf. auf
function withTimeout(promise, ms, what, onTimeout) {
	let timer;
	let off = () => {};
	let limit = new Promise((resolve, reject) => {
		timer = setTimeout(() => {
			if (onTimeout) {
				try {
					onTimeout();
				} catch (e) {
					// egal
				}
			}
			reject(new TimeoutError(what));
		}, ms);
		off = abort.on(() => reject(new Cancelled()));
	});
	return Promise.race([promise, limit]).finally(() => {
		clearTimeout(timer);
		off();
	});
}

// ---- HTTP -----------------------------------------------------------------------------------------------------------
class HttpError extends Error {
	constructor(message, code) {
		super(message);
		this.code = code;   // ECONNREFUSED, ETIMEDOUT, …
		this.network = true;
	}
}

// Eine Anfrage, Antwort ganz im Speicher (höchstens maxBytes). Für die Demo selbst: download.js.
function request(url, {method = 'GET', headers = {}, body = null, timeoutMs = 30000, maxBytes = 8 << 20, agent} = {}) {
	return new Promise((resolve, reject) => {
		if (abort.cancelled) {
			return reject(new Cancelled());
		}
		let u = new URL(url);
		let mod = u.protocol == 'https:' ? https : http;
		let h = Object.assign({}, headers);
		if (body != null) {
			h['Content-Length'] = Buffer.byteLength(body);
		}
		let done = false;
		let off = () => {};
		let finish = (err, res) => {
			if (done) {
				return;
			}
			done = true;
			clearTimeout(timer);
			off();
			err ? reject(err) : resolve(res);
		};
		let req = mod.request(u, {method, headers: h, agent}, (res) => {
			let chunks = [];
			let size = 0;
			res.on('data', (c) => {
				size += c.length;
				if (size > maxBytes) {
					req.destroy();
					finish(new HttpError(`Antwort zu groß (über ${fmtBytes(maxBytes)})`, 'ETOOBIG'));
					return;
				}
				chunks.push(c);
			});
			res.on('end', () => finish(null, {status: res.statusCode, headers: res.headers, body: Buffer.concat(chunks)}));
			res.on('error', (e) => finish(new HttpError(e.message, e.code)));
			res.on('aborted', () => finish(new HttpError('Verbindung abgebrochen', 'ECONNRESET')));
		});
		let timer = setTimeout(() => {
			req.destroy();
			finish(new HttpError('Zeitüberschreitung', 'ETIMEDOUT'));
		}, timeoutMs);
		off = abort.on(() => {
			req.destroy();
			finish(new Cancelled());
		});
		// Lehnt der Server einen großen Körper früh ab (413) und schließt, sieht Node oft erst EPIPE/ECONNRESET – die
		// Antwort kommt dann kurz danach und soll gewinnen
		req.on('error', (e) => {
			let late = e.code == 'EPIPE' || e.code == 'ECONNRESET';
			setTimeout(() => finish(new HttpError(e.message, e.code)), late ? 250 : 0);
		});
		if (body != null) {
			req.write(body);
		}
		req.end();
	});
}

function netText(err) {   // Netzwerkfehler auf Deutsch
	switch (err && err.code) {
		case 'ETIMEDOUT':
			return 'Zeitüberschreitung';
		case 'ECONNREFUSED':
			return 'Verbindung abgelehnt';
		case 'ECONNRESET':
		case 'EPIPE':
			return 'Verbindung abgebrochen';
		case 'ENOTFOUND':
		case 'EAI_AGAIN':
			return 'Name nicht auflösbar (DNS)';
		case 'ENETUNREACH':
		case 'EHOSTUNREACH':
			return 'Netz nicht erreichbar';
		default:
			return (err && err.message) || 'unbekannter Fehler';
	}
}

// ---- JWT (Steam-Tokens) ---------------------------------------------------------------------------------------------
function decodeJwt(token) {
	try {
		let parts = String(token).split('.');
		if (parts.length != 3) {
			return null;
		}
		let json = Buffer.from(parts[1].replace(/-/g, '+').replace(/_/g, '/'), 'base64').toString('utf8');
		let v = JSON.parse(json);
		return v && typeof v == 'object' ? v : null;
	} catch (e) {
		return null;
	}
}

// ---- Texte ----------------------------------------------------------------------------------------------------------
function fmtBytes(n) {
	if (n >= 1 << 30) {
		return (n / (1 << 30)).toFixed(1).replace('.', ',') + ' GB';
	}
	if (n >= 1 << 20) {
		return (n / (1 << 20)).toFixed(n >= 100 << 20 ? 0 : 1).replace('.', ',') + ' MB';
	}
	if (n >= 1024) {
		return Math.round(n / 1024) + ' KB';
	}
	return n + ' B';
}

function fmtDuration(ms) {
	let s = Math.round(ms / 1000);
	if (s < 60) {
		return s + ' s';
	}
	let m = Math.floor(s / 60);
	s %= 60;
	if (m < 60) {
		return s ? `${m} min ${s} s` : `${m} min`;
	}
	return `${Math.floor(m / 60)} h ${m % 60} min`;
}

const pad2 = (n) => String(n).padStart(2, '0');

// Ortszeit des Servers (TZ aus der Umgebung, bei FragDesk /etc/default/fragdesk)
function fmtDate(epochSeconds, withYear) {
	if (!epochSeconds) {
		return '?';
	}
	let d = new Date(epochSeconds * 1000);
	return `${pad2(d.getDate())}.${pad2(d.getMonth() + 1)}.${withYear ? d.getFullYear() : ''} ${pad2(d.getHours())}:${pad2(d.getMinutes())}`;
}

const now = () => Math.floor(Date.now() / 1000);

// ---- Freigabecodes (CSGO-xxxxx-xxxxx-xxxxx-xxxxx-xxxxx) ---------------------------------------------------------------
const DICT = 'ABCDEFGHJKLMNOPQRSTUVWXYZabcdefhijkmnopqrstuvwxyz23456789';
const SHARE_RE = /^CSGO(-[A-Za-z0-9]{5}){5}$/;

// → {matchId, outcomeId, token} als Zeichenketten (wie globaloffensive-sharecode), null bei ungültigem Code
function decodeShareCode(code) {
	if (!SHARE_RE.test(code)) {
		return null;
	}
	let chars = code.slice(5).replace(/-/g, '').split('').reverse();
	let big = 0n;
	for (let c of chars) {
		let i = DICT.indexOf(c);
		if (i < 0) {
			return null;
		}
		big = big * 57n + BigInt(i);
	}
	let bytes = Buffer.alloc(18);
	for (let i = 17; i >= 0; i--) {
		bytes[i] = Number(big & 0xffn);
		big >>= 8n;
	}
	if (big != 0n) {
		return null;
	}
	return {
		matchId: bytes.readBigUInt64LE(0).toString(),
		outcomeId: bytes.readBigUInt64LE(8).toString(),
		token: String(bytes.readUInt16LE(16))
	};
}

function encodeShareCode(matchId, outcomeId, token) {
	let bytes = Buffer.alloc(18);
	bytes.writeBigUInt64LE(BigInt(matchId), 0);
	bytes.writeBigUInt64LE(BigInt(outcomeId), 8);
	bytes.writeUInt16LE(Number(token) & 0xffff, 16);
	let big = BigInt('0x' + bytes.toString('hex'));
	let out = '';
	for (let i = 0; i < 25; i++) {
		out += DICT[Number(big % 57n)];
		big /= 57n;
	}
	return 'CSGO-' + out.match(/.{5}/g).join('-');
}

// Für das Protokoll: nur der letzte Block (der ganze Code reicht, um die Demo zu laden)
function maskCode(code) {
	return SHARE_RE.test(code || '') ? 'CSGO-…-' + code.slice(-5) : '(kein Code)';
}

module.exports = {
	Cancelled,
	TimeoutError,
	HttpError,
	abort,
	sleep,
	withTimeout,
	request,
	netText,
	decodeJwt,
	fmtBytes,
	fmtDuration,
	fmtDate,
	now,
	SHARE_RE,
	decodeShareCode,
	encodeShareCode,
	maskCode
};
