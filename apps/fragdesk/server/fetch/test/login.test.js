// Tests: Anmeldung des Bots (Zustände working → need-code/need-confirm → done/error) mit der Attrappe und einem
// Schein-Server, der den Auftrag liefert (Code erst nach ein paar Abfragen, wie wenn der Admin ihn eintippt)
'use strict';

const test = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const path = require('path');
const {interactiveLogin, prompt} = require('../src/login');
const fake = require('../src/steam-fake');
const {decodeJwt, abort} = require('../src/util');
const {tmpDir, captureLog} = require('./helpers');

const dir = tmpDir('login');

function steamWith(login, timing) {
	let file = path.join(dir, `fx-${Math.random().toString(36).slice(2)}.json`);
	fs.writeFileSync(file, JSON.stringify({login, timing: Object.assign({pollMs: 20, loginMaxMs: 2000, loginStartMs: 2000}, timing || {})}));
	return fake.load(file);
}

// jobs: Liste der login-Objekte, die der Auftrag bei den folgenden Abfragen enthält (danach null)
function stubApi(jobs = []) {
	let api = {
		reports: [],
		polls: 0,
		async login(body) {
			api.reports.push(body);
		},
		async job() {
			let login = api.polls < jobs.length ? jobs[api.polls] : null;
			api.polls++;
			return {bot: {}, login, accounts: []};
		}
	};
	return api;
}

const states = (api) => api.reports.map((r) => r.state);

test.beforeEach(() => abort.reset());

test('E-Mail-Code: need-code (email) → Code aus dem Auftrag → done mit Token, Name, SteamID, Ablauf', async () => {
	let cap = captureLog();
	try {
		let steam = steamWith({password: 'pw', actions: ['email'], detail: 'gmx.de', code: 'ABCDE', accountName: 'fragbot', steamId: '76561199000000042', tokenDays: 180});
		let api = stubApi([null, null, {user: 'fragbot', password: '', code: 'abcde'}]);   // kleingeschrieben – wird groß
		let r = await interactiveLogin({steam, api, login: {user: 'fragbot', password: 'pw'}});
		assert.ok(r, 'angemeldet');
		assert.deepStrictEqual(states(api), ['working', 'need-code', 'working', 'done']);
		assert.strictEqual(api.reports[1].codeType, 'email');
		assert.match(api.reports[1].hint, /gmx\.de/);
		let done = api.reports[3];
		assert.strictEqual(done.botName, 'fragbot');
		assert.strictEqual(done.botSteamId, '76561199000000042');
		assert.strictEqual(done.refreshToken, r.refreshToken);
		let jwt = decodeJwt(done.refreshToken);
		assert.strictEqual(done.tokenUntil, jwt.exp);
		assert.ok(Math.abs(done.tokenUntil - (Date.now() / 1000 + 180 * 86400)) < 60);
		// Passwort und Code nie im Protokoll
		assert.doesNotMatch(cap.text(), /\bpw\b.*|ABCDE/);
	} finally {
		cap.stop();
	}
});

test('falscher Code: Hinweis und neuer Versuch, dann richtig', async () => {
	let steam = steamWith({actions: ['app'], code: 'RIGHT'});
	let api = stubApi([{code: 'WRONG'}, null, {code: 'RIGHT'}]);
	let r = await interactiveLogin({steam, api, login: {user: 'bot', password: 'x'}});
	assert.ok(r);
	assert.deepStrictEqual(states(api), ['working', 'need-code', 'working', 'need-code', 'working', 'done']);
	assert.strictEqual(api.reports[1].codeType, 'app');
	assert.strictEqual(api.reports[3].hint, 'Code falsch – bitte den neuen Code eingeben.');
});

test('fünfmal falscher Code → error „Code falsch.“', async () => {
	let steam = steamWith({actions: ['email'], code: 'RIGHT'});
	let api = stubApi(['A1', 'A2', 'A3', 'A4', 'A5', 'RIGHT'].map((code) => ({code})));
	let r = await interactiveLogin({steam, api, login: {user: 'bot', password: 'x'}});
	assert.strictEqual(r, null);
	let last = api.reports[api.reports.length - 1];
	assert.deepStrictEqual(last, {state: 'error', hint: 'Code falsch.'});
});

