// fragdesk-fetch – Helfer des FragDesk-Servers: holt neue Matchmaking-Demos (Premier, Wettkampf, Wingman) der Konten
// bei Valve, wertet sie mit fdanalyze aus und legt sie über 127.0.0.1 ins Konto. Läuft als systemd-Dienst
// fragdesk-fetch.service (vom Timer alle 15 min und sobald der Server <data>/fetch/wake neu schreibt), arbeitet ab, was
// ansteht, und beendet sich. Spezifikation: „Demos automatisch laden“ (Server und Helfer).
//
//   node --jitless fragdesk-fetch.js run   [--server http://127.0.0.1:7420] [--data /var/lib/fragdesk]
//                                          [--analyzer /usr/local/lib/fragdesk/fdanalyze] [--verbose]
//   node --jitless fragdesk-fetch.js check [dieselben Optionen] [--offline]   Umgebung prüfen (für die Fehlersuche)
//   node fragdesk-fetch.js version
//
// Test ohne Steam: FRAGDESK_FETCH_FAKE=<json-datei> (siehe steam-fake.js).
//
// Absichtlich ohne 'use strict' in dieser Datei: esbuild würde es sonst für die ganze gebündelte Datei setzen, und
// eine der Steam-Bibliotheken (globaloffensive-sharecode) braucht den nicht-strikten Modus.

// Bibliotheken (steam-session) schreiben mit DEBUG=… alles ins Protokoll, auch Steam-Guard-Codes – nie zulassen.
delete process.env.DEBUG;
// Gebündelt gilt alles als eigener Code: Node würde sonst bei jedem Lauf vor altem Code der Bibliotheken warnen
// (new Buffer() in globaloffensive-sharecode), was im Protokoll nur stört.
process.noDeprecation = true;
// Valve ist über IPv4 erreichbar; Node 18 probiert sonst nicht IPv4, wenn IPv6 beim Anbieter halb kaputt ist
try {
	require('dns').setDefaultResultOrder('ipv4first');
} catch (e) {
	// ältere Node-Versionen: egal
}

const fs = require('fs');
const path = require('path');
const log = require('./log');
const {abort, fmtDuration, fmtBytes, request, netText} = require('./util');

const VERSION = process.env.FRAGDESK_FETCH_VERSION || 'dev';   // beim Bündeln aus app.json eingesetzt

const DEFAULTS = {server: 'http://127.0.0.1:7420', data: '/var/lib/fragdesk', analyzer: '/usr/local/lib/fragdesk/fdanalyze'};

function usage(out) {
	out.write(`fragdesk-fetch ${VERSION} – holt neue CS2-Demos bei Valve für den FragDesk-Server

  node --jitless fragdesk-fetch.js run   [--server URL] [--data DIR] [--analyzer DATEI] [--verbose]
  node --jitless fragdesk-fetch.js check [--server URL] [--data DIR] [--analyzer DATEI] [--offline]
  node fragdesk-fetch.js version

Standard: --server ${DEFAULTS.server} --data ${DEFAULTS.data} --analyzer ${DEFAULTS.analyzer}
Läuft normalerweise als systemd-Dienst fragdesk-fetch.service (Protokoll: journalctl -u fragdesk-fetch).
`);
}

function parseArgs(argv) {
	let o = Object.assign({cmd: argv[0] || 'help', verbose: false, offline: false}, DEFAULTS);
	for (let i = 1; i < argv.length; i++) {
		let a = argv[i];
		let next = () => {
			if (i + 1 >= argv.length) {
				log.error(`${a} braucht einen Wert`);
				process.exit(2);
			}
			return argv[++i];
		};
		if (a == '--server') {
			o.server = next();
		} else if (a == '--data') {
			o.data = next();
		} else if (a == '--analyzer') {
			o.analyzer = next();
		} else if (a == '--verbose' || a == '-v') {
			o.verbose = true;
		} else if (a == '--offline') {
			o.offline = true;
		} else if (a == '--help' || a == '-h') {
			o.cmd = 'help';
		} else {
			log.error(`unbekannte Option ${a}`);
			usage(process.stderr);
			process.exit(2);
		}
	}
	return o;
}

