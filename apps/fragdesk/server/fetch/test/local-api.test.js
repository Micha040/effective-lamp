// Tests: Anfragen an fragdesk-server über 127.0.0.1 – Kopfzeilen, Wiederholen bei 503, keine Verbindung, alter Server,
// falscher Schlüssel, und ein Server, der wie der echte einen zu großen Körper sofort mit 413 ablehnt und schließt
'use strict';

const test = require('node:test');
const assert = require('node:assert');
const net = require('net');
const http = require('http');
const {LocalApi, LocalError} = require('../src/local-api');
const {abort} = require('../src/util');
const {listenFree, FakeServer, readBody} = require('./helpers');

test.beforeEach(() => abort.reset());

test('Kopfzeilen: X-FragDesk-Local immer, X-FragDesk: 1 bei schreibenden Anfragen, JSON-Körper', async () => {
	let seen = [];
	let server = http.createServer(async (req, res) => {
		let body = await readBody(req);
		seen.push({method: req.method, url: req.url, headers: req.headers, body: body.toString()});
		res.writeHead(200, {'Content-Type': 'application/json'});
		res.end(req.url.endsWith('/job') ? '{"bot":{},"login":null,"accounts":[]}' : '{"ok":true}');
	});
	let port = await listenFree(server);
	let api = new LocalApi({server: `http://127.0.0.1:${port}`, key: 'k3y-1234567890'});
	try {
		await api.job();
		await api.run('start', 'Läuft');
		await api.state('abcdef01', {knownCode: 'CSGO-x', lastCheck: 1});
		await api.putMatchFile('abcdef01', 'abc', 'heat', Buffer.from('FDHM1234'));
		let src = await api.putMatchSource('abcdef01', 'abc', {url: 'http://replay183.valve.net/730/1_2.dem.bz2', matchtime: 1762588800, shareCode: 'CSGO-x'});
		assert.deepStrictEqual(src, {status: 200, error: '', json: {ok: true}});
		assert.strictEqual(seen[4].method, 'PUT');
		assert.strictEqual(seen[4].url, '/api/v1/local/fetch/abcdef01/match/abc/source');
		assert.strictEqual(seen[4].headers['content-type'], 'application/json');
		assert.strictEqual(seen[4].headers['x-fragdesk'], '1');
		assert.deepStrictEqual(JSON.parse(seen[4].body), {url: 'http://replay183.valve.net/730/1_2.dem.bz2', matchtime: 1762588800, shareCode: 'CSGO-x'});
		assert.strictEqual(seen[0].headers['x-fragdesk-local'], 'k3y-1234567890');
		assert.strictEqual(seen[0].headers['x-fragdesk'], undefined);
		assert.strictEqual(seen[1].headers['x-fragdesk'], '1');
		assert.deepStrictEqual(JSON.parse(seen[1].body), {phase: 'start', text: 'Läuft'});
		assert.strictEqual(seen[2].url, '/api/v1/local/fetch/abcdef01/state');
		assert.strictEqual(seen[3].url, '/api/v1/local/fetch/abcdef01/match/abc/heat');
		assert.strictEqual(seen[3].headers['content-type'], 'application/octet-stream');
		assert.strictEqual(seen[3].headers['content-length'], '8');
		assert.strictEqual(seen[3].headers['transfer-encoding'], undefined, 'der Server kennt kein chunked');
		assert.strictEqual(seen[3].headers.connection, 'close');
	} finally {
		api.close();
		server.close();
	}
});

test('503 (Server startet neu): wiederholen; keine Verbindung: deutscher Fehler', async () => {
	let n = 0;
	let server = http.createServer((req, res) => {
		n++;
		res.writeHead(n < 3 ? 503 : 200, {'Content-Type': 'application/json'});
		res.end(n < 3 ? '{"error":"Der Server startet gerade neu"}' : '{"bot":{},"accounts":[]}');
	});
	let port = await listenFree(server);
	let api = new LocalApi({server: `http://127.0.0.1:${port}`, key: 'k', retries: 3, retryMs: 10});
	try {
		let job = await api.job();
		assert.deepStrictEqual(job.accounts, []);
		assert.strictEqual(n, 3);
	} finally {
		api.close();
		await new Promise((r) => server.close(r));
	}
	let gone = new LocalApi({server: `http://127.0.0.1:${port}`, key: 'k', retries: 1, retryMs: 10});
	await assert.rejects(gone.job(), (e) => e instanceof LocalError && e.fatal && /Der FragDesk-Server antwortet nicht .*Verbindung abgelehnt/.test(e.message));
	gone.close();
});

test('falscher Schlüssel und alter Server ohne /fetch: klare Texte', async () => {
	let fs = await new FakeServer({key: 'richtig'}).start();
	let api = new LocalApi({server: fs.url, key: 'falsch'});
	try {
		await assert.rejects(api.job(), /lehnt den Helfer ab: Falscher Schlüssel \(local\.key\)/);
	} finally {
		api.close();
		await fs.stop();
	}
	let old = http.createServer((req, res) => {
		res.writeHead(404, {'Content-Type': 'application/json'});
		res.end('{"error":"Unbekannte Anfrage"}');
	});
	let port = await listenFree(old);
	let api2 = new LocalApi({server: `http://127.0.0.1:${port}`, key: 'k'});
	try {
		await assert.rejects(api2.job(), /kennt das automatische Laden von Demos noch nicht/);
	} finally {
		api2.close();
		old.close();
	}
});

test('zu großer Körper: Server antwortet sofort 413 und schließt (wie fragdesk-server) → Status 413, kein Netzfehler', async () => {
	// wie src/api/http.cpp: Kopf lesen, Content-Length über der Grenze → 413 ohne JSON, Verbindung zu
	let server = net.createServer((sock) => {
		let buf = '';
		sock.on('data', (d) => {
			buf += d.toString('latin1');
			let end = buf.indexOf('\r\n\r\n');
			if (end < 0) {
				return;
			}
			let len = Number((buf.slice(0, end).match(/content-length: *(\d+)/i) || [])[1] || 0);
			if (len > 65536) {
				sock.end('HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n');
				sock.destroy();
			}
		});
		sock.on('error', () => {});
	});
	let port = await listenFree(server);
	let api = new LocalApi({server: `http://127.0.0.1:${port}`, key: 'k', retries: 0});
	try {
		let r = await api.putMatchFile('abcdef01', 'abc', 'replay', Buffer.alloc(8 << 20, 1));
		assert.strictEqual(r.status, 413);
		assert.strictEqual(r.error, 'zu groß für den Server (Status 413)');
	} finally {
		api.close();
		server.close();
	}
});