test('In der Steam-App bestätigen: need-confirm, dann done ohne Code', async () => {
	let steam = steamWith({actions: ['confirm'], confirmAfterMs: 120});
	let api = stubApi();
	let r = await interactiveLogin({steam, api, login: {user: 'bot', password: 'x'}});
	assert.ok(r);
	assert.deepStrictEqual(states(api), ['working', 'need-confirm', 'done']);
	assert.strictEqual(api.reports[1].codeType, undefined);
	assert.ok(api.polls >= 1, 'Auftrag wird während des Wartens abgefragt');
});

test('Bestätigen oder Code aus der App: need-confirm mit codeType app, Code geht auch', async () => {
	let steam = steamWith({actions: ['app', 'confirm'], code: 'QWERT'});
	let api = stubApi([null, {code: 'QWERT'}]);
	let r = await interactiveLogin({steam, api, login: {user: 'bot', password: 'x'}});
	assert.ok(r);
	assert.strictEqual(api.reports[1].state, 'need-confirm');
	assert.strictEqual(api.reports[1].codeType, 'app');
	assert.strictEqual(api.reports[api.reports.length - 1].state, 'done');
});

test('Fehler: Passwort falsch, zu viele Versuche, Steam nicht erreichbar, Zeit abgelaufen', async () => {
	let cases = [
		[{password: 'richtig'}, {}, 'Benutzername oder Passwort falsch.'],
		[{error: 'RateLimitExceeded'}, {}, 'Zu viele Versuche – in einer Stunde nochmal.'],
		[{error: 'AccountLoginDeniedThrottle'}, {}, 'Zu viele Versuche – in einer Stunde nochmal.'],
		[{error: 'ServiceUnavailable'}, {}, 'Steam ist gerade nicht erreichbar – später noch einmal versuchen.'],
		[{actions: ['email'], code: 'X'}, {loginMaxMs: 150}, 'Zeit abgelaufen – bitte noch einmal anmelden.']
	];
	for (let [login, timing, hint] of cases) {
		let api = stubApi();
		let r = await interactiveLogin({steam: steamWith(login, timing), api, login: {user: 'bot', password: 'falsch'}});
		assert.strictEqual(r, null, hint);
		assert.deepStrictEqual(api.reports[api.reports.length - 1], {state: 'error', hint});
	}
});

test('Code schon im ersten Auftrag (z. B. aus der Authenticator-App): ohne need-code angemeldet', async () => {
	let steam = steamWith({actions: ['app'], code: 'ZZZZZ'});
	let api = stubApi();
	let r = await interactiveLogin({steam, api, login: {user: 'bot', password: 'x', code: 'zzzzz'}});
	assert.ok(r);
	assert.deepStrictEqual(states(api), ['working', 'done']);
});

test('ohne Steam Guard: gleich done', async () => {
	let api = stubApi();
	let r = await interactiveLogin({steam: steamWith({actions: []}), api, login: {user: 'bot', password: 'x'}});
	assert.ok(r);
	assert.deepStrictEqual(states(api), ['working', 'done']);
});

test('neue Zugangsdaten während des Wartens: von vorn mit dem neuen Passwort', async () => {
	let steam = steamWith({password: 'neu', actions: []});
	let api = stubApi();
	// erster Versuch scheitert am Passwort → error; mit wartendem Code-Schritt testen wir den Neustart:
	let steam2 = steamWith({password: 'neu', actions: ['email'], code: 'CODE1'});
	let api2 = stubApi([null, {user: 'bot', password: 'neu'}, {code: 'CODE1'}]);
	// Anmeldung mit altem Passwort scheitert sofort – das ist der normale Fehlerfall
	assert.strictEqual(await interactiveLogin({steam, api, login: {user: 'bot', password: 'alt'}}), null);
	// mit richtigem Passwort, aber der Admin gibt währenddessen neue Daten ein → Neustart, dann Code
	let r = await interactiveLogin({steam: steam2, api: api2, login: {user: 'bot', password: 'neu'}});
	assert.ok(r);
	assert.ok(api2.reports.filter((x) => x.state == 'working').length >= 2, 'zweimal begonnen');
	assert.strictEqual(api2.reports[api2.reports.length - 1].state, 'done');
});

test('validActions → Zustand und Hinweis', () => {
	assert.deepStrictEqual(prompt([{type: 2, detail: 'web.de'}]).codeType, 'email');
	assert.strictEqual(prompt([{type: 3}]).state, 'need-code');
	assert.strictEqual(prompt([{type: 4}]).state, 'need-confirm');
	assert.strictEqual(prompt([{type: 5}]).hint, 'Den Link in der E-Mail von Steam öffnen und die Anmeldung bestätigen.');
	assert.strictEqual(prompt([{type: 3}, {type: 4}]).codeType, 'app');
});
