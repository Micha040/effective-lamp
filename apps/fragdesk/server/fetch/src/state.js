// fragdesk-fetch – Dateien in <data>/fetch/ (der Server legt den Ordner an, 0700):
//   lock        nie zwei Läufe gleichzeitig: {"pid", "boot", "started"}; ein Rest nach Absturz oder Neustart gilt nicht
//   wake        schreibt der Server (aktuelle Zeit), wenn der Helfer laufen soll; ändert er sich im Lauf: einmal von vorn
//   tmp/        Demos und Auswertungen während des Laufs (wird am Anfang und am Ende geleert)
//   state.json  Gedächtnis des Helfers (0600): Fehlversuche je Freigabecode, Konten mit falschen Angaben (403/412 –
//               nicht weiter fragen, bis sie sich ändern), voller Speicher (507 – eine Stunde Pause), CS2-Lizenz geholt.
//               Keine Geheimnisse: von Authentifizierungscodes nur ein gekürzter SHA-256.
'use strict';

const crypto = require('crypto');
const fs = require('fs');
const path = require('path');
const log = require('./log');
const {now} = require('./util');

const STALE_LOCK_SECONDS = 3 * 3600;   // länger als TimeoutStartSec=2h des Dienstes

function bootId() {
	try {
		return fs.readFileSync('/proc/sys/kernel/random/boot_id', 'utf8').trim();
	} catch (e) {
		return '';
	}
}

function pidAlive(pid) {
	if (!Number.isInteger(pid) || pid <= 0) {
		return false;
	}
	try {
		process.kill(pid, 0);
		return true;
	} catch (e) {
		return e.code == 'EPERM';
	}
}

class Lock {
	constructor(file) {
		this.file = file;
		this.held = false;
	}

	// true = genommen; false = ein anderer Lauf ist aktiv
	acquire() {
		let me = JSON.stringify({pid: process.pid, boot: bootId(), started: now()});
		for (let attempt = 0; attempt < 2; attempt++) {
			try {
				let fd = fs.openSync(this.file, 'wx', 0o600);
				fs.writeSync(fd, me);
				fs.closeSync(fd);
				this.held = true;
				return true;
			} catch (e) {
				if (e.code != 'EEXIST') {
					throw e;
				}
			}
			let other = null;
			try {
				other = JSON.parse(fs.readFileSync(this.file, 'utf8'));
			} catch (e) {
				other = null;   // halb geschrieben oder kaputt: wie veraltet behandeln, wenn alt genug
			}
			let stale;
			if (!other) {
				let age = now() - Math.floor(fs.statSync(this.file).mtimeMs / 1000);
				stale = age > 60;
			} else {
				stale = (other.boot && other.boot != bootId()) || !pidAlive(other.pid) || now() - (other.started || 0) > STALE_LOCK_SECONDS;
			}
			if (!stale) {
				return false;
			}
			log.info('Alte Sperre von einem abgebrochenen Lauf gefunden – wird übernommen.');
			try {
				fs.renameSync(this.file, this.file + '.alt');   // atomar: nur einer kann sie wegnehmen
				fs.unlinkSync(this.file + '.alt');
			} catch (e) {
				if (e.code != 'ENOENT') {
					throw e;
				}
			}
		}
		return false;
	}

	release() {
		if (!this.held) {
			return;
		}
		this.held = false;
		try {
			let other = JSON.parse(fs.readFileSync(this.file, 'utf8'));
			if (other.pid == process.pid) {
				fs.unlinkSync(this.file);
			}
		} catch (e) {
			// schon weg
		}
	}
}

// Inhalt und Zeit von wake – ändert sich eins davon, hat der Server den Helfer (wieder) geweckt
function readWake(file) {
	try {
		let st = fs.statSync(file);
		return fs.readFileSync(file, 'utf8').trim() + '@' + st.mtimeMs;
	} catch (e) {
		return '';
	}
}

function emptyDir(dir) {
	fs.mkdirSync(dir, {recursive: true, mode: 0o700});
	for (let name of fs.readdirSync(dir)) {
		fs.rmSync(path.join(dir, name), {recursive: true, force: true});
	}
}

function hashOf(...parts) {
	return crypto.createHash('sha256').update(parts.join('\n')).digest('hex').slice(0, 16);
}

// ---- state.json --------------------------------------------------------------------------------------------------------
class HelperState {
	constructor(file) {
		this.file = file;
		this.data = {accounts: {}, license: {}};
		try {
			let v = JSON.parse(fs.readFileSync(file, 'utf8'));
			if (v && typeof v == 'object') {
				this.data.accounts = v.accounts && typeof v.accounts == 'object' ? v.accounts : {};
				this.data.license = v.license && typeof v.license == 'object' ? v.license : {};
			}
		} catch (e) {
			if (e.code != 'ENOENT') {
				log.warn(`${file} ist unlesbar und wird neu angelegt (${e.message}).`);
			}
		}
	}

	account(id) {
		if (!this.data.accounts[id]) {
			this.data.accounts[id] = {};
		}
		return this.data.accounts[id];
	}

	// Fehlversuche für einen Freigabecode (der nächste nach knownCode)
	failures(id, code) {
		let a = this.data.accounts[id];
		return a && a.attempts && a.attempts.code == code ? a.attempts.count || 0 : 0;
	}

	addFailure(id, code) {
		let a = this.account(id);
		let count = this.failures(id, code) + 1;
		a.attempts = {code, count, last: now()};
		return count;
	}

	clearFailures(id) {
		if (this.data.accounts[id]) {
			delete this.data.accounts[id].attempts;
		}
	}

	// 403/412: nicht weiter fragen, solange die Angaben gleich sind (höchstens 6 Stunden)
	blocked(id, key) {
		let a = this.data.accounts[id];
		if (!a || !a.blocked) {
			return null;
		}
		if (a.blocked.key != key || now() - a.blocked.at > 6 * 3600) {
			delete a.blocked;
			return null;
		}
		return a.blocked;
	}

	block(id, key, status, text) {
		this.account(id).blocked = {key, status, text, at: now()};
	}

	// 507: eine Stunde nichts laden
	full(id) {
		let a = this.data.accounts[id];
		if (!a || !a.full) {
			return null;
		}
		if (now() - a.full.at > 3600) {
			delete a.full;
			return null;
		}
		return a.full;
	}

	setFull(id, text) {
		this.account(id).full = {at: now(), text};
	}

	clearFull(id) {
		if (this.data.accounts[id]) {
			delete this.data.accounts[id].full;
		}
	}

	// Konten, die es im Auftrag nicht mehr gibt, vergessen
	prune(ids) {
		for (let id of Object.keys(this.data.accounts)) {
			let a = this.data.accounts[id];
			if (!ids.includes(id) || Object.keys(a).length == 0) {
				delete this.data.accounts[id];
			}
		}
	}

	save() {
		let tmp = this.file + '.neu';
		try {
			fs.writeFileSync(tmp, JSON.stringify(this.data, null, 1) + '\n', {mode: 0o600});
			fs.renameSync(tmp, this.file);
		} catch (e) {
			log.warn(`${this.file} lässt sich nicht speichern (${e.message}) – Fehlversuche zählen dann von vorn.`);
		}
	}
}

module.exports = {Lock, readWake, emptyDir, hashOf, HelperState, bootId, pidAlive};
