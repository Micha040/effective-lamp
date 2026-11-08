// fragdesk-fetch – eine Demo von Valve laden (http://replayNNN.valve.net/730/<id>_<id>.dem.bz2): erst in <ziel>.part,
// erst vollständig und geprüft unter dem richtigen Namen. Grenzen: Größe (Standard 1 GB), Gesamtzeit, Zeit ohne Daten.
// Fortschritt ins Protokoll (alle 15 s oder je 25 %). Weiterleitungen (höchstens 3) werden befolgt.
'use strict';

const fs = require('fs');
const http = require('http');
const https = require('https');
const log = require('./log');
const {abort, Cancelled, fmtBytes, fmtDuration, netText} = require('./util');

class DownloadError extends Error {
	// kind: 'gone' (404/410 – Demo nicht (mehr) da), 'http', 'net', 'size', 'format', 'disk'
	constructor(message, kind, status) {
		super(message);
		this.kind = kind;
		this.status = status || 0;
	}
}

// Valve liefert .dem.bz2; ungepackt (PBDEMS2), gzip und zstd nimmt fdanalyze auch
const MAGIC = [Buffer.from('BZh'), Buffer.from('PBDEMS2\0'), Buffer.from([0x1f, 0x8b]), Buffer.from([0x28, 0xb5, 0x2f, 0xfd])];

function looksLikeDemo(head) {
	return MAGIC.some((m) => head.length >= m.length && head.subarray(0, m.length).equals(m));
}

// Platz für die Demo, die entpackte Fassung (fdanalyze entpackt nach -temp; gemessen: 84 MB Valve-Demo → 53 MB .bz2,
// also ≈ 1,6×; mit Luft 2,5×) und die Reserve, die auch fragdesk-server frei lässt (--min-free 512)
const UNPACK_RATIO = 2.5;
const RESERVE = 512 * 1048576;

function freeBytes(dir) {
	try {
		if (typeof fs.statfsSync != 'function') {   // erst ab Node 18.15
			return -1;
		}
		let st = fs.statfsSync(dir);
		return Number(st.bavail) * Number(st.bsize);
	} catch (e) {
		return -1;
	}
}