function readKey(data) {
	let file = path.join(data, 'local.key');
	try {
		let key = fs.readFileSync(file, 'utf8').trim();
		if (!key) {
			throw new Error('leer');
		}
		return key;
	} catch (e) {
		throw new Error(`${file} lässt sich nicht lesen (${e.code || e.message}). Als Benutzer fragdesk ausführen, ggf. --data angeben.`);
	}
}

function steamLayer() {
	let fake = process.env.FRAGDESK_FETCH_FAKE;
	if (fake) {
		log.info(`Attrappe statt Steam: ${fake}`);
		return require('./steam-fake').load(fake);
	}
	return require('./steam-real');
}

async function cmdRun(o) {
	let {LocalApi} = require('./local-api');
	let {Runner} = require('./run');
	let steam = steamLayer();
	let fetchDir = path.join(o.data, 'fetch');
	if (!fs.existsSync(fetchDir)) {
		try {
			fs.mkdirSync(fetchDir, {recursive: true, mode: 0o700});
		} catch (e) {
			throw new Error(`${fetchDir} fehlt und lässt sich nicht anlegen (${e.code}). Legt der Server ihn an? Server aktualisieren.`);
		}
	}
	let api = new LocalApi({server: o.server, key: readKey(o.data)});
	log.info(`fragdesk-fetch ${VERSION} (Node ${process.versions.node}): Lauf beginnt.`);
	if (process.getuid && process.getuid() == 0) {
		log.warn('Läuft als root – besser als Benutzer fragdesk starten (sudo -u fragdesk …), sonst gehören Dateien in fetch/ danach root.');
	}
	try {
		let runner = new Runner({api, steam, data: o.data, analyzer: o.analyzer});
		let code = await runner.run();
		log.info(`fragdesk-fetch: fertig nach ${fmtDuration(Date.now() - runner.started)}.`);
		return code;
	} finally {
		api.close();
	}
}

