// fragdesk-fetch – Protokoll: eine Zeile pro Ereignis auf stdout/stderr (unter systemd im Journal). Geheimnisse
// (Passwort, Steam-Guard-Code, Tokens, Web-API-Schlüssel, Authentifizierungscodes) werden vor dem Schreiben
// durch „•••“ ersetzt – auch dann, wenn sie in einer Fehlermeldung einer Bibliothek stecken.
// Unter systemd (JOURNAL_STREAM gesetzt) bekommt jede Zeile ein Präfix <3>/<4>/<6>/<7>, damit journalctl die
// Wichtigkeit kennt (journalctl -p warning -u fragdesk-fetch zeigt nur Warnungen und Fehler).
'use strict';

const secrets = new Set();
let sink = null;   // für Tests: function(level, text)
let verbose = !!process.env.FRAGDESK_FETCH_DEBUG;
const journal = !!process.env.JOURNAL_STREAM;

function addSecret(value) {
	if (typeof value == 'string' && value.length >= 4) {
		secrets.add(value);
		// auch in der Schreibweise, in der es in URLs stehen könnte
		let enc = encodeURIComponent(value);
		if (enc != value) {
			secrets.add(enc);
		}
	}
}

function scrub(text) {
	text = String(text);
	for (let s of secrets) {
		if (text.includes(s)) {
			text = text.split(s).join('•••');
		}
	}
	// Sicherheitsnetz: JWTs (Steam-Tokens) und key=-Parameter nie ausschreiben
	text = text.replace(/eyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]+/g, '•••');
	text = text.replace(/([?&](?:key|steamidkey|access_token)=)[^&\s]+/gi, '$1•••');
	return text;
}

const PREFIX = {error: '<3>', warn: '<4>', info: '<6>', debug: '<7>'};
const WORD = {error: 'Fehler: ', warn: 'Achtung: ', info: '', debug: '· '};

function write(level, parts) {
	if (level == 'debug' && !verbose) {
		return;
	}
	let text = scrub(parts.map((p) => (p instanceof Error ? p.message : typeof p == 'string' ? p : JSON.stringify(p))).join(' '));
	if (sink) {
		sink(level, text);
		return;
	}
	let lines = text.split('\n').filter((l) => l.length > 0);
	let out = (level == 'error' || level == 'warn') ? process.stderr : process.stdout;
	for (let l of lines) {
		out.write((journal ? PREFIX[level] : WORD[level]) + l + '\n');
	}
}

module.exports = {
	info: (...p) => write('info', p),
	warn: (...p) => write('warn', p),
	error: (...p) => write('error', p),
	debug: (...p) => write('debug', p),
	addSecret,
	scrub,
	setSink(fn) {
		sink = fn;
	},
	setVerbose(v) {
		verbose = !!v;
	},
	get verbose() {
		return verbose;
	}
};
