// Tests: ganzer Lauf im Prozess – nachgebauter fragdesk-server, Attrappe für Steam, falsches fdanalyze, Demo-Server.
// Freigabecode-Kette, Hochladen und /state, Fehlversuche (3 je Code), 507, 403/412, falscher Schlüssel, gleiche Matches
// mehrerer Konten, gelöschte Demos, Token-Erneuerung, Bot-Fehler, Anmeldung im Lauf, Sperre, wake, Server weg.
'use strict';

const test = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const path = require('path');
const {Runner, demoName} = require('../src/run');
const {LocalApi} = require('../src/local-api');
const fake = require('../src/steam-fake');
const {abort, decodeShareCode, now, decodeJwt} = require('../src/util');
const {tmpDir, FakeServer, writeFakeAnalyzer, demoServer, captureLog, shareCode} = require('./helpers');

const root = tmpDir('run');
const analyzer = writeFakeAnalyzer(root);
const analyzerLog = path.join(root, 'analyzer.jsonl');
process.env.FAKE_ANALYZER_LOG = analyzerLog;

const A = 'aaaaaaa1';
const B = 'bbbbbbb2';
const SID_A = '76561190000000001';
const SID_B = '76561190000000002';
const AUTH_A = 'AAAA-BBBBB-CCCC';
const AUTH_B = 'DDDD-EEEEE-FFFF';
const KEY = '0123456789ABCDEF0123456789ABCDEF';
const OLD = now() - 5 * 86400;   // vor 5 Tagen gespielt

const codes = Array.from({length: 30}, (_, i) => shareCode(i + 1));
const demoBytes = (code) => Buffer.concat([Buffer.from('BZh91AY&SY'), Buffer.from(code), Buffer.alloc(3000, code.charCodeAt(7))]);

let demos;
let routes = {};
test.before(async () => {
	demos = await demoServer(routes);
});
test.after(() => demos.close());
test.beforeEach(() => abort.reset());

// Demo für einen Code bereitstellen → Adresse wie bei Valve (/730/<matchid>_<n>.dem.bz2)
function serve(code, opts = {}) {
	let id = decodeShareCode(code).matchId;
	let p = `/730/${id.padStart(21, '0')}_${1000 + codes.indexOf(code)}.dem.bz2`;
	routes[p] = Object.assign({data: demoBytes(code)}, opts);
	return demos.url(p);
}

function bot() {
	return {refreshToken: fake.fakeToken('76561199000000009', 100), apiKey: KEY};
}

async function setup({accounts, webapi, gc = {}, botFx = {}, login, jobBot, extra = {}} = {}) {
	let data = fs.mkdtempSync(path.join(root, 'data-'));
	fs.mkdirSync(path.join(data, 'fetch'), {mode: 0o700});
	let server = await new FakeServer({job: {bot: jobBot || bot(), login: login || null, accounts}}).start();
	fs.writeFileSync(path.join(data, 'local.key'), server.key + '\n');
	let record = path.join(data, 'calls.jsonl');
	let fixtureFile = path.join(data, 'fake.json');
	let ctx = {
		data,
		server,
		record,
		fixture: Object.assign({record, apiKey: KEY, webapi, gc, bot: botFx, login: {}}, extra),
		calls(what) {
			if (!fs.existsSync(record)) {
				return [];
			}
			return fs.readFileSync(record, 'utf8').trim().split('\n').map((l) => JSON.parse(l)).filter((c) => !what || c.what == what);
		},
		helperState() {
			let f = path.join(data, 'fetch', 'state.json');
			return fs.existsSync(f) ? JSON.parse(fs.readFileSync(f, 'utf8')) : null;
		},
		lastState(id) {
			let s = server.states[id] || [];
			return s[s.length - 1];
		},
		async run(analyzerPath) {
			fs.writeFileSync(fixtureFile, JSON.stringify(ctx.fixture));
			let api = new LocalApi({server: server.url, key: server.key, retries: 1, retryMs: 10});
			let runner = new Runner({api, steam: fake.load(fixtureFile), data, analyzer: analyzerPath || analyzer});
			try {
				return await runner.run();
			} finally {
				api.close();
			}
		},
		async done() {
			await server.stop();
		}
	};
	return ctx;
}

function account(id, steamId, authCode, knownCode) {
	return {id, steamId, authCode, knownCode};
}

