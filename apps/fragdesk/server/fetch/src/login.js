// fragdesk-fetch – den Steam-Bot mit Benutzername und Passwort anmelden (Admin in der Handy-App, einmal für den Server).
// Ablauf: steam-session (SteamClient) startWithCredentials → je nach validActions „need-code“ (E-Mail-Code oder Code
// aus der Steam-App) oder „need-confirm“ (in der Steam-App bestätigen). Während gewartet wird, holt der Helfer alle
// pollMs (3 s) den Auftrag: dort steht der Code, sobald der Admin ihn eingibt – höchstens loginMaxMs (10 min) lang.
// Jeder Zwischenstand geht an POST /api/v1/local/fetch/login, am Ende mit refreshToken, Name, SteamID und Ablauf.
'use strict';

const log = require('./log');
const {LOGIN, loginErrorText, eresultOf, EResult, rawReason} = require('./texts');
const {sleep, withTimeout, decodeJwt, fmtDate} = require('./util');

const GUARD = {EmailCode: 2, DeviceCode: 3, DeviceConfirmation: 4, EmailConfirmation: 5};
const MAX_WRONG_CODES = 5;
const MAX_RESTARTS = 3;   // neue Zugangsdaten während des Wartens

function normCode(code) {
	return typeof code == 'string' ? code.replace(/\s+/g, '').toUpperCase() : '';
}

// validActions → Zustand für die Handy-App
function prompt(actions) {
	let has = (t) => actions.some((a) => a && a.type == t);
	let email = actions.find((a) => a && a.type == GUARD.EmailCode);
	if (email) {
		let where = email.detail ? ` (an …@${email.detail})` : '';
		return {state: 'need-code', codeType: 'email', hint: `Steam hat einen Code per E-Mail geschickt${where} – bitte eingeben.`};
	}
	if (has(GUARD.DeviceConfirmation) && has(GUARD.DeviceCode)) {
		return {state: 'need-confirm', codeType: 'app', hint: 'In der Steam-App auf dem Handy die Anmeldung bestätigen – oder den Code aus der Steam-App eingeben.'};
	}
	if (has(GUARD.DeviceCode)) {
		return {state: 'need-code', codeType: 'app', hint: 'Code aus der Steam-App (Steam Guard) eingeben.'};
	}
	if (has(GUARD.DeviceConfirmation)) {
		return {state: 'need-confirm', hint: 'In der Steam-App auf dem Handy die Anmeldung bestätigen.'};
	}
	if (has(GUARD.EmailConfirmation)) {
		return {state: 'need-confirm', hint: 'Den Link in der E-Mail von Steam öffnen und die Anmeldung bestätigen.'};
	}
	return {state: 'need-confirm', hint: 'Steam verlangt eine Bestätigung – in der Steam-App oder per E-Mail.'};
}

async function report(api, body) {
	try {
		await api.login(body);
	} catch (err) {
		if (err.cancelled) {
			throw err;
		}
		log.warn(`Anmeldestand nicht beim Server angekommen: ${err.message}`);
		if (body.refreshToken) {
			throw err;   // ohne gespeichertes Token wäre die Anmeldung umsonst – lieber laut scheitern
		}
	}
}

// → {refreshToken, botName, botSteamId, tokenUntil} oder null (Fehler wurde gemeldet)
async function interactiveLogin({steam, api, login}) {
	let creds = {user: String(login.user || '').trim(), password: String(login.password || ''), code: normCode(login.code)};
	for (let restart = 0; restart <= MAX_RESTARTS; restart++) {
		log.addSecret(creds.password);
		log.addSecret(creds.code);
		let r = await attempt({steam, api, creds});
		if (!r || !r.restart) {
			return r;
		}
		creds = r.restart;
		log.info('Steam-Anmeldung: neue Zugangsdaten vom Admin – fange von vorn an.');
	}
	await report(api, {state: 'error', hint: LOGIN.timeout});
	return null;
}

