// Tests der gebündelten Datei (wie im Release): bauen, allein aus einem leeren Ordner mit node --jitless starten,
// ganze Kette in der Attrappe (Unterprozess, Protokoll wie im Journal), SIGTERM mitten in der Auswertung – und mit
// FRAGDESK_TEST_DEMO=<Valve-Demo .dem> und FRAGDESK_TEST_ANALYZER=<fdanalyze für Linux> der echte Weg: Demo als .dem.bz2
// über HTTP, echtes fdanalyze, Prüfsumme wie am PC, Heatmap und 2D-Daten beim Server.
'use strict';

const test = require('node:test');
const assert = require('node:assert');
const {spawn, spawnSync} = require('child_process');
const fs = require('fs');
const path = require('path');
const fake = require('../src/steam-fake');
const {now} = require('../src/util');
const {tmpDir, FakeServer, writeFakeAnalyzer, demoServer, shareCode} = require('./helpers');

const here = path.join(__dirname, '..');
const root = tmpDir('bundle');
const hasEsbuild = fs.existsSync(path.join(here, 'node_modules', 'esbuild'));
const skip = hasEsbuild ? false : 'node_modules fehlt (erst npm ci im Ordner server/fetch)';
let bundle = null;

function build() {
	if (bundle) {
		return bundle;
	}
	let out = path.join(root, 'out');
	let r = spawnSync(process.execPath, [path.join(here, 'build.js'), out], {encoding: 'utf8'});
	assert.strictEqual(r.status, 0, r.stderr + r.stdout);
	bundle = path.join(out, 'fragdesk-fetch.js');
	return bundle;
}

// Die Datei allein in einen leeren Ordner kopieren und dort starten
function isolated() {
	let dir = fs.mkdtempSync(path.join(root, 'leer-'));
	let file = path.join(dir, 'fragdesk-fetch.js');
	fs.copyFileSync(build(), file);
	return {dir, file};
}

function runNode(args, {cwd, env, onSpawn} = {}) {
	return new Promise((resolve) => {
		let child = spawn(process.execPath, args, {cwd, env: Object.assign({}, process.env, {NODE_PATH: ''}, env || {}), stdio: ['ignore', 'pipe', 'pipe']});
		let out = '';
		child.stdout.on('data', (d) => (out += d));
		child.stderr.on('data', (d) => (out += d));
		if (onSpawn) {
			onSpawn(child);
		}
		child.on('close', (code, signal) => resolve({code, signal, out}));
	});
}