function analyzerCalls() {
	return fs.existsSync(analyzerLog) ? fs.readFileSync(analyzerLog, 'utf8').trim().split('\n').length : 0;
}

test('Kette → Match-Infos → Demo → fdanalyze → json/heat/replay hochladen → /state; zweiter Lauf: nichts Neues', async () => {
	let [c0, c1, c2, c3] = codes;
	let gc = {};
	for (let c of [c1, c2, c3]) {
		gc[c] = {url: serve(c), matchtime: OLD + codes.indexOf(c) * 3600};
	}
	let ctx = await setup({accounts: [account(A, SID_A, AUTH_A, c0)], webapi: {[SID_A]: {chain: [c0, c1, c2, c3], authCode: AUTH_A}}, gc});
	let cap = captureLog();
	try {
		assert.strictEqual(await ctx.run(), 0);
		let files = ctx.server.files[A];
		assert.strictEqual(Object.keys(files).length, 3);
		for (let f of Object.values(files)) {
			assert.ok(f.json && f.heat && f.replay, 'alle drei Teile');
		}
		// nach jedem Import: woher die Demo kam (für „Demo von Valve laden“ in FragDesk) – zum richtigen Match
		let sources = ctx.server.sources[A] || {};
		assert.deepStrictEqual(Object.keys(sources).sort(), Object.keys(files).sort());
		for (let [checksum, s] of Object.entries(sources)) {
			let c = [c1, c2, c3].find((x) => gc[x].url == s.url);
			assert.ok(c, `Adresse der Demo von ${checksum}`);
			assert.deepStrictEqual(s, {url: gc[c].url, matchtime: gc[c].matchtime, shareCode: c});
			let demo = demoBytes(c);   // falsches fdanalyze: Prüfsumme = SHA-256 der Demo, gekürzt
			assert.strictEqual(checksum, require('crypto').createHash('sha256').update(demo).digest('hex').slice(0, 16), 'Quelle gehört zu diesem Match');
		}
		let order = ctx.server.calls.filter((x) => x.path.includes('/match/')).map((x) => x.path.split('/').pop());
		assert.deepStrictEqual(order, ['json', 'heat', 'replay', 'source', 'json', 'heat', 'replay', 'source', 'json', 'heat', 'replay', 'source'],
			'Quelle erst nach den Dateien');
		let states = ctx.server.states[A];
		assert.deepStrictEqual(states.map((s) => s.knownCode), [c1, c2, c3, c3]);
		assert.deepStrictEqual(states.map((s) => s.from), [c0, c1, c2, c3]);
		assert.deepStrictEqual(states.map((s) => s.fetched), [1, 1, 1, undefined]);
		assert.ok(states.slice(0, 3).every((s) => s.lastNew > 0 && s.lastError === ''));
		assert.strictEqual(states[3].lastError, '');
		assert.strictEqual(ctx.server.fetched[A], 3);
		assert.strictEqual(ctx.server.job.accounts[0].knownCode, c3);
		assert.deepStrictEqual(ctx.server.runs.map((r) => r.phase), ['start', 'end']);
		assert.strictEqual(ctx.server.runs[1].text, '1 Konto geprüft · 3 neue Matches');
		// Datum der Demo = Spielzeit (fdanalyze nimmt ohne .info das Dateidatum)
		let first = JSON.parse(files[Object.keys(files)[0]].json.data.toString());
		assert.ok([c1, c2, c3].map((c) => new Date(gc[c].matchtime * 1000).toISOString().replace('.000', '')).includes(first.match.date));
		// Abfolge bei Steam: Web-API, dann einmal Bot, Lizenz, GC, drei Anfragen, abmelden
		assert.deepStrictEqual(ctx.calls().map((c) => c.what), ['webapi', 'webapi', 'webapi', 'webapi', 'bot-logon', 'license', 'gc-connect', 'gc-request', 'gc-request', 'gc-request', 'bot-logoff']);
		// tmp ist leer, Sperre weg
		assert.deepStrictEqual(fs.readdirSync(path.join(ctx.data, 'fetch', 'tmp')), []);
		assert.ok(!fs.existsSync(path.join(ctx.data, 'fetch', 'lock')));
		// keine Geheimnisse im Protokoll
		assert.doesNotMatch(cap.text(), new RegExp([AUTH_A, KEY, ctx.server.key].join('|')));

		// zweiter Lauf: nur eine Web-API-Frage, kein Bot, Lizenz nicht noch einmal
		fs.unlinkSync(ctx.record);
		await ctx.run();
		assert.deepStrictEqual(ctx.calls().map((c) => c.what), ['webapi']);
		assert.deepStrictEqual(ctx.lastState(A), Object.assign({}, ctx.lastState(A), {knownCode: c3, lastError: ''}));
		assert.strictEqual(ctx.server.runs[3].text, '1 Konto geprüft · nichts Neues');
	} finally {
		cap.stop();
		await ctx.done();
	}
});

