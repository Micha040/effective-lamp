// Tests: der echte Steam-Weg (steam-real.js) ohne Netz – echtes globaloffensive mit echten Protobufs, davor ein
// nachgebauter steam-user, der wie Steam und der Game Coordinator antwortet (ClientHello → ClientWelcome,
// MatchListRequestFullGameInfo → MatchList). Prüft 64-Bit-IDs in der Anfrage, Zuordnung der Antwort, fehlende
// Demo-Adresse, leere Liste, keine Antwort (zwei Versuche), .info-Datei, Token-Erneuerung, Anmeldefehler.
'use strict';

const nodeTest = require('node:test');
const assert = require('node:assert');
const {EventEmitter} = require('events');
const steamReal = require('../src/steam-real');
const fake = require('../src/steam-fake');
const {decodeShareCode, abort} = require('../src/util');
const {botErrorText} = require('../src/texts');
const {shareCode, noModules} = require('./helpers');

// ohne node_modules: alle Tests hier mit Hinweis überspringen
const test = (name, fn) => nodeTest(name, {skip: noModules}, fn);
test.beforeEach = nodeTest.beforeEach;
const GlobalOffensive = noModules ? null : require('globaloffensive');
const Language = noModules ? null : require('globaloffensive/language.js');
const Protos = noModules ? null : require('globaloffensive/protobufs/generated/_load.js');

// globaloffensive schickt das erste Hallo nach 500 ms
Object.assign(steamReal.timing, {botLogonMs: 2000, gcConnectMs: 1500, gcRequestMs: 150, gcGapMs: 0});

// matches: matchId → MatchInfo-Objekt | 'empty' | 'silent'
function fakeSteamUser({matches = {}, welcome = true, logonError = null, playingBlocked = false, renew = null} = {}) {
	let sent = [];
	class FakeSteamUser extends EventEmitter {
		constructor(options) {
			super();
			this.options = options;
			this.packageName = 'steam-user';
			this.packageVersion = '5.3.0';
			this.steamID = null;
			FakeSteamUser.last = this;
		}

		logOn(details) {
			this.details = details;
			setImmediate(() => {
				if (logonError) {
					let e = new Error('AccessDenied');
					e.eresult = logonError;
					return this.emit('error', e);
				}
				this.steamID = {getSteamID64: () => '76561199000000077'};
				this.emit('loggedOn', {});
				this.emit('playingState', playingBlocked, playingBlocked ? 730 : 0);
				if (renew) {
					this.emit('refreshToken', renew);
				}
			});
		}

		gamesPlayed(apps) {
			this.playing = apps;
			if (apps.includes(730)) {
				this.emit('appLaunched', 730);
			}
		}

		async requestFreeLicense(apps) {
			return {grantedPackageIds: [], grantedAppIds: apps.includes(730) && !this.owns ? [] : []};
		}

		sendToGC(appid, type, header, payload) {
			sent.push({appid, type, payload});
			let reply = (t, proto, body) => setImmediate(() => this.emit('receivedFromGC', 730, t, Buffer.from(proto.encode(body).finish())));
			if (type == Language.ClientHello && welcome) {
				reply(Language.ClientWelcome, Protos.CMsgClientWelcome, {version: 2000244});
			}
			if (type == Language.MatchListRequestFullGameInfo) {
				let req = Protos.CMsgGCCStrike15_v2_MatchListRequestFullGameInfo.toObject(Protos.CMsgGCCStrike15_v2_MatchListRequestFullGameInfo.decode(payload), {longs: String});
				let m = matches[req.matchid];
				if (m === 'silent' || m === undefined) {
					return;
				}
				reply(Language.MatchList, Protos.CMsgGCCStrike15_v2_MatchList, {msgrequestid: 9147, matches: m === 'empty' ? [] : [m]});
			}
		}

		logOff() {
			this.steamID = null;
			setImmediate(() => this.emit('disconnected', 0, 'Logged off'));
		}
	}
	FakeSteamUser.sent = sent;
	return FakeSteamUser;
}

function matchInfo(code, {url = 'http://replay183.valve.net/730/003717052436717584506_1418282143.dem.bz2', matchtime = 1762300000} = {}) {
	let d = decodeShareCode(code);
	return {
		matchid: d.matchId,
		matchtime,
		watchablematchinfo: {server_ip: 2130706433, tv_port: 27015, tv_spectators: 0, cl_decryptdata_key_pub: '1234567890123456789'},
		roundstatsall: [
			{reservationid: '3717058124424299001', reservation: {account_ids: [11, 22, 33]}, map: '', round: 1, team_scores: [1, 0]},
			{reservationid: '3717058124424299188', reservation: {account_ids: [11, 22, 33]}, map: url || '', round: 24, team_scores: [13, 11], match_duration: 2400}
		]
	};
}

const token = () => fake.fakeToken('76561199000000077', 100);

test.beforeEach(() => abort.reset());