// ---- check: alles prüfen, was beim ersten echten Lauf schiefgehen kann ----------------------------------------------
async function cmdCheck(o) {
	let ok = true;
	let line = (good, text) => {
		ok = ok && good !== false;
		process.stdout.write(`${good === false ? 'FEHLT ' : good === null ? '  –   ' : '  ok  '} ${text}\n`);
	};
	let major = Number(process.versions.node.split('.')[0]);
	line(major >= 18, `Node ${process.versions.node}${major >= 18 ? '' : ' – mindestens 18 nötig'}${process.execArgv.includes('--jitless') ? ' (--jitless)' : ''}`);

	// Steam-Bibliotheken (gebündelt) laden, Objekte anlegen (ohne Netz) und 64-Bit-Match-IDs durch die Protobufs schicken
	try {
		let {LoginSession, EAuthTokenPlatformType} = require('steam-session');
		let SteamUser = require('steam-user');
		let GlobalOffensive = require('globaloffensive');
		let user = new SteamUser({dataDirectory: null, autoRelogin: false});
		new GlobalOffensive(user);   // prüft Name und Version von steam-user (package.json muss mitgebündelt sein)
		new LoginSession(EAuthTokenPlatformType.SteamClient).cancelLoginAttempt();
		let Protos = require('globaloffensive/protobufs/generated/_load.js');
		let {ShareCode} = require('globaloffensive-sharecode');
		let {decodeShareCode, encodeShareCode} = require('./util');
		let id = '3717052436717584506';
		let code = encodeShareCode(id, '3717058124424299188', 4242);
		let lib = new ShareCode(code).decode();
		let mine = decodeShareCode(code);
		let T = Protos.CMsgGCCStrike15_v2_MatchListRequestFullGameInfo;
		let back = T.toObject(T.decode(T.encode({matchid: lib.matchId, outcomeid: lib.outcomeId, token: Number(lib.token)}).finish()), {longs: String});
		let good = back.matchid == id && lib.matchId == id && mine.matchId == id && lib.token == mine.token;
		line(good, `Steam-Bibliotheken (steam-user, steam-session, globaloffensive)${good ? '' : ' – 64-Bit-IDs gehen kaputt'}`);
	} catch (e) {
		line(false, `Steam-Bibliotheken: ${e.message}`);
	}

	let {analyzerVersion} = require('./analyze');
	let v = await analyzerVersion(o.analyzer);
	line(v.ok, `fdanalyze: ${o.analyzer} – ${v.text}`);

	let fetchDir = path.join(o.data, 'fetch');
	try {
		fs.accessSync(fetchDir, fs.constants.W_OK);
		line(true, `${fetchDir} beschreibbar`);
	} catch (e) {
		line(false, `${fetchDir}: ${e.code}`);
	}
	let {freeBytes} = require('./download');
	let free = freeBytes(fs.existsSync(fetchDir) ? fetchDir : o.data);
	line(free < 0 ? null : free > 2 * 1073741824, `freier Platz: ${free < 0 ? 'unbekannt' : fmtBytes(free)}`);

	try {
		readKey(o.data);
		line(true, `${path.join(o.data, 'local.key')} lesbar`);
		let r = await request(new URL('/api/v1/health', o.server).toString(), {timeoutMs: 5000});
		line(r.status == 200, `fragdesk-server ${o.server}: Status ${r.status}`);
	} catch (e) {
		line(false, e.network ? `fragdesk-server ${o.server}: ${netText(e)}` : e.message);
	}

	if (!o.offline) {
		for (let [what, url] of [
			['Steam-Web-API', 'https://api.steampowered.com/ISteamWebAPIUtil/GetServerInfo/v1/'],
			['Steam-Server (CM-Liste)', 'https://api.steampowered.com/ISteamDirectory/GetCMListForConnect/v1/?cellid=0&cmtype=websockets']
		]) {
			try {
				let r = await request(url, {timeoutMs: 15000, maxBytes: 4 << 20});
				let extra = '';
				if (url.includes('GetCMList')) {
					try {
						extra = `, ${JSON.parse(r.body.toString('utf8')).response.serverlist.length} Server`;
					} catch (e) {
						extra = '';
					}
				}
				line(r.status == 200, `${what}: Status ${r.status}${extra}`);
			} catch (e) {
				line(false, `${what}: ${netText(e)}`);
			}
		}
	}
	process.stdout.write(ok ? 'Alles bereit.\n' : 'Nicht alles bereit – siehe oben.\n');
	return ok ? 0 : 1;
}

async function main() {
	let o = parseArgs(process.argv.slice(2));
	log.setVerbose(o.verbose || log.verbose);
	let stopping = 0;
	let stop = (sig) => {
		stopping++;
		if (stopping > 1) {
			process.exit(130);
		}
		log.warn(`${sig} erhalten – breche ab und räume auf …`);
		abort.cancel();
	};
	process.on('SIGTERM', () => stop('SIGTERM'));
	process.on('SIGINT', () => stop('SIGINT'));
	// Eine Bibliothek, die unterwegs etwas wirft, soll den Lauf nicht stumm beenden
	process.on('unhandledRejection', (err) => {
		log.error(`Unerwarteter Fehler: ${(err && err.stack) || err}`.slice(0, 2000));
	});
	process.on('uncaughtException', (err) => {
		log.error(`Unerwarteter Fehler: ${(err && err.stack) || err}`.slice(0, 2000));
		process.exit(1);
	});

	switch (o.cmd) {
		case 'run':
			return cmdRun(o);
		case 'check':
			return cmdCheck(o);
		case 'version':
		case '--version':
			process.stdout.write(`fragdesk-fetch ${VERSION} (Node ${process.versions.node})\n`);
			return 0;
		case 'help':
		case '--help':
		case '-h':
			usage(process.stdout);
			return 0;
		default:
			log.error(`unbekannter Befehl ${o.cmd}`);
			usage(process.stderr);
			return 2;
	}
}

main().then(
	(code) => process.exit(code || 0),   // ausdrücklich beenden: Bibliotheken lassen Zeitgeber und Verbindungen offen
	(err) => {
		if (err && err.cancelled) {
			log.warn('Lauf abgebrochen.');
			process.exit(143);
		}
		log.error(err && err.fatal !== undefined ? err.message : `${(err && err.stack) || err}`.slice(0, 2000));
		process.exit(1);
	}
);
