// Tests: fdanalyze aufrufen (mit nice -n 19 und GOMEMLIMIT), Ergebnisse finden, Fehler und Zeitlimit
'use strict';

const test = require('node:test');
const assert = require('node:assert');
const fs = require('fs');
const path = require('path');
const {analyze, analyzerVersion, AnalyzeError} = require('../src/analyze');
const {tmpDir, writeFakeAnalyzer} = require('./helpers');

const dir = tmpDir('analyze');
const analyzer = writeFakeAnalyzer(dir);
const demo = path.join(dir, 'match730_1_2_3.dem.bz2');
fs.writeFileSync(demo, Buffer.concat([Buffer.from('BZh9'), Buffer.alloc(1000, 3)]));
const logFile = path.join(dir, 'calls.jsonl');

function run(mode, opts = {}) {
	process.env.FAKE_ANALYZER = mode;
	process.env.FAKE_ANALYZER_LOG = logFile;
	return analyze(Object.assign({analyzer, demo, outBase: path.join(dir, 'out', mode), temp: path.join(dir, 'unpack-' + mode)}, opts)).finally(() => {
		delete process.env.FAKE_ANALYZER;
	});
}

test('Erfolg: json/heat/replay, Prüfsumme, Aufruf mit -demo -out -replay -temp, nice 19, GOMEMLIMIT', async () => {
	let r = await run('ok');
	assert.match(r.checksum, /^[0-9a-f]{16}$/);
	assert.ok(r.json.endsWith('ok.json') && fs.existsSync(r.json));
	assert.ok(r.heat.endsWith('ok.heat'));
	assert.ok(r.replay.endsWith('ok.replay'));
	assert.strictEqual(r.match.map, 'de_test');
	let call = JSON.parse(fs.readFileSync(logFile, 'utf8').trim().split('\n').pop());
	assert.deepStrictEqual(call.args, ['-demo', demo, '-out', path.join(dir, 'out', 'ok.json'), '-replay', path.join(dir, 'out', 'ok.replay'), '-temp', path.join(dir, 'unpack-ok')]);
	assert.strictEqual(call.nice, 19, 'mit nice -n 19');
});

test('ohne Heatmap und 2D-Daten: kein Fehler', async () => {
	let r = await run('noheat');
	assert.strictEqual(r.heat, null);
	assert.strictEqual(r.replay, null);
});

test('Fehler: ERROR-Zeile, Absturz, kaputtes JSON, fehlendes Programm, Zeitlimit', async () => {
	await assert.rejects(run('error'), (e) => e instanceof AnalyzeError && e.code == 'CORRUPT' && /Die Demo ist beschädigt: kaputte Testdemo/.test(e.message));
	await assert.rejects(run('crash'), (e) => e.code == 'SIGNAL' && /SIGKILL/.test(e.message));
	await assert.rejects(run('badjson'), (e) => e.code == 'OUTPUT');
	await assert.rejects(run('ok', {analyzer: path.join(dir, 'gibtsnicht')}), (e) => e.code == 'START' && /fdanalyze fehlt/.test(e.message));
	await assert.rejects(run('slow', {timeoutMs: 300}), (e) => e.code == 'TIMEOUT');
});

test('Version für „check“', async () => {
	let v = await analyzerVersion(analyzer);
	assert.deepStrictEqual(v, {ok: true, text: 'fdanalyze 0.0.0-test'});
	assert.strictEqual((await analyzerVersion(path.join(dir, 'fehlt'))).ok, false);
});