test('Fehlversuche: knownCode bleibt, höchstens 3 je Code, dann überspringen und weiter', async () => {
	let [c0, c1, c2] = codes.slice(3);
	let gc = {[c1]: {url: serve(c1, {status: 500}), matchtime: OLD}, [c2]: {url: serve(c2), matchtime: OLD + 60}};
	let ctx = await setup({accounts: [account(A, SID_A, AUTH_A, c0)], webapi: {[SID_A]: {chain: [c0, c1, c2]}}, gc});
	try {
		for (let n = 1; n <= 2; n++) {
			await ctx.run();
			let s = ctx.lastState(A);
			assert.strictEqual(s.knownCode, c0, `Lauf ${n}: knownCode bleibt`);
			assert.match(s.lastError, new RegExp(`^Match vom .*: Download: Valve antwortet mit Status 500 \\(Versuch ${n} von 3 – der nächste Lauf versucht es wieder\\)$`));
			assert.strictEqual(ctx.helperState().accounts[A].attempts.count, n);
			assert.strictEqual(ctx.server.files[A], undefined, 'nichts hochgeladen');
		}
		await ctx.run();
		let states = ctx.server.states[A];
		let last3 = states.slice(-3);
		assert.strictEqual(last3[0].knownCode, c1, 'übersprungen');
		assert.match(last3[0].lastError, /nach 3 Fehlversuchen übersprungen: Download: Valve antwortet mit Status 500/);
		assert.strictEqual(last3[1].knownCode, c2);
		assert.strictEqual(last3[1].fetched, 1);
		assert.strictEqual(last3[2].knownCode, c2);
		assert.ok(!ctx.helperState().accounts[A] || !ctx.helperState().accounts[A].attempts, 'Zähler gelöscht');
		assert.strictEqual(Object.keys(ctx.server.files[A]).length, 1);
	} finally {
		await ctx.done();
	}
});

test('Game Coordinator stumm, Bot spielt woanders, Token ungültig: kein Fehlversuch, knownCode bleibt', async () => {
	let [c0, c1, c2] = codes.slice(6);
	let gc = {[c1]: {url: serve(c1), matchtime: OLD}, [c2]: {url: serve(c2), matchtime: OLD}};
	for (let [botFx, re, loginError] of [
		[{gc: 'down'}, /Game Coordinator von CS2 antwortet nicht/, false],
		[{playingBlocked: true}, /läuft gerade ein Spiel auf einem anderen Gerät/, false],
		[{error: 'AccessDenied'}, /Anmeldung des Steam-Bots gilt nicht mehr/, true],
		[{error: 'ServiceUnavailable'}, /Steam ist gerade nicht erreichbar/, false]
	]) {
		let ctx = await setup({accounts: [account(A, SID_A, AUTH_A, c0)], webapi: {[SID_A]: {chain: [c0, c1, c2]}}, gc, botFx});
		try {
			await ctx.run();
			let s = ctx.lastState(A);
			assert.strictEqual(s.knownCode, c0);
			assert.match(s.lastError, re);
			let hs = ctx.helperState();
			assert.ok(!hs.accounts[A] || !hs.accounts[A].attempts, 'kein Fehlversuch gezählt');
			assert.strictEqual(ctx.server.logins.length, loginError ? 1 : 0);
			if (loginError) {
				assert.strictEqual(ctx.server.logins[0].state, 'error');
				assert.match(ctx.server.logins[0].hint, /neu anmelden/);
			}
		} finally {
			await ctx.done();
		}
	}
});

