// Tests: Freigabecodes (gegen globaloffensive-sharecode), JWT, Texte, Protokoll ohne Geheimnisse
'use strict';

const test = require('node:test');
const assert = require('node:assert');
const util = require('../src/util');
const log = require('../src/log');
const {captureLog, noModules} = require('./helpers');

test('Freigabecodes: eigener Decoder stimmt mit globaloffensive-sharecode überein', {skip: noModules}, () => {
	const {ShareCode} = require('globaloffensive-sharecode');
	let rnd = (bits) => {
		let v = 0n;
		for (let i = 0; i < bits; i += 16) {
			v = (v << 16n) | BigInt(Math.floor(Math.random() * 65536));
		}
		return v & ((1n << BigInt(bits)) - 1n);
	};
	for (let i = 0; i < 200; i++) {
		let matchId = i == 0 ? (1n << 64n) - 1n : rnd(64);
		let outcomeId = rnd(64);
		let token = Number(rnd(16));
		let code = util.encodeShareCode(matchId, outcomeId, token);
		assert.match(code, util.SHARE_RE);
		let mine = util.decodeShareCode(code);
		let lib = new ShareCode(code).decode();
		assert.deepStrictEqual(mine, {matchId: matchId.toString(), outcomeId: outcomeId.toString(), token: String(token)});
		assert.strictEqual(lib.matchId, mine.matchId, code);
		assert.strictEqual(lib.outcomeId, mine.outcomeId, code);
		assert.strictEqual(String(lib.token), mine.token, code);
	}
	assert.strictEqual(util.decodeShareCode('CSGO-abc'), null);
	assert.strictEqual(util.decodeShareCode('CSGO-00000-00000-00000-00000-00000'), null);   // 0 gibt es im Alphabet nicht
	assert.strictEqual(util.maskCode('CSGO-aaaaa-bbbbb-ccccc-ddddd-eeeee'), 'CSGO-…-eeeee');
});

test('JWT lesen und Zeiten/Größen auf Deutsch', () => {
	let payload = Buffer.from(JSON.stringify({iss: 'steam', sub: '76561199000000001', aud: ['client'], exp: 1800000000})).toString('base64url');
	let jwt = util.decodeJwt(`eyJhbGciOiJFZERTQSJ9.${payload}.sig`);
	assert.strictEqual(jwt.sub, '76561199000000001');
	assert.strictEqual(util.decodeJwt('kaputt'), null);
	assert.strictEqual(util.decodeJwt('a.b.c'), null);
	assert.strictEqual(util.fmtBytes(84 * 1048576), '84,0 MB');
	assert.strictEqual(util.fmtBytes(1536), '2 KB');
	assert.strictEqual(util.fmtBytes(3 * (1 << 30)), '3,0 GB');
	assert.strictEqual(util.fmtDuration(2500), '3 s');
	assert.strictEqual(util.fmtDuration(125000), '2 min 5 s');
	assert.match(util.fmtDate(1762000000, true), /^\d\d\.\d\d\.2025 \d\d:\d\d$/);
});

test('Protokoll: Geheimnisse, Tokens und key= tauchen nie auf', () => {
	let cap = captureLog();
	try {
		log.addSecret('MeinPasswort!');
		log.addSecret('ABCD-EFGHJ-KLMN');
		log.addSecret('0123456789ABCDEF0123456789ABCDEF');
		log.info('Login mit MeinPasswort! und Code ABCD-EFGHJ-KLMN');
		log.warn(new Error('GET https://api.steampowered.com/x?key=0123456789ABCDEF0123456789ABCDEF&steamid=1&steamidkey=GEHEIM-1'));
		log.error('Token eyJhbGciOiJFZERTQSJ9.eyJpc3MiOiJzdGVhbSIsInN1YiI6IjEifQ.c2lnbmF0dXJlMTIzNDU2 ging nicht');
		let all = cap.text();
		assert.doesNotMatch(all, /MeinPasswort|ABCD-EFGHJ-KLMN|0123456789ABCDEF|GEHEIM-1|eyJpc3M/);
		assert.match(all, /•••/);
		assert.strictEqual(cap.lines.length, 3);
	} finally {
		cap.stop();
	}
});

test('sleep und withTimeout lassen sich abbrechen', async () => {
	util.abort.reset();
	await assert.rejects(util.withTimeout(new Promise(() => {}), 30, 'zu langsam'), (e) => e.timeout === true);
	let p = util.sleep(10000);
	setTimeout(() => util.abort.cancel(), 20);
	await assert.rejects(p, (e) => e.cancelled === true);
	util.abort.reset();
});