async function attempt({steam, api, creds}) {
	let t = steam.timing;
	if (!creds.user || !creds.password) {
		await report(api, {state: 'error', hint: LOGIN.wrongPassword});
		return null;
	}
	log.info(`Steam-Anmeldung des Bots „${creds.user}“ beginnt.`);
	await report(api, {state: 'working', hint: LOGIN.working});
	let deadline = Date.now() + t.loginMaxMs;
	let flow = steam.createLogin();
	let authenticated = false;
	let failure = null;
	let wake = null;
	let signal = () => wake && wake();
	flow.on('authenticated', () => {
		authenticated = true;
		signal();
	});
	flow.on('timeout', () => {
		failure = failure || LOGIN.timeout;
		signal();
	});
	flow.on('error', (err) => {
		failure = failure || loginErrorText(err);
		log.warn(`Steam-Anmeldung: Verbindung zu Steam gestört (${rawReason(err)})`);
		signal();
	});

	let started;
	try {
		started = await withTimeout(flow.start(creds), t.loginStartMs, 'Steam antwortet nicht', () => flow.cancel());
	} catch (err) {
		if (err.cancelled) {
			flow.cancel();
			throw err;
		}
		flow.cancel();
		let text = err.timeout ? LOGIN.unreachable : loginErrorText(err);
		log.warn(`Steam-Anmeldung: ${text} (${rawReason(err)})`);
		await report(api, {state: 'error', hint: text});
		return null;
	}

	let lastCode = creds.code;
	let wrong = 0;
	let current = null;
	if (started && started.actionRequired) {
		current = prompt(started.validActions || []);
		log.info(`Steam-Anmeldung: wartet auf ${current.state == 'need-code' ? 'den Code' : 'die Bestätigung'} (${current.codeType || 'Bestätigung'}).`);
		await report(api, current);
	}

	try {
		while (!authenticated && !failure) {
			if (Date.now() >= deadline) {
				failure = LOGIN.timeout;
				break;
			}
			// warten: Anmeldung fertig, Fehler oder nächste Abfrage des Auftrags
			await Promise.race([
				new Promise((resolve) => {
					wake = resolve;
				}),
				sleep(Math.min(t.pollMs, Math.max(0, deadline - Date.now())))
			]);
			wake = null;
			if (authenticated || failure || !current) {
				continue;
			}
			let job;
			try {
				job = await api.job();
			} catch (err) {
				if (err.cancelled) {
					throw err;
				}
				log.debug(`Auftrag während der Anmeldung: ${err.message}`);
				continue;
			}
			let jl = job && job.login;
			if (jl && jl.password && (jl.password !== creds.password || (jl.user && String(jl.user).trim() !== creds.user))) {
				flow.cancel();
				return {restart: {user: String(jl.user || creds.user).trim(), password: String(jl.password), code: normCode(jl.code)}};
			}
			let code = normCode(jl && jl.code);
			if (!code || code === lastCode) {
				continue;
			}
			lastCode = code;
			log.addSecret(code);
			log.info('Steam-Anmeldung: Code erhalten, wird geprüft.');
			await report(api, {state: 'working', hint: LOGIN.checkingCode});
			try {
				await withTimeout(flow.submitCode(code), t.loginStartMs, 'Steam antwortet nicht');
			} catch (err) {
				if (err.cancelled) {
					throw err;
				}
				let r = eresultOf(err);
				log.debug(`Code abgelehnt: ${rawReason(err)}`);
				if (r == EResult.InvalidLoginAuthCode || r == EResult.TwoFactorCodeMismatch) {
					wrong++;
					log.warn(`Steam-Anmeldung: Code falsch (${wrong}. Versuch).`);
					if (wrong >= MAX_WRONG_CODES) {
						failure = LOGIN.wrongCode;
						break;
					}
					await report(api, Object.assign({}, current, {hint: LOGIN.wrongCodeAgain}));
				} else if (r == EResult.ExpiredLoginAuthCode) {
					await report(api, Object.assign({}, current, {hint: LOGIN.codeExpired}));
				} else {
					failure = err.timeout ? LOGIN.unreachable : loginErrorText(err);
					log.warn(`Steam-Anmeldung: Code nicht angenommen (${rawReason(err)})`);
				}
			}
		}
	} finally {
		if (!authenticated) {
			flow.cancel();
		}
	}

	if (!authenticated) {
		log.warn(`Steam-Anmeldung: ${failure}`);
		await report(api, {state: 'error', hint: failure});
		return null;
	}
	let res = flow.result();
	let jwt = decodeJwt(res.refreshToken);
	if (!res.refreshToken || !jwt) {
		await report(api, {state: 'error', hint: 'Steam hat kein gültiges Token geliefert – bitte noch einmal anmelden.'});
		return null;
	}
	log.addSecret(res.refreshToken);
	let out = {
		refreshToken: res.refreshToken,
		botName: res.accountName || creds.user,
		botSteamId: res.steamId || String(jwt.sub || ''),
		tokenUntil: Number(jwt.exp) || 0
	};
	log.info(`Steam-Anmeldung: angemeldet als „${out.botName}“ (${out.botSteamId}), Token gültig bis ${fmtDate(out.tokenUntil, true)}.`);
	await report(api, Object.assign({state: 'done', hint: `Angemeldet als ${out.botName}`}, out));
	return out;
}

module.exports = {interactiveLogin, prompt, GUARD};