test('Game Coordinator begrüßt, schweigt aber zu einem einzelnen Code: zählt als Fehlversuch (sonst hinge der Code ewig)', async () => {
	let [c0, c1] = [shareCode(200), shareCode(201)];
	let ctx = await setup({accounts: [account(A, SID_A, AUTH_A, c0)], webapi: {[SID_A]: {chain: [c0, c1]}}, gc: {}});
	try {
		await ctx.run();
		assert.match(ctx.lastState(A).lastError, /liefert zu diesem Match keine Infos \(Versuch 1 von 3/);
		assert.strictEqual(ctx.helperState().accounts[A].attempts.count, 1);
	} finally {
		await ctx.done();
	}
});

test('Speicher voll (507): Konto stoppt, eine Stunde Pause, danach wieder', async () => {
	let [c0, c1] = codes.slice(9);
	let ctx = await setup({accounts: [account(A, SID_A, AUTH_A, c0)], webapi: {[SID_A]: {chain: [c0, c1]}}, gc: {[c1]: {url: serve(c1), matchtime: OLD}}});
	try {
		ctx.server.full.add(A);
		await ctx.run();
		let s = ctx.lastState(A);
		assert.strictEqual(s.knownCode, c0);
		assert.match(s.lastError, /Dein Speicher auf dem Server ist voll/);
		assert.ok(ctx.helperState().accounts[A].full);
		assert.ok(!ctx.helperState().accounts[A].attempts, 'kein Fehlversuch');
		assert.match(ctx.server.runs[1].text, /Speicher voll/);
		// nächster Lauf innerhalb der Stunde: nicht einmal die Web-API
		fs.unlinkSync(ctx.record);
		await ctx.run();
		assert.deepStrictEqual(ctx.calls(), []);
		assert.match(ctx.lastState(A).lastError, /Speicher/);
		// eine Stunde später, Platz wieder frei
		let f = path.join(ctx.data, 'fetch', 'state.json');
		let st = JSON.parse(fs.readFileSync(f, 'utf8'));
		st.accounts[A].full.at -= 3601;
		fs.writeFileSync(f, JSON.stringify(st));
		ctx.server.full.delete(A);
		await ctx.run();
		assert.strictEqual(ctx.lastState(A).knownCode, c1);
		assert.strictEqual(ctx.lastState(A).lastError, '');
	} finally {
		await ctx.done();
	}
});

test('403/412: deutscher Text, nicht weiter fragen bis sich die Angaben ändern; falscher API-Schlüssel gilt für alle', async () => {
	let [c0, c1] = codes.slice(11);
	let ctx = await setup({
		accounts: [account(A, SID_A, 'XXXX-XXXXX-XXXX', c0), account(B, SID_B, AUTH_B, shareCode(99))],
		webapi: {[SID_A]: {chain: [c0, c1], authCode: AUTH_A}, [SID_B]: {chain: [c0, c1]}},
		gc: {[c1]: {url: serve(c1), matchtime: OLD}}
	});
	try {
		await ctx.run();
		assert.match(ctx.lastState(A).lastError, /Authentifizierungscode und SteamID passen nicht zusammen/);
		assert.match(ctx.lastState(B).lastError, /Freigabecode gehört nicht zu diesem Steam-Konto/);
		assert.strictEqual(ctx.calls('webapi').length, 2);
		fs.unlinkSync(ctx.record);
		await ctx.run();
		assert.strictEqual(ctx.calls('webapi').length, 0, 'gesperrt, solange die Angaben gleich sind');
		assert.match(ctx.lastState(A).lastError, /Authentifizierungscode/);
		// Nutzer korrigiert den Code → sofort wieder gefragt, Match geladen
		ctx.server.job.accounts[0].authCode = AUTH_A;
		await ctx.run();
		assert.strictEqual(ctx.lastState(A).knownCode, c1);
		assert.strictEqual(ctx.lastState(A).lastError, '');

		// falscher Web-API-Schlüssel: eine Anfrage, dann für alle Konten derselbe Text
		ctx.server.job.accounts[0].knownCode = c0;
		ctx.server.job.bot.apiKey = 'FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF';
		fs.unlinkSync(ctx.record);
		await ctx.run();
		assert.strictEqual(ctx.calls('webapi').length, 1);
		assert.match(ctx.lastState(A).lastError, /Web-API-Schlüssel des Steam-Bots gilt nicht/);
		assert.match(ctx.server.runs[ctx.server.runs.length - 1].text, /Web-API-Schlüssel ungültig/);
	} finally {
		await ctx.done();
	}
});

test('zwei Konten im selben Match: eine Demo, eine Auswertung, beide bekommen das Match', async () => {
	let [c0, c1, c2] = codes.slice(13);
	let url = serve(c1);
	let p = new URL(url).pathname;
	let ctx = await setup({
		accounts: [account(A, SID_A, AUTH_A, c0), account(B, SID_B, AUTH_B, c0)],
		webapi: {[SID_A]: {chain: [c0, c1]}, [SID_B]: {chain: [c0, c1, c2]}},
		gc: {[c1]: {url, matchtime: OLD}, [c2]: {url: serve(c2), matchtime: OLD + 99}}
	});
	try {
		let hits = demos.hits[p] || 0;
		let calls = analyzerCalls();
		await ctx.run();
		assert.strictEqual(demos.hits[p] - hits, 1, 'Demo einmal geladen');
		assert.strictEqual(analyzerCalls() - calls, 2, 'c1 einmal, c2 einmal ausgewertet');
		let sumA = Object.keys(ctx.server.files[A]);
		let sumB = Object.keys(ctx.server.files[B]);
		assert.strictEqual(sumA.length, 1);
		assert.strictEqual(sumB.length, 2);
		assert.ok(sumB.includes(sumA[0]));
		assert.deepStrictEqual(ctx.server.sources[A][sumA[0]], {url, matchtime: OLD, shareCode: c1}, 'beide Konten bekommen die Quelle');
		assert.deepStrictEqual(ctx.server.sources[B][sumA[0]], {url, matchtime: OLD, shareCode: c1});
		assert.strictEqual(ctx.calls('gc-request').length, 2, 'jeder Code einmal beim Game Coordinator');
		assert.strictEqual(ctx.server.runs[1].text, '2 Konten geprüft · 3 neue Matches');
	} finally {
		await ctx.done();
	}
});

test('keine Demo-Adresse: alt → überspringen (knownCode weiter), frisch → Fehlversuch; 404 genauso', async () => {
	let [c0, c1, c2, c3] = codes.slice(16);
	let ctx = await setup({
		accounts: [account(A, SID_A, AUTH_A, c0)],
		webapi: {[SID_A]: {chain: [c0, c1, c2, c3]}},
		gc: {
			[c1]: {url: null, matchtime: OLD},                         // gelöscht
			[c2]: {url: serve(c2, {status: 404}), matchtime: OLD},     // gelöscht (404)
			[c3]: {url: null, matchtime: now() - 600}                   // gerade gespielt, Demo noch nicht da
		}
	});
	try {
		await ctx.run();
		let st = ctx.server.states[A];
		assert.deepStrictEqual(st.map((s) => s.knownCode), [c1, c2, c2]);
		assert.strictEqual(st[0].lastError, '');
		assert.strictEqual(st[0].fetched, undefined);
		assert.match(st[2].lastError, /Die Demo ist bei Valve noch nicht bereit \(Versuch 1 von 3/);
		assert.match(ctx.server.runs[1].text, /2 übersprungen/);
	} finally {
		await ctx.done();
	}
});

test('Token-Erneuerung durch Steam → /token mit Ablauf aus dem JWT', async () => {
	let [c0, c1] = codes.slice(20);
	let ctx = await setup({accounts: [account(A, SID_A, AUTH_A, c0)], webapi: {[SID_A]: {chain: [c0, c1]}}, gc: {[c1]: {url: serve(c1), matchtime: OLD}}, botFx: {renewToken: true}});
	try {
		await ctx.run();
		assert.strictEqual(ctx.server.tokens.length, 1);
		let t = ctx.server.tokens[0];
		assert.strictEqual(t.tokenUntil, decodeJwt(t.refreshToken).exp);
		assert.strictEqual(ctx.server.job.bot.refreshToken, t.refreshToken);
	} finally {
		await ctx.done();
	}
});

test('Anmeldung im Lauf: need-code → Admin gibt Code ein → done → gleich mit dem neuen Token weiter', async () => {
	let [c0, c1] = codes.slice(22);
	let ctx = await setup({
		accounts: [account(A, SID_A, AUTH_A, c0)],
		webapi: {[SID_A]: {chain: [c0, c1]}},
		gc: {[c1]: {url: serve(c1), matchtime: OLD}},
		jobBot: {refreshToken: '', apiKey: KEY},
		login: {user: 'fragbot', password: 'geheim123', code: ''},
		extra: {login: {password: 'geheim123', actions: ['email'], code: 'K7X2Q', accountName: 'fragbot'}, timing: {pollMs: 20}}
	});
	ctx.server.onLogin = (body) => {
		if (body.state == 'need-code') {
			setTimeout(() => {
				ctx.server.pendingCode = 'K7X2Q';
			}, 50);
		}
	};
	let cap = captureLog();
	try {
		await ctx.run();
		assert.deepStrictEqual(ctx.server.logins.map((l) => l.state), ['working', 'need-code', 'working', 'done']);
		let done = ctx.server.logins[3];
		assert.strictEqual(done.botName, 'fragbot');
		assert.ok(done.refreshToken && done.tokenUntil > now());
		assert.strictEqual(ctx.lastState(A).knownCode, c1, 'im selben Lauf geladen');
		assert.ok(ctx.calls().findIndex((c) => c.what == 'bot-logon') > ctx.calls().findIndex((c) => c.what == 'login-code'));
		assert.doesNotMatch(cap.text(), /geheim123|K7X2Q/);
	} finally {
		cap.stop();
		await ctx.done();
	}
});

test('ohne Bot oder ohne Web-API-Schlüssel: nichts tun; ungültige Angaben im Auftrag', async () => {
	let ctx = await setup({accounts: [account(A, SID_A, AUTH_A, codes[0])], webapi: {}, jobBot: {refreshToken: '', apiKey: KEY}});
	try {
		await ctx.run();
		assert.deepStrictEqual(ctx.calls(), []);
		assert.strictEqual(ctx.server.runs[1].text, 'Kein Steam-Bot angemeldet – nichts zu tun');
		ctx.server.job.bot = {refreshToken: bot().refreshToken, apiKey: ''};
		await ctx.run();
		assert.strictEqual(ctx.server.runs[3].text, 'Der Web-API-Schlüssel des Bots fehlt – nichts zu tun');
		ctx.server.job.bot = bot();
		ctx.server.job.accounts = [account(A, SID_A, 'kaputt', codes[0]), account(B, '123', AUTH_B, codes[0])];
		await ctx.run();
		assert.deepStrictEqual(ctx.calls(), []);
		assert.match(ctx.lastState(A).lastError, /Form XXXX-XXXXX-XXXX/);
		assert.match(ctx.lastState(B).lastError, /SteamID/);
	} finally {
		await ctx.done();
	}
});

test('Heatmap abgelehnt: Match trotzdem da; Auswertung abgelehnt (400): Fehlversuch; fdanalyze fehlt: kein Fehlversuch, kein Download', async () => {
	let [c0, c1] = codes.slice(24);
	let url = serve(c1);
	let ctx = await setup({accounts: [account(A, SID_A, AUTH_A, c0)], webapi: {[SID_A]: {chain: [c0, c1]}}, gc: {[c1]: {url, matchtime: OLD}}});
	try {
		let hits = demos.hits[new URL(url).pathname] || 0;
		await ctx.run(path.join(root, 'gibt-es-nicht'));
		assert.strictEqual((demos.hits[new URL(url).pathname] || 0) - hits, 0, 'nicht geladen');
		assert.match(ctx.lastState(A).lastError, /fdanalyze fehlt/);
		assert.ok(!(ctx.helperState().accounts[A] || {}).attempts);

		ctx.server.reject.json = 400;
		await ctx.run();
		assert.match(ctx.lastState(A).lastError, /Der Server nimmt die Auswertung nicht an \(abgelehnt \(json\)\) \(Versuch 1 von 3/);

		delete ctx.server.reject.json;
		ctx.server.reject.heat = 400;
		ctx.server.reject.source = 404;   // älterer Server ohne Demo-Quelle: kein Fehler des Matches
		let cap = captureLog();
		try {
			await ctx.run();
		} finally {
			cap.stop();
		}
		assert.strictEqual(ctx.lastState(A).knownCode, c1);
		assert.strictEqual(ctx.lastState(A).lastError, '');
		let f = Object.values(ctx.server.files[A])[0];
		assert.ok(f.json && f.replay && !f.heat);
		assert.ok(cap.has(/Heatmap nicht angenommen/));
		assert.ok(cap.has(/Adresse der Demo nicht beim Server angekommen \(Unbekannte Anfrage\) – das Match ist trotzdem da/));
		assert.strictEqual(ctx.server.sources[A], undefined);
	} finally {
		await ctx.done();
	}
});

test('Sperre: zweiter Lauf beendet sich sofort; alte Sperre (toter Prozess) wird übernommen', async () => {
	let ctx = await setup({accounts: [], webapi: {}});
	let lock = path.join(ctx.data, 'fetch', 'lock');
	try {
		fs.writeFileSync(lock, JSON.stringify({pid: process.pid, boot: require('../src/state').bootId(), started: now()}));
		let cap = captureLog();
		await ctx.run();
		cap.stop();
		assert.ok(cap.has(/anderer Lauf ist noch aktiv/));
		assert.strictEqual(ctx.server.calls.length, 0, 'nicht einmal den Auftrag geholt');
		assert.ok(fs.existsSync(lock), 'fremde Sperre bleibt');

		fs.writeFileSync(lock, JSON.stringify({pid: 2147483646, boot: require('../src/state').bootId(), started: now()}));
		await ctx.run();
		assert.ok(ctx.server.calls.length > 0);
		assert.ok(!fs.existsSync(lock));
	} finally {
		await ctx.done();
	}
});

test('wake ändert sich im Lauf → genau einmal von vorn', async () => {
	let ctx = await setup({accounts: [account(A, SID_A, AUTH_A, codes[27])], webapi: {[SID_A]: {chain: [codes[27]]}}});
	let wake = path.join(ctx.data, 'fetch', 'wake');
	fs.writeFileSync(wake, '1');
	let n = 0;
	let orig = ctx.server.handle.bind(ctx.server);
	ctx.server.handle = async (req, res) => {
		if (req.url.endsWith('/run')) {
			fs.writeFileSync(wake, String(++n + 100));   // jeder Laufstand weckt erneut
		}
		return orig(req, res);
	};
	ctx.server.server.removeAllListeners('request');
	ctx.server.server.on('request', (req, res) => ctx.server.handle(req, res));
	try {
		await ctx.run();
		assert.deepStrictEqual(ctx.server.runs.map((r) => r.phase), ['start', 'end', 'start', 'end']);
	} finally {
		await ctx.done();
	}
});

test('Server weg mitten im Lauf: Lauf endet mit Fehler, Sperre frei; nächster Lauf macht weiter', async () => {
	let [c0, c1, c2] = codes.slice(26, 29);
	let ctx = await setup({accounts: [account(A, SID_A, AUTH_A, c0)], webapi: {[SID_A]: {chain: [c0, c1, c2]}}, gc: {[c1]: {url: serve(c1), matchtime: OLD}, [c2]: {url: serve(c2), matchtime: OLD}}});
	let orig = ctx.server.handle.bind(ctx.server);
	let puts = 0;
	ctx.server.server.removeAllListeners('request');
	ctx.server.server.on('request', (req, res) => {
		if (req.method == 'PUT' && ++puts == 5) {   // zweites Match, erster Teil (erstes: json, heat, replay, source)
			ctx.server.down = true;
		}
		return orig(req, res);
	});
	try {
		await assert.rejects(ctx.run(), /startet gerade neu/);
		assert.ok(!fs.existsSync(path.join(ctx.data, 'fetch', 'lock')));
		assert.deepStrictEqual(fs.readdirSync(path.join(ctx.data, 'fetch', 'tmp')), []);
		assert.strictEqual(ctx.server.job.accounts[0].knownCode, c1, 'erstes Match ist durch');
		ctx.server.down = false;
		await ctx.run();
		assert.strictEqual(ctx.server.job.accounts[0].knownCode, c2);
	} finally {
		await ctx.done();
	}
});

test('Dateiname der Demo wie bei CS2', () => {
	assert.strictEqual(demoName('http://replay183.valve.net/730/003717052436717584506_1418282143.dem.bz2', '1'), 'match730_003717052436717584506_1418282143_183.dem.bz2');
	assert.strictEqual(demoName('http://127.0.0.1:7711/730/0042_7.dem.bz2', '1'), 'match730_0042_7.dem.bz2');
	assert.strictEqual(demoName('https://cdn.example/x/y.dem', '99'), 'match730_99.dem');
	assert.strictEqual(demoName('kaputt', '5'), 'match730_5.dem.bz2');
});