test('Bot anmelden, Game Coordinator begrüßt, Match-Infos per Freigabecode → Demo-Adresse, Spielzeit, .info', async () => {
	let code = shareCode(77);
	let id = decodeShareCode(code).matchId;
	let SteamUser = fakeSteamUser({matches: {[id]: matchInfo(code)}});
	let tokens = [];
	let bot = await steamReal.openBot({refreshToken: token(), onToken: (t) => tokens.push(t)}, {SteamUser, GlobalOffensive});
	let user = SteamUser.last;
	assert.deepStrictEqual(user.options, {dataDirectory: null, autoRelogin: false, renewRefreshTokens: true, enablePicsCache: false});
	assert.strictEqual(bot.steamId, '76561199000000077');
	assert.strictEqual((await bot.waitPlayingState()).blocked, false);
	assert.strictEqual(await bot.ensureLicense(), 'owned');
	assert.strictEqual(await bot.connectGC(), true);
	assert.deepStrictEqual(user.playing, [730]);

	let r = await bot.requestMatch(code);
	assert.strictEqual(r.status, 'ok');
	assert.strictEqual(r.url, 'http://replay183.valve.net/730/003717052436717584506_1418282143.dem.bz2');
	assert.strictEqual(r.matchtime, 1762300000);
	// Anfrage: genau die 64-Bit-IDs aus dem Freigabecode
	let req = SteamUser.sent.find((s) => s.type == Language.MatchListRequestFullGameInfo);
	let sentObj = Protos.CMsgGCCStrike15_v2_MatchListRequestFullGameInfo.toObject(Protos.CMsgGCCStrike15_v2_MatchListRequestFullGameInfo.decode(req.payload), {longs: String});
	let d = decodeShareCode(code);
	assert.deepStrictEqual([sentObj.matchid, sentObj.outcomeid, String(sentObj.token)], [d.matchId, d.outcomeId, d.token]);
	// .info wie bei CS2: dieselben Match-Infos, 64-Bit-Zahlen unversehrt
	let info = Protos.CDataGCCStrike15_v2_MatchInfo.toObject(Protos.CDataGCCStrike15_v2_MatchInfo.decode(r.info), {longs: String});
	assert.strictEqual(info.matchid, d.matchId);
	assert.strictEqual(info.matchtime, 1762300000);
	assert.strictEqual(info.roundstatsall[1].reservationid, '3717058124424299188');
	assert.strictEqual(info.watchablematchinfo.tv_port, 27015);
	assert.strictEqual(info.watchablematchinfo.cl_decryptdata_key_pub, '1234567890123456789');

	await bot.close();
	assert.deepStrictEqual(user.playing, []);
	assert.strictEqual(user.steamID, null);
	assert.deepStrictEqual(tokens, []);
});

test('keine Demo-Adresse, leere Liste, keine Antwort (zwei Versuche), ohne Begrüßung trotzdem anfragen', async () => {
	let [a, b, c] = [shareCode(81), shareCode(82), shareCode(83)];
	let SteamUser = fakeSteamUser({welcome: false, matches: {
		[decodeShareCode(a).matchId]: matchInfo(a, {url: null, matchtime: 1700000000}),
		[decodeShareCode(b).matchId]: 'empty',
		[decodeShareCode(c).matchId]: 'silent'
	}});
	let bot = await steamReal.openBot({refreshToken: token(), onToken: () => {}}, {SteamUser, GlobalOffensive});
	assert.strictEqual(await bot.connectGC(), false, 'keine Begrüßung');
	assert.deepStrictEqual(await bot.requestMatch(a), {status: 'nourl', matchtime: 1700000000});
	assert.deepStrictEqual(await bot.requestMatch(b), {status: 'empty'});
	let before = SteamUser.sent.length;
	assert.deepStrictEqual(await bot.requestMatch(c), {status: 'timeout'});
	assert.strictEqual(SteamUser.sent.filter((s, i) => i >= before && s.type == Language.MatchListRequestFullGameInfo).length, 2, 'zweimal gefragt');
	await bot.close();
});

test('Token-Erneuerung, Spiel auf anderem Gerät, abgelehntes und kaputtes Token', async () => {
	let renewed = fake.fakeToken('76561199000000077', 200);
	let tokens = [];
	let bot = await steamReal.openBot({refreshToken: token(), onToken: (t) => tokens.push(t)}, {SteamUser: fakeSteamUser({renew: renewed, playingBlocked: true}), GlobalOffensive});
	await bot.waitPlayingState();
	assert.strictEqual(bot.playingBlocked, true);
	assert.deepStrictEqual(tokens, [renewed]);
	await bot.close();

	await assert.rejects(steamReal.openBot({refreshToken: token(), onToken: () => {}}, {SteamUser: fakeSteamUser({logonError: 15}), GlobalOffensive}), (e) => {
		let m = botErrorText(e);
		return e.eresult == 15 && m.tokenGone && /neu anmelden/.test(m.text);
	});
	// kaputtes Token: steam-user würde es erst im nächsten Tick ablehnen (unbehandelt) – wir prüfen vorher
	for (let bad of ['kein-jwt', fake.fakeToken('7656', 1).replace(/^[^.]+\.[^.]+/, 'a.' + Buffer.from('{"iss":"web","aud":["web"]}').toString('base64url'))]) {
		await assert.rejects(steamReal.openBot({refreshToken: bad, onToken: () => {}}, {SteamUser: fakeSteamUser(), GlobalOffensive}), (e) => e.badToken === true && botErrorText(e).tokenGone);
	}
});

test('extractMatch: roundstats_legacy, fehlende Felder', () => {
	assert.deepStrictEqual(steamReal.extractMatch(Protos, {matchtime: 5, roundstatsall: [], roundstats_legacy: null}), {status: 'nourl', matchtime: 5});
	let r = steamReal.extractMatch(Protos, {matchid: '1', matchtime: 9, roundstatsall: [], roundstats_legacy: {map: 'http://replay1.valve.net/730/1_2.dem.bz2'}});
	assert.strictEqual(r.status, 'ok');
	assert.strictEqual(r.url, 'http://replay1.valve.net/730/1_2.dem.bz2');
	assert.ok(Buffer.isBuffer(r.info));
});
