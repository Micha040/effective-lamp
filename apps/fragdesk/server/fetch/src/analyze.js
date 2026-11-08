// fragdesk-fetch – fdanalyze (Linux) auf eine geladene Demo ansetzen:
//   nice -n 19 fdanalyze -demo <x.dem.bz2> -out <x>.json -replay <x>.replay -temp <ordner>   (GOMEMLIMIT=512MiB)
// fdanalyze schreibt die Heatmap selbst neben -out (<x>.heat) und meldet zeilenweise STEP …, DONE <datei> oder
// ERROR <KENNWORT> <Text>. Fehlende Heatmap/2D-Daten sind kein Fehler (dann steht ein Hinweis unter "warnings").
'use strict';

const {spawn} = require('child_process');
const fs = require('fs');
const os = require('os');
const log = require('./log');
const {ANALYZER} = require('./texts');
const {abort, Cancelled, fmtDuration} = require('./util');

class AnalyzeError extends Error {
	constructor(message, code) {
		super(message);
		this.code = code;
	}
}

const CHECKSUM_RE = /^[0-9A-Za-z_-]{1,64}$/;

function findNice() {
	for (let p of ['/usr/bin/nice', '/bin/nice']) {
		try {
			fs.accessSync(p, fs.constants.X_OK);
			return p;
		} catch (e) {
			// weiter
		}
	}
	return null;
}

// → {json, heat, replay (Pfade oder null), checksum, match (Kurzinfo), warnings}
function analyze({analyzer, demo, outBase, temp, timeoutMs = 20 * 60000}) {
	return new Promise((resolve, reject) => {
		if (abort.cancelled) {
			return reject(new Cancelled());
		}
		let out = outBase + '.json';
		let replay = outBase + '.replay';
		let heat = outBase + '.heat';
		try {   // fehlt fdanalyze, ist das kein Fehler des Matches (zählt nicht als Fehlversuch)
			fs.accessSync(analyzer, fs.constants.X_OK);
		} catch (e) {
			return reject(new AnalyzeError(`fdanalyze fehlt oder ist nicht ausführbar (${analyzer}: ${e.code})`, 'START'));
		}
		fs.mkdirSync(temp, {recursive: true, mode: 0o700});
		let args = ['-demo', demo, '-out', out, '-replay', replay, '-temp', temp];
		let nice = findNice();
		let cmd = nice || analyzer;
		let argv = nice ? ['-n', '19', analyzer, ...args] : args;
		let env = Object.assign({}, process.env, {GOMEMLIMIT: '512MiB'});
		let started = Date.now();
		let child;
		try {
			child = spawn(cmd, argv, {env, stdio: ['ignore', 'pipe', 'pipe']});
		} catch (e) {
			return reject(new AnalyzeError(`fdanalyze lässt sich nicht starten (${e.code || e.message})`, 'START'));
		}
		if (!nice && child.pid) {
			try {
				os.setPriority(child.pid, 19);
			} catch (e) {
				// dann eben normal
			}
		}
		let stderr = '';
		let done = null;
		let error = null;
		let killed = false;
		let lineBuf = '';
		child.stdout.on('data', (d) => {
			lineBuf += d.toString('utf8');
			let lines = lineBuf.split('\n');
			lineBuf = lines.pop();
			for (let l of lines) {
				l = l.trim();
				if (l.startsWith('STEP ')) {
					log.debug(`fdanalyze: ${l.slice(5)}`);
				} else if (l.startsWith('DONE ')) {
					done = l.slice(5);
				} else if (l.startsWith('ERROR ')) {
					let rest = l.slice(6);
					let sp = rest.indexOf(' ');
					error = {code: sp < 0 ? rest : rest.slice(0, sp), text: sp < 0 ? '' : rest.slice(sp + 1)};
				}
			}
		});
		child.stderr.on('data', (d) => {
			stderr = (stderr + d.toString('utf8')).slice(-2048);
		});
		let timer = setTimeout(() => {
			killed = true;
			child.kill('SIGKILL');
		}, timeoutMs);
		let off = abort.on(() => {
			killed = true;
			child.kill('SIGKILL');
		});
		child.on('error', (e) => {
			clearTimeout(timer);
			off();
			reject(new AnalyzeError(e.code == 'ENOENT' ? `fdanalyze fehlt (${analyzer})` : `fdanalyze lässt sich nicht starten (${e.code || e.message})`, 'START'));
		});
		child.on('close', (code, signal) => {
			clearTimeout(timer);
			off();
			if (abort.cancelled) {
				return reject(new Cancelled());
			}
			if (killed) {
				return reject(new AnalyzeError(`fdanalyze braucht länger als ${fmtDuration(timeoutMs)} – abgebrochen`, 'TIMEOUT'));
			}
			if (error || code != 0 || !done) {
				let c = error ? error.code : signal ? 'SIGNAL' : 'EXIT';
				let what = ANALYZER[c] || (signal ? `fdanalyze wurde beendet (${signal}${signal == 'SIGKILL' ? ', zu wenig Arbeitsspeicher?' : ''})` : `fdanalyze ist mit Code ${code} ausgestiegen`);
				let detail = error && error.text ? `: ${error.text.slice(0, 200)}` : stderr.trim() ? `: ${stderr.trim().split('\n').pop().slice(0, 200)}` : '';
				return reject(new AnalyzeError(what + detail, c));
			}
			let text;
			let json;
			try {
				text = fs.readFileSync(out);
				json = JSON.parse(text.toString('utf8'));
			} catch (e) {
				return reject(new AnalyzeError(`Die Auswertung ist unlesbar (${e.message})`, 'OUTPUT'));
			}
			let checksum = json && json.checksum;
			if (typeof checksum != 'string' || !CHECKSUM_RE.test(checksum)) {
				return reject(new AnalyzeError('Die Auswertung hat keine gültige Prüfsumme', 'OUTPUT'));
			}
			let m = json.match || {};
			resolve({
				json: out,
				heat: fs.existsSync(heat) ? heat : null,
				replay: fs.existsSync(replay) ? replay : null,
				checksum,
				ms: Date.now() - started,
				match: {map: m.map || '', scoreA: m.scoreA, scoreB: m.scoreB, date: m.date || '', mode: m.mode || '', shareCode: m.shareCode || ''},
				warnings: Array.isArray(json.warnings) ? json.warnings.map(String) : []
			});
		});
	});
}

// Für „check“: fdanalyze -version
function analyzerVersion(analyzer) {
	return new Promise((resolve) => {
		let child;
		try {
			child = spawn(analyzer, ['-version'], {stdio: ['ignore', 'pipe', 'pipe']});
		} catch (e) {
			return resolve({ok: false, text: e.code || e.message});
		}
		let outText = '';
		child.stdout.on('data', (d) => (outText += d));
		child.on('error', (e) => resolve({ok: false, text: e.code == 'ENOENT' ? 'fehlt' : e.code || e.message}));
		child.on('close', (code) => resolve({ok: code == 0, text: outText.trim() || `Code ${code}`}));
	});
}

module.exports = {analyze, analyzerVersion, AnalyzeError, CHECKSUM_RE};
