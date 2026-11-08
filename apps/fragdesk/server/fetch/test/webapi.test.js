// Tests: GetNextMatchSharingCode – Antworten deuten (200/202/403/412/429/5xx, falscher Schlüssel) und die Kette ablaufen
'use strict';

const test = require('node:test');
const assert = require('node:assert');
const webapi = require('../src/webapi');
const {HttpError} = require('../src/util');
const {shareCode} = require('./helpers');

const body = (code) => Buffer.from(JSON.stringify({result: {nextcode: code}}));

test('Antworten der Web-API auf deutsche Texte abbilden', () => {
	let c = shareCode(1);
	assert.deepStrictEqual(webapi.interpret(200, body(c)), {kind: 'next', code: c});
	assert.deepStrictEqual(webapi.interpret(200, body('n/a')), {kind: 'none'});
	assert.deepStrictEqual(webapi.interpret(202, body('n/a')), {kind: 'none'});
	assert.deepStrictEqual(webapi.interpret(202, ''), {kind: 'none'});

	let auth = webapi.interpret(403, '');
	assert.strictEqual(auth.scope, 'account');
	assert.match(auth.text, /Authentifizierungscode und SteamID passen nicht/);

	let key = webapi.interpret(403, '<html><body>Access is denied. Retrying will not help. Please verify your <pre>key=</pre> parameter.</body></html>');
	assert.strictEqual(key.scope, 'key');
	assert.match(key.text, /Web-API-Schlüssel/);
	assert.strictEqual(webapi.interpret(401, '').scope, 'key');

	let known = webapi.interpret(412, '');
	assert.strictEqual(known.scope, 'account');
	assert.match(known.text, /Freigabecode/);

	assert.strictEqual(webapi.interpret(429, '').scope, 'global');
	assert.match(webapi.interpret(429, '').text, /begrenzt/);
	assert.strictEqual(webapi.interpret(503, '').scope, 'global');
	assert.strictEqual(webapi.interpret(500, '').scope, 'retry');
	assert.strictEqual(webapi.interpret(504, '').scope, 'retry');
	assert.strictEqual(webapi.interpret(200, '{"result":{}}').kind, 'error');
	assert.strictEqual(webapi.interpret(200, 'kein json').kind, 'error');
	assert.strictEqual(webapi.interpret(418, '').scope, 'retry');
});

function chainTransport(chain, extra = {}) {
	let calls = [];
	let fn = async ({knownCode, apiKey, steamId, authCode}) => {
		calls.push({knownCode, apiKey, steamId, authCode});
		if (extra[knownCode]) {
			let e = extra[knownCode];
			if (e.network) {
				throw new HttpError('weg', e.network);
			}
			return {status: e.status, body: Buffer.alloc(0)};
		}
		let i = chain.indexOf(knownCode);
		if (i < 0) {
			return {status: 412, body: Buffer.alloc(0)};
		}
		if (i == chain.length - 1) {
			return {status: 202, body: body('n/a')};
		}
		return {status: 200, body: body(chain[i + 1])};
	};
	fn.calls = calls;
	return fn;
}

const params = (knownCode) => ({apiKey: 'K', steamId: '76561190000000001', authCode: 'AAAA-BBBBB-CCCC', knownCode});

test('Kette: alle neuen Codes bis "n/a", mit Parametern', async () => {
	let chain = [1, 2, 3, 4].map(shareCode);
	let t = chainTransport(chain);
	let r = await webapi.walk(t, params(chain[0]), {gapMs: 0});
	assert.deepStrictEqual(r.codes, chain.slice(1));
	assert.strictEqual(r.error, null);
	assert.strictEqual(t.calls.length, 4);   // 3 neue + die Antwort „nichts Neues“
	assert.deepStrictEqual(t.calls[0], {knownCode: chain[0], apiKey: 'K', steamId: '76561190000000001', authCode: 'AAAA-BBBBB-CCCC'});
	let none = await webapi.walk(t, params(chain[3]), {gapMs: 0});
	assert.deepStrictEqual(none.codes, []);
	assert.strictEqual(none.error, null);
});

test('Kette: höchstens 10 neue Codes je Lauf, Abstand zwischen den Anfragen', async () => {
	let chain = Array.from({length: 15}, (_, i) => shareCode(i + 10));
	let t = chainTransport(chain);
	let started = Date.now();
	let r = await webapi.walk(t, params(chain[0]), {gapMs: 20});
	assert.strictEqual(r.codes.length, 10);
	assert.deepStrictEqual(r.codes, chain.slice(1, 11));
	assert.strictEqual(r.more, true);
	assert.strictEqual(t.calls.length, 10);
	assert.ok(Date.now() - started >= 9 * 20 - 5, 'Abstand eingehalten');
});

test('Kette: Fehler unterwegs behält die bis dahin gefundenen Codes', async () => {
	let chain = [1, 2, 3, 4].map(shareCode);
	let r = await webapi.walk(chainTransport(chain, {[chain[2]]: {status: 500}}), params(chain[0]), {gapMs: 0});
	assert.deepStrictEqual(r.codes, chain.slice(1, 3));
	assert.strictEqual(r.error.status, 500);
	assert.strictEqual(r.error.scope, 'retry');

	let n = await webapi.walk(chainTransport(chain, {[chain[0]]: {network: 'ETIMEDOUT'}}), params(chain[0]), {gapMs: 0});
	assert.deepStrictEqual(n.codes, []);
	assert.strictEqual(n.error.scope, 'global');
	assert.match(n.error.text, /Zeitüberschreitung/);

	let bad = await webapi.walk(chainTransport(chain), params(shareCode(99)), {gapMs: 0});
	assert.strictEqual(bad.error.status, 412);
});

test('Kette: Steam im Kreis → aufhören statt endlos fragen', async () => {
	let a = shareCode(1);
	let b = shareCode(2);
	let t = async ({knownCode}) => ({status: 200, body: body(knownCode == a ? b : a)});
	let r = await webapi.walk(t, params(a), {gapMs: 0});
	assert.deepStrictEqual(r.codes, [b]);
});