test('Bündel: eine Datei, ohne „use strict“ oben, Lizenztexte daneben', {skip}, () => {
	let file = build();
	let head = fs.readFileSync(file, 'utf8').slice(0, 400);
	assert.match(head, /^#!\/usr\/bin\/env node\n\/\/ fragdesk-fetch /);
	assert.doesNotMatch(head, /"use strict"/, 'sonst bricht globaloffensive-sharecode');
	let size = fs.statSync(file).size;
	assert.ok(size > 1e6 && size < 16e6, `Größe ${size}`);
	let notices = fs.readFileSync(path.join(path.dirname(file), 'fragdesk-fetch-NOTICES.txt'), 'utf8');
	for (let p of ['steam-user 5.3.0', 'steam-session 1.9.4', 'globaloffensive 3.3.0', 'protobufjs 7.6.6', 'long 5.3.2', 'Permission is hereby granted']) {
		assert.ok(notices.includes(p), p);
	}
	assert.ok(!notices.includes('adm-zip') && !notices.includes('zstddec'), 'CDN-Teile sind nicht gebündelt');
});

test('Bündel: allein aus leerem Ordner mit --jitless – version, check (Steam-Bibliotheken, 64-Bit-IDs)', {skip}, async () => {
	let {dir, file} = isolated();
	let v = await runNode(['--jitless', file, 'version'], {cwd: dir});
	assert.strictEqual(v.code, 0, v.out);
	assert.match(v.out, /fragdesk-fetch \d+\.\d+\.\d+ \(Node /);
	let data = path.join(dir, 'data');
	fs.mkdirSync(path.join(data, 'fetch'), {recursive: true});
	let analyzer = writeFakeAnalyzer(dir);
	let c = await runNode(['--jitless', file, 'check', '--offline', '--data', data, '--analyzer', analyzer, '--server', 'http://127.0.0.1:7749'], {cwd: dir});
	assert.match(c.out, /ok {3}Steam-Bibliotheken/, c.out);
	assert.match(c.out, /ok {3}fdanalyze: .* fdanalyze 0\.0\.0-test/);
	assert.doesNotMatch(c.out, /DeprecationWarning|Cannot find module/);
});

async function fakeChain(dir, analyzer, {analyzerMode} = {}) {
	let [c0, c1, c2] = [shareCode(501), shareCode(502), shareCode(503)];
	let demo1 = Buffer.concat([Buffer.from('BZh91AY&SY'), Buffer.alloc(200000, 1)]);
	let demo2 = Buffer.concat([Buffer.from('BZh91AY&SY'), Buffer.alloc(150000, 2)]);
	let demos = await demoServer({'/730/000000000000000000501_1.dem.bz2': {data: demo1}, '/730/000000000000000000502_2.dem.bz2': {data: demo2}});
	let data = path.join(dir, 'data');
	fs.mkdirSync(path.join(data, 'fetch'), {recursive: true, mode: 0o700});
	let server = await new FakeServer({job: {
		bot: {refreshToken: fake.fakeToken('76561199000000009', 30), apiKey: 'K'.repeat(32)},
		accounts: [{id: 'cafe0001', steamId: '76561190000000001', authCode: 'AAAA-BBBBB-CCCC', knownCode: c0}]
	}}).start();
	fs.writeFileSync(path.join(data, 'local.key'), server.key);
	let fixture = path.join(dir, 'fake.json');
	fs.writeFileSync(fixture, JSON.stringify({
		apiKey: 'K'.repeat(32),
		webapi: {'76561190000000001': {chain: [c0, c1, c2], authCode: 'AAAA-BBBBB-CCCC'}},
		bot: {renewToken: true},
		gc: {[c1]: {url: demos.url('/730/000000000000000000501_1.dem.bz2'), matchtime: now() - 86400}, [c2]: {url: demos.url('/730/000000000000000000502_2.dem.bz2'), matchtime: now() - 7200}}
	}));
	let env = {FRAGDESK_FETCH_FAKE: fixture, JOURNAL_STREAM: '8:12345'};
	if (analyzerMode) {
		env.FAKE_ANALYZER = analyzerMode;
	}
	return {server, demos, data, env, codes: [c0, c1, c2]};
}

test('Bündel: ganze Kette in der Attrappe als eigener Prozess (Protokoll mit Journal-Präfixen)', {skip}, async () => {
	let {dir, file} = isolated();
	let analyzer = writeFakeAnalyzer(dir);
	let fx = await fakeChain(dir, analyzer);
	try {
		let r = await runNode(['--jitless', file, 'run', '--server', fx.server.url, '--data', fx.data, '--analyzer', analyzer], {cwd: dir, env: fx.env});
		assert.strictEqual(r.code, 0, r.out);
		let lines = r.out.trim().split('\n');
		assert.ok(lines.every((l) => /^<[3467]>/.test(l) || /expose_wasm/.test(l)), r.out);
		assert.ok(lines.some((l) => l.startsWith('<6>Lauf: 1 Konto geprüft · 2 neue Matches')), r.out);
		assert.strictEqual(Object.keys(fx.server.files.cafe0001).length, 2);
		assert.strictEqual(fx.server.job.accounts[0].knownCode, fx.codes[2]);
		assert.strictEqual(fx.server.tokens.length, 1);
		assert.doesNotMatch(r.out, /AAAA-BBBBB-CCCC|KKKKKKKK|eyJ/);
	} finally {
		await fx.server.stop();
		await fx.demos.close();
	}
});

test('Bündel: SIGTERM mitten in der Auswertung – fdanalyze weg, tmp leer, Sperre frei, Code 143', {skip}, async () => {
	let {dir, file} = isolated();
	let analyzer = writeFakeAnalyzer(dir);
	let fx = await fakeChain(dir, analyzer, {analyzerMode: 'slow'});
	let pidFile = path.join(dir, 'analyzer.jsonl');
	fx.env.FAKE_ANALYZER_LOG = pidFile;
	try {
		let r = await runNode(['--jitless', file, 'run', '--server', fx.server.url, '--data', fx.data, '--analyzer', analyzer], {
			cwd: dir,
			env: fx.env,
			onSpawn: (child) => {
				let t = setInterval(() => {
					if (fs.existsSync(pidFile)) {   // fdanalyze läuft
						clearInterval(t);
						setTimeout(() => child.kill('SIGTERM'), 100);
					}
				}, 20);
			}
		});
		assert.strictEqual(r.code, 143, r.out);
		assert.match(r.out, /SIGTERM erhalten/);
		assert.ok(!fs.existsSync(path.join(fx.data, 'fetch', 'lock')), 'Sperre frei');
		assert.deepStrictEqual(fs.readdirSync(path.join(fx.data, 'fetch', 'tmp')), []);
		assert.strictEqual(fx.server.job.accounts[0].knownCode, fx.codes[0], 'nichts weitergezählt');
		let ps = spawnSync('pgrep', ['-f', 'fdanalyze-test'], {encoding: 'utf8'});
		assert.ok(ps.error || !ps.stdout.trim(), 'kein fdanalyze mehr: ' + ps.stdout);
	} finally {
		await fx.server.stop();
		await fx.demos.close();
	}
});

// ---- echter Weg mit echter Demo und echtem fdanalyze -----------------------------------------------------------------
const realDemo = process.env.FRAGDESK_TEST_DEMO;
const realAnalyzer = process.env.FRAGDESK_TEST_ANALYZER;
const haveBzip2 = !spawnSync('bzip2', ['--help']).error;
const skipReal = skip || (!realDemo || !realAnalyzer ? 'FRAGDESK_TEST_DEMO und FRAGDESK_TEST_ANALYZER nicht gesetzt' : !haveBzip2 ? 'bzip2 fehlt' : false);

test('echt: Valve-Demo als .dem.bz2 über HTTP → echtes fdanalyze → json/heat/replay beim Server, Prüfsumme wie am PC', {skip: skipReal, timeout: 600000}, async () => {
	let {dir, file} = isolated();
	let bz2 = path.join(dir, 'demo.dem.bz2');
	let z = spawnSync('sh', ['-c', 'bzip2 -c "$1" > "$2"', 'sh', realDemo, bz2]);   // das Original bleibt unberührt
	assert.strictEqual(z.status, 0, String(z.stderr));
	// Vergleich: dieselbe Demo direkt (so wie FragDesk am PC sie auswertet)
	let ref = path.join(dir, 'ref', 'ref.json');
	let d = spawnSync(realAnalyzer, ['-demo', realDemo, '-out', ref, '-temp', path.join(dir, 'ref-tmp')], {encoding: 'utf8'});
	assert.strictEqual(d.status, 0, d.stdout + d.stderr);
	let refJson = JSON.parse(fs.readFileSync(ref, 'utf8'));

	let [c0, c1] = [shareCode(901), shareCode(902)];
	let matchtime = now() - 3 * 86400;
	let demos = await demoServer({'/730/003717052436717584506_1418282143.dem.bz2': {file: bz2}});
	let data = path.join(dir, 'data');
	fs.mkdirSync(path.join(data, 'fetch'), {recursive: true, mode: 0o700});
	let server = await new FakeServer({job: {
		bot: {refreshToken: fake.fakeToken('76561199000000009', 30), apiKey: 'K'.repeat(32)},
		accounts: [{id: 'beef0001', steamId: '76561190000000001', authCode: 'AAAA-BBBBB-CCCC', knownCode: c0}]
	}}).start();
	fs.writeFileSync(path.join(data, 'local.key'), server.key);
	let fixture = path.join(dir, 'fake.json');
	fs.writeFileSync(fixture, JSON.stringify({
		webapi: {'76561190000000001': {chain: [c0, c1]}},
		gc: {[c1]: {url: demos.url('/730/003717052436717584506_1418282143.dem.bz2'), matchtime, info: true}}
	}));
	try {
		let started = Date.now();
		let r = await runNode(['--jitless', file, 'run', '--server', server.url, '--data', data, '--analyzer', realAnalyzer], {cwd: dir, env: {FRAGDESK_FETCH_FAKE: fixture}});
		if (process.env.FRAGDESK_TEST_VERBOSE) {
			console.log(r.out.replace(/^/gm, '# '));
		}
		assert.strictEqual(r.code, 0, r.out);
		let files = server.files.beef0001;
		let sums = Object.keys(files);
		assert.deepStrictEqual(sums, [refJson.checksum], 'Prüfsumme wie bei der Auswertung am PC');
		let f = files[refJson.checksum];
		let json = JSON.parse(f.json.data.toString('utf8'));
		assert.strictEqual(json.checksum, refJson.checksum);
		assert.strictEqual(json.match.map, refJson.match.map);
		assert.strictEqual(json.match.scoreA, refJson.match.scoreA);
		assert.strictEqual(json.match.date, new Date(matchtime * 1000).toISOString().replace('.000', ''), 'Spielzeit vom Game Coordinator');
		// .info wie bei CS2: fdanalyze (cs-demo-analyzer) rechnet daraus denselben Freigabecode zurück
		assert.strictEqual(json.match.shareCode, c1, 'Freigabecode aus der .info-Datei');
		assert.strictEqual(json.demo.file, 'match730_003717052436717584506_1418282143.dem.bz2');
		assert.strictEqual(json.demo.packed, 'bz2');
		assert.strictEqual(f.heat.data.subarray(0, 4).toString(), 'FDHM');
		assert.strictEqual(f.replay.data.subarray(0, 4).toString(), 'FDRP');
		assert.strictEqual(server.job.accounts[0].knownCode, c1);
		assert.match(r.out, /ausgewertet in/);
		assert.deepStrictEqual(fs.readdirSync(path.join(data, 'fetch', 'tmp')), []);
		console.log(`# echter Weg: ${(fs.statSync(bz2).size / 1048576).toFixed(0)} MB .bz2, json ${f.json.size} B, heat ${f.heat.size} B, replay ${f.replay.size} B, ${Date.now() - started} ms`);
	} finally {
		await server.stop();
		await demos.close();
	}
});