function once(url, file, opts, redirects) {
	return new Promise((resolve, reject) => {
		if (abort.cancelled) {
			return reject(new Cancelled());
		}
		let u;
		try {
			u = new URL(url);
		} catch (e) {
			return reject(new DownloadError('Ungültige Demo-Adresse', 'format'));
		}
		if (u.protocol != 'http:' && u.protocol != 'https:') {
			return reject(new DownloadError('Ungültige Demo-Adresse', 'format'));
		}
		let mod = u.protocol == 'https:' ? https : http;
		let started = Date.now();
		let out = null;
		let outClosed = false;
		let settled = false;
		let idleTimer = null;
		let totalTimer = null;
		let off = () => {};
		let req = null;
		let finish = (err, value) => {
			if (settled) {
				return;
			}
			settled = true;
			clearTimeout(idleTimer);
			clearTimeout(totalTimer);
			off();
			if (err) {
				if (req) {
					req.destroy();
				}
				let cleanup = () => fs.rm(file + '.part', {force: true}, () => reject(err));
				if (out && !outClosed) {
					out.on('close', cleanup);
					out.destroy();
				} else {
					cleanup();
				}
			} else {
				resolve(value);
			}
		};
		let idle = () => {
			clearTimeout(idleTimer);
			idleTimer = setTimeout(() => finish(new DownloadError(`keine Daten mehr seit ${Math.round(opts.idleMs / 1000)} s`, 'net')), opts.idleMs);
		};
		totalTimer = setTimeout(() => finish(new DownloadError(`dauert länger als ${fmtDuration(opts.timeoutMs)}`, 'net')), opts.timeoutMs);
		off = abort.on(() => finish(new Cancelled()));
		idle();
		req = mod.get(u, {headers: {'User-Agent': 'fragdesk-fetch', 'Accept-Encoding': 'identity'}}, (res) => {
			let status = res.statusCode;
			if ([301, 302, 303, 307, 308].includes(status) && res.headers.location) {
				res.resume();
				if (redirects >= 3) {
					return finish(new DownloadError('zu viele Weiterleitungen', 'http', status));
				}
				let next = new URL(res.headers.location, u).toString();
				log.debug(`Demo: weitergeleitet nach ${new URL(next).host}`);
				clearTimeout(idleTimer);
				clearTimeout(totalTimer);
				off();
				settled = true;
				return once(next, file, opts, redirects + 1).then(resolve, reject);
			}
			if (status == 404 || status == 410) {
				res.resume();
				return finish(new DownloadError(`die Demo gibt es bei Valve nicht (mehr) (${status})`, 'gone', status));
			}
			if (status != 200) {
				res.resume();
				return finish(new DownloadError(`Valve antwortet mit Status ${status}`, 'http', status));
			}
			let total = parseInt(res.headers['content-length'] || '-1', 10);
			if (total > opts.maxBytes) {
				res.resume();
				return finish(new DownloadError(`die Demo ist zu groß (${fmtBytes(total)}, höchstens ${fmtBytes(opts.maxBytes)})`, 'size'));
			}
			let free = freeBytes(opts.dir);
			let need = RESERVE + (1 + UNPACK_RATIO) * Math.max(total, 64 * 1048576);
			if (free >= 0 && free < need) {
				res.resume();
				return finish(new DownloadError(`zu wenig freier Platz auf dem Server (${fmtBytes(free)} frei, gebraucht etwa ${fmtBytes(need)})`, 'disk'));
			}
			out = fs.createWriteStream(file + '.part', {mode: 0o600});
			out.on('close', () => {
				outClosed = true;
			});
			out.on('error', (e) => finish(new DownloadError(`Speichern ging schief (${e.code || e.message})`, 'disk')));
			let got = 0;
			let head = Buffer.alloc(0);
			let lastLog = Date.now();
			let lastQuarter = 0;
			res.on('data', (chunk) => {
				if (settled) {
					return;
				}
				idle();
				got += chunk.length;
				if (head.length < 8) {
					head = Buffer.concat([head, chunk.subarray(0, 8 - head.length)]);
					if (head.length >= 8 && !looksLikeDemo(head)) {
						return finish(new DownloadError('die Antwort ist keine Demo', 'format'));
					}
				}
				if (got > opts.maxBytes) {
					return finish(new DownloadError(`die Demo ist zu groß (über ${fmtBytes(opts.maxBytes)})`, 'size'));
				}
				if (!out.write(chunk)) {
					res.pause();
					out.once('drain', () => res.resume());
				}
				let quarter = total > 0 ? Math.floor((got / total) * 4) : 0;
				if (Date.now() - lastLog >= 15000 || (quarter > lastQuarter && quarter < 4)) {
					lastLog = Date.now();
					lastQuarter = quarter;
					let pct = total > 0 ? ` von ${fmtBytes(total)} (${Math.floor((got / total) * 100)} %)` : '';
					opts.onProgress(`${fmtBytes(got)}${pct}`);
				}
			});
			res.on('aborted', () => finish(new DownloadError('Verbindung abgebrochen', 'net')));
			res.on('error', (e) => finish(new DownloadError(netText(e), 'net')));
			res.on('end', () => {
				if (settled) {
					return;
				}
				if (total >= 0 && got != total) {
					return finish(new DownloadError(`unvollständig (${fmtBytes(got)} von ${fmtBytes(total)})`, 'net'));
				}
				if (got < 16 || !looksLikeDemo(head)) {
					return finish(new DownloadError('die Antwort ist keine Demo', 'format'));
				}
				out.end(() => {
					fs.rename(file + '.part', file, (e) => {
						if (e) {
							return finish(new DownloadError(`Speichern ging schief (${e.code})`, 'disk'));
						}
						finish(null, {bytes: got, ms: Date.now() - started});
					});
				});
			});
		});
		req.on('error', (e) => finish(new DownloadError(netText(e), 'net')));
	});
}

// Laden mit einem zweiten Versuch bei Netzproblemen (nicht bei 404 oder zu groß)
async function download(url, file, {maxBytes = 1 << 30, timeoutMs = 30 * 60000, idleMs = 60000, onProgress = () => {}, dir, attempts = 2} = {}) {
	let opts = {maxBytes, timeoutMs, idleMs, onProgress, dir: dir || require('path').dirname(file)};
	let lastErr = null;
	for (let i = 0; i < attempts; i++) {
		try {
			return await once(url, file, opts, 0);
		} catch (err) {
			if (err.cancelled || !(err instanceof DownloadError) || err.kind != 'net') {
				throw err;
			}
			lastErr = err;
			if (i + 1 < attempts) {
				log.warn(`Demo: Download abgebrochen (${err.message}) – zweiter Versuch …`);
			}
		}
	}
	throw lastErr;
}

module.exports = {download, DownloadError, freeBytes};
