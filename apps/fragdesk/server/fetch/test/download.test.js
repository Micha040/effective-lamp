// Tests: Demo laden – vollständig, 404, zu groß, abgebrochen, hängend, keine Demo, Weiterleitung, Fortschritt
'use strict';

const test = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const path = require('path');
const {download, DownloadError} = require('../src/download');
const {abort} = require('../src/util');
const {tmpDir, demoServer, captureLog} = require('./helpers');

const dir = tmpDir('download');
const bz2 = Buffer.concat([Buffer.from('BZh91AY&SY'), Buffer.alloc(300000, 7)]);

let srv;
test.before(async () => {
	srv = await demoServer({
		'/730/ok.dem.bz2': {data: bz2},
		'/730/nolen.dem.bz2': {data: bz2, noLength: true},
		'/730/gone.dem.bz2': {status: 404},
		'/730/err.dem.bz2': {status: 500},
		'/730/cut.dem.bz2': {data: bz2, truncate: 1000},
		'/730/stall.dem.bz2': {data: bz2, stall: true},
		'/730/html.dem.bz2': {data: Buffer.from('<html>kein Demo</html>')},
		'/730/moved.dem.bz2': {redirect: '/730/ok.dem.bz2'}
	});
});
test.after(() => srv.close());
test.beforeEach(() => abort.reset());

const target = (name) => path.join(dir, name);

test('vollständig: Datei unter dem richtigen Namen, keine .part übrig, Fortschritt gemeldet', async () => {
	let progress = [];
	let r = await download(srv.url('/730/ok.dem.bz2'), target('ok.dem.bz2'), {onProgress: (t) => progress.push(t)});
	assert.strictEqual(r.bytes, bz2.length);
	assert.ok(fs.readFileSync(target('ok.dem.bz2')).equals(bz2));
	assert.ok(!fs.existsSync(target('ok.dem.bz2') + '.part'));
	assert.ok(progress.length >= 1, 'Fortschritt');
	assert.match(progress[0], /KB von 293 KB \(\d+ %\)/);
	let r2 = await download(srv.url('/730/nolen.dem.bz2'), target('nolen.dem.bz2'));
	assert.strictEqual(r2.bytes, bz2.length);
	let r3 = await download(srv.url('/730/moved.dem.bz2'), target('moved.dem.bz2'));
	assert.strictEqual(r3.bytes, bz2.length);
});

test('Fehler: 404 = gone, 500 = http, zu groß, keine Demo – nie eine halbe Datei', async () => {
	let cases = [
		['/730/gone.dem.bz2', {}, 'gone'],
		['/730/err.dem.bz2', {}, 'http'],
		['/730/ok.dem.bz2', {maxBytes: 1000}, 'size'],
		['/730/nolen.dem.bz2', {maxBytes: 1000}, 'size'],
		['/730/html.dem.bz2', {}, 'format']
	];
	for (let [p, opts, kind] of cases) {
		let file = target('f-' + kind + path.basename(p));
		await assert.rejects(download(srv.url(p), file, opts), (e) => e instanceof DownloadError && e.kind == kind, p);
		assert.ok(!fs.existsSync(file) && !fs.existsSync(file + '.part'), `keine Reste: ${p}`);
	}
});

test('abgebrochene Verbindung: zweiter Versuch, dann Fehler „unvollständig“', async () => {
	let cap = captureLog();
	try {
		let before = srv.hits['/730/cut.dem.bz2'] || 0;
		let file = target('cut.dem.bz2');
		await assert.rejects(download(srv.url('/730/cut.dem.bz2'), file), (e) => e.kind == 'net');
		assert.strictEqual(srv.hits['/730/cut.dem.bz2'] - before, 2, 'zwei Versuche');
		assert.ok(!fs.existsSync(file + '.part'));
		assert.ok(cap.has(/zweiter Versuch/));
	} finally {
		cap.stop();
	}
});

test('hängender Server: Abbruch nach der Zeit ohne Daten', async () => {
	let file = target('stall.dem.bz2');
	let t = Date.now();
	await assert.rejects(download(srv.url('/730/stall.dem.bz2'), file, {idleMs: 150, attempts: 1}), (e) => e.kind == 'net' && /keine Daten/.test(e.message));
	assert.ok(Date.now() - t < 3000);
	assert.ok(!fs.existsSync(file + '.part'));
});

test('Abbruch (SIGTERM) beendet den Download sofort', async () => {
	let file = target('abort.dem.bz2');
	setTimeout(() => abort.cancel(), 50);
	await assert.rejects(download(srv.url('/730/stall.dem.bz2'), file, {idleMs: 10000}), (e) => e.cancelled === true);
	assert.ok(!fs.existsSync(file + '.part'));
	abort.reset();
});
