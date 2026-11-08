// FragDesk Handy-App – „Demos automatisch laden“ (nur eigener Server): die Karte in den Einstellungen (jedes Konto,
// eigener Authentifizierungscode) und der Steam-Bot unter Server verwalten (Admins, einmal für den ganzen Server).
// Beide Karten laden und aktualisieren sich selbst; Eingaben bleiben beim Aktualisieren stehen.
import {h, icon, sheet, closeSheet, toast, notice, spinner, fmtDay} from '../ui.js';
import * as api from '../api.js';

const HELP_URL = 'https://help.steampowered.com/de/wizard/HelpWithGameIssue/?appid=730&issueid=128';
const KEY_URL = 'https://steamcommunity.com/dev/apikey';
const ME = '/api/v1/me/autodl';
const BOT = '/api/v1/admin/steam';

const plural = (n, one, many) => `${n} ${n === 1 ? one : many}`;
const ext = (href, text) => h('a', {class: 'extlink', href, target: '_blank', rel: 'noopener noreferrer'}, text, icon('external'));

// Eingabefeld mit Beschriftung; mono: Codes in fester Breite
function field(label, attrs, help) {
  const input = h('input', Object.assign({autocomplete: 'off', autocorrect: 'off', spellcheck: 'false'}, attrs));
  return {input, el: h('label', {class: 'field'}, h('span', null, label), input, help ? h('small', null, help) : null)};
}

function toggle(on, change, label) {
  const input = h('input', {type: 'checkbox', role: 'switch', 'aria-label': label});
  input.checked = !!on;
  input.addEventListener('change', () => change(input.checked, input));
  return h('label', {class: 'switch'}, input, h('span', {class: 'knob'}));
}

function statusLine(d) {
  return h('div', {class: 'autodl-status ' + (d.tone || 'wait')}, h('span', {class: 'dot'}), h('span', null, d.status || '–'));
}

// ---- Einstellungen: Demos automatisch laden (jedes Konto) ------------------------------------------------------------
export function autodlSection(ctx) {
  const box = h('section', {class: 'card autodl'});
  let d = api.cached(ME), polls = 0, timer = 0;

  const save = async (body, okText, btn) => {
    if (btn) btn.disabled = true;
    try {
      d = await api.put(ME, body);
      if (okText) toast(okText);
      closeSheet();
      draw();
      watch();
      return true;
    } catch (e) {
      toast(e.message, true);
      return false;
    } finally {
      if (btn) btn.disabled = false;
    }
  };

  // Nach „Jetzt prüfen“ oder dem Einschalten ein paar Mal nachsehen, bis der Server fertig ist
  const watch = () => {
    clearTimeout(timer);
    polls = 0;
    const tick = async () => {
      if (!ctx.current() || ++polls > 40) return;
      try { d = await api.get(ME); } catch (e) { return; }
      if (!ctx.current()) return;
      draw();
      if (d.checking || (d.enabled && !d.lastCheck && d.ready)) timer = setTimeout(tick, 3000);
    };
    timer = setTimeout(tick, 3000);
  };

  const codeFields = (forChange) => {
    const auth = field('Authentifizierungscode', {
      class: 'mono', placeholder: d && d.authCodeMasked ? d.authCodeMasked + ' (gespeichert)' : 'AAAA-AAAAA-AAAA', autocapitalize: 'characters',
      inputmode: 'text', maxlength: '20', 'aria-label': 'Authentifizierungscode',
    }, forChange ? 'Leer lassen = den gespeicherten Code behalten.' : null);
    const known = field('Freigabecode deines letzten Matches', {   // proportional: 34 Zeichen passen sonst nicht aufs iPhone
      class: 'plain', placeholder: (d && d.knownCode) || 'CSGO-xxxxx-xxxxx-…', autocapitalize: 'off', maxlength: '40',
      'aria-label': 'Freigabecode deines letzten Matches',
    }, 'Steht auf derselben Steam-Seite unter dem Authentifizierungscode (beginnt mit CSGO-).');
    const link = h('p', {class: 'field-help'}, ext(HELP_URL, 'Code bei Steam holen'),
      h('span', null, ' – auf der Steam-Seite „Authentifizierungscode erstellen“ tippen.'));
    return {auth, known, nodes: [auth.el, link, known.el]};
  };

  const changeSheet = () => {
    const f = codeFields(true);
    const btn = h('button', {class: 'btn primary block', type: 'button'}, 'Speichern');
    btn.addEventListener('click', () => {
      const body = {authCode: f.auth.input.value, knownCode: f.known.input.value};
      if (!body.authCode.trim() && !body.knownCode.trim()) return closeSheet();
      save(body, 'Gespeichert – der Server prüft mit den neuen Codes.', btn);
    });
    sheet({
      title: 'Codes ändern',
      text: 'Einen neuen Freigabecode brauchst du nur, wenn der Server sagt, dass er nicht passt – sonst zählt er von selbst weiter.',
      body: h('div', null, f.nodes, btn),
    });
  };

  const draw = () => {
    if (!d) {
      box.replaceChildren(spinner());
      return;
    }
    const saved = !!(d.authCodeMasked && d.knownCode);
    const out = [];
    out.push(h('div', {class: 'autodl-head'},
      h('span', {class: 'lead'}, icon('save')),
      h('div', {class: 'main'}, h('b', null, 'Demos automatisch laden'), h('small', null, 'Premier, Wettkampf und Wingman')),
      saved && d.steamId ? toggle(d.enabled, (on, input) => {
        input.disabled = true;
        save({enabled: on}, on ? 'Eingeschaltet' : 'Ausgeschaltet – deine Codes bleiben gespeichert.').then(ok => {
          if (!ok) { input.checked = !on; input.disabled = false; }
        });
      }, 'Demos automatisch laden') : null));
    out.push(statusLine(d));
    if (d.steamId && !d.botReady && d.admin) {
      out.push(h('a', {class: 'btn block', href: '#/admin?steam=1', style: {marginBottom: '12px'}}, icon('shield'),
        d.botNeedsLogin ? 'Steam-Bot neu anmelden' : 'Steam-Bot einrichten'));
    }
    if (d.steamId && !saved) {   // erstes Einschalten: beide Codes eintragen
      const f = codeFields(false);
      const btn = h('button', {class: 'btn primary block', type: 'button'}, icon('save'), 'Einschalten');
      btn.addEventListener('click', () => save({authCode: f.auth.input.value, knownCode: f.known.input.value, enabled: true},
        d.ready ? 'Eingeschaltet – der Server prüft gleich.' : 'Gespeichert – es geht los, sobald der Steam-Bot bereit ist.', btn));
      out.push(h('div', {class: 'autodl-form'}, f.nodes, btn));
    } else if (d.steamId) {
      const rows = [];
      if (d.lastNew) rows.push(['Neuestes Match geladen', fmtDay(d.lastNew)]);
      rows.push(['Freigabecode', d.knownCode, 'mono']);   // Codes in Festbreitenschrift: der Freigabecode bleibt in einer Zeile
      rows.push(['Authentifizierungscode', d.authCodeMasked, 'mono']);
      out.push(h('div', {class: 'autodl-facts'}, rows.map(([k, v, cls]) => h('div', {class: 'kv'}, h('span', null, k), h('b', {class: cls || null}, v)))));
      const btns = [];
      if (d.enabled && d.ready) {
        const check = h('button', {class: 'btn', type: 'button', disabled: d.checking || null}, icon('refresh'), 'Jetzt prüfen');
        check.addEventListener('click', async () => {
          check.disabled = true;
          try {
            d = await api.post(ME + '/check');
            toast('Der Server prüft gleich – neue Matches erscheinen von selbst.');
            draw();
            watch();
          } catch (e) {
            toast(e.message, true);
            check.disabled = false;
          }
        });
        btns.push(check);
      }
      const change = h('button', {class: 'btn', type: 'button'}, icon('key'), 'Codes ändern');
      change.addEventListener('click', changeSheet);
      btns.push(change);
      out.push(h('div', {class: 'btnrow'}, btns));
      const off = h('button', {class: 'btn danger block', type: 'button', style: {marginTop: '8px'}}, icon('close'), 'Ausschalten');
      off.addEventListener('click', async () => {
        if (!confirm('„Demos automatisch laden“ ausschalten und beide Codes vom Server löschen?')) return;
        off.disabled = true;
        try {
          d = await api.del(ME);
          toast('Ausgeschaltet – deine Codes sind vom Server gelöscht.');
          draw();
        } catch (e) {
          toast(e.message, true);
          off.disabled = false;
        }
      });
      out.push(off);
    }
    out.push(h('p', {class: 'autodl-explain'},
      'Der Server holt neue Matches selbst bei Valve, wertet sie aus und legt sie in dein Konto – auch wenn dein PC aus ist. ',
      'Das geht nur für Premier, Wettkampf und Wingman (FACEIT nicht) und nur etwa einen Monat zurück, so lange hebt Valve die Demos auf. ',
      'Geladen werden jeweils die Matches nach dem Freigabecode. Mit dem Authentifizierungscode kann der Server nur deinen Spielverlauf abfragen, dich nicht anmelden.'));
    box.replaceChildren(...out);
  };

  draw();
  api.get(ME).then(v => {
    d = v;
    if (!ctx.current()) return;
    draw();
    if (d.checking) watch();
  }).catch(e => {
    if (!d) box.replaceChildren(notice('err', 'Demos automatisch laden', e.message));
  });
  return box;
}

// ---- Server verwalten: Steam-Bot (Admins) -----------------------------------------------------------------------------
const PENDING = ['working', 'need-code', 'need-confirm'];

export function steamBotSection(ctx) {
  const box = h('div', {class: 'steambot'});
  const status = h('section', {class: 'card flush'});
  const login = h('div');           // Fortschritt der Anmeldung (bleibt stehen, solange sich nichts ändert)
  const actions = h('div', {class: 'steambot-actions'});
  const help = guide();             // offen, solange der Bot noch nicht bereit ist
  box.append(status, login, actions, help);
  let d = api.cached(BOT), shown = '', timer = 0, helpSet = false;

  const refresh = async () => {
    try { d = await api.get(BOT); } catch (e) { return; }
    if (ctx.current()) draw();
  };
  // während der Anmeldung alle 2 s nachsehen (der Helfer meldet sich über den Server)
  const poll = () => {
    clearTimeout(timer);
    if (!d || !PENDING.includes(d.login.state)) return;
    timer = setTimeout(async () => {
      if (!ctx.current()) return;
      await refresh();
      poll();
    }, 2000);
  };
  const send = async (method, path, body, okText, btn) => {
    if (btn) btn.disabled = true;
    try {
      d = await api[method](path, body);
      if (okText) toast(okText);
      closeSheet();
      shown = '';
      draw();
      poll();
      return true;
    } catch (e) {
      toast(e.message, true);
      return false;
    } finally {
      if (btn) btn.disabled = false;
    }
  };

  const loginSheet = () => {
    const user = field('Benutzername des Bot-Kontos', {class: 'plain', autocapitalize: 'off', 'aria-label': 'Benutzername'},
      'So, wie du dich bei Steam anmeldest – nicht der Anzeigename.');
    const pw = field('Passwort', {class: 'plain', type: 'password', autocomplete: 'current-password', 'aria-label': 'Passwort'},
      'Bleibt nur kurz im Arbeitsspeicher des Servers und landet nie auf der Platte.');
    const btn = h('button', {class: 'btn primary block', type: 'button'}, icon('shield'), 'Anmelden');
    btn.addEventListener('click', () => {
      if (!user.input.value.trim() || !pw.input.value) return toast('Benutzername und Passwort eintragen.', true);
      send('post', BOT + '/login', {user: user.input.value.trim(), password: pw.input.value}, null, btn);
    });
    sheet({
      title: 'Steam-Bot anmelden',
      text: 'Gleich fragt Steam nach einem Code (E-Mail oder Steam-App) oder du bestätigst die Anmeldung in der Steam-App.',
      body: h('div', null, user.el, pw.el, btn),
    });
  };

  const keySheet = () => {
    const key = field('Web-API-Schlüssel', {class: 'mono', autocapitalize: 'characters', maxlength: '40', placeholder: d.apiKeySet ? 'gespeichert – neuer ersetzt ihn' : '32 Zeichen, 0–9 und A–F', 'aria-label': 'Web-API-Schlüssel'},
      'Gibt es nur für Konten mit mindestens 5 € Umsatz und Steam Guard über die Steam-App.');
    const btn = h('button', {class: 'btn primary block', type: 'button'}, icon('key'), 'Speichern');
    btn.addEventListener('click', () => {
      if (!key.input.value.trim()) return toast('Schlüssel einfügen.', true);
      send('put', BOT + '/apikey', {key: key.input.value}, 'Schlüssel gespeichert', btn);
    });
    sheet({
      title: 'Web-API-Schlüssel',
      text: 'Am besten vom Bot-Konto: als Bot bei Steam angemeldet auf der Seite eine Domain eintragen (z. B. die Adresse deines Servers) und den ' +
        'Schlüssel kopieren. Der Schlüssel deines eigenen Kontos ginge auch – er läge dann aber auf dem Server, und wer ihn erbeutet, kann damit ' +
        'Betrug beim Handeln mit Skins versuchen. Deshalb lieber den des Bots.',
      body: h('div', null, h('p', {class: 'field-help'}, ext(KEY_URL, 'steamcommunity.com/dev/apikey öffnen')), key.el, btn),
    });
  };

  const drawLogin = () => {
    const l = d.login;
    const sig = [l.state, l.hint, l.codeType].join('|');
    if (sig === shown) return;   // unverändert: Eingabe (Code) stehen lassen
    shown = sig;
    if (l.state === 'idle') return login.replaceChildren();
    if (l.state === 'working') {
      return login.replaceChildren(h('div', {class: 'card steam-login'}, h('div', {class: 'steam-wait'}, h('div', {class: 'spinner small'}),
        h('div', null, h('b', null, 'Anmeldung läuft …'), h('small', null, l.hint)))));
    }
    // Steam-Guard-Code (5 Zeichen): Feld und Knopf
    const codeEntry = (primary) => {
      const code = h('input', {class: 'code' + (primary ? '' : ' small'), maxlength: '5', autocomplete: 'one-time-code', autocapitalize: 'characters',
        autocorrect: 'off', spellcheck: 'false', placeholder: '•••••', 'aria-label': 'Steam-Guard-Code'});
      const btn = h('button', {class: 'btn block' + (primary ? ' primary' : ''), type: 'button'}, 'Code senden');
      const go = () => {
        if (code.value.trim().length !== 5) return toast('Der Code hat 5 Zeichen.', true);
        send('post', BOT + '/code', {code: code.value.trim()}, null, btn);
      };
      btn.addEventListener('click', go);
      code.addEventListener('keydown', e => { if (e.key === 'Enter') go(); });
      return [code, btn];
    };
    if (l.state === 'need-code') {
      login.replaceChildren(h('div', {class: 'card steam-login'},
        h('div', {class: 'steam-wait'}, icon(l.codeType === 'app' ? 'phone' : 'info'),
          h('div', null, h('b', null, l.codeType === 'app' ? 'Code aus der Steam-App' : 'Code aus der E-Mail von Steam'), h('small', null, l.hint))),
        codeEntry(true)));
      return;
    }
    if (l.state === 'need-confirm') {   // bestätigen – mit codeType "app" geht stattdessen auch der Code aus der App
      return login.replaceChildren(h('div', {class: 'card steam-login'}, h('div', {class: 'steam-wait'}, icon('phone'),
        h('div', null, h('b', null, 'In der Steam-App bestätigen'), h('small', null, l.hint)), h('div', {class: 'spinner small'})),
        l.codeType === 'app' ? [h('div', {class: 'steam-or'}, 'Oder den Code aus der Steam-App eingeben:'), codeEntry(false)] : null));
    }
    if (l.state === 'done') return login.replaceChildren(notice('info', 'Bot angemeldet', l.hint));
    login.replaceChildren(notice('err', 'Anmeldung hat nicht geklappt', l.hint));
  };

  const draw = () => {
    if (!d) {
      status.replaceChildren(spinner());
      return;
    }
    const b = d.bot, run = d.lastRun || {};
    const row = (ic, title, sub, onClick, cls) => {
      const el = h(onClick ? 'button' : 'div', {class: 'row' + (cls ? ' ' + cls : ''), type: onClick ? 'button' : null},
        h('span', {class: 'lead'}, icon(ic)), h('div', {class: 'main'}, h('b', null, title), h('small', null, sub)), onClick ? icon('chev', 'chev') : null);
      if (onClick) el.addEventListener('click', onClick);
      return el;
    };
    status.replaceChildren(h('div', {class: 'list'},
      b.needsLogin
        ? row('warning', 'Neu anmelden nötig' + (b.name ? ': ' + b.name : ''), b.problem || 'Steam nimmt die Anmeldung des Bots nicht mehr an.', null, 'wrap')
        : row('shield', b.loggedIn ? 'Angemeldet als ' + (b.name || 'Bot') : 'Nicht angemeldet',
          b.loggedIn ? (b.tokenUntil ? 'Token gültig bis ' + fmtDay(b.tokenUntil) : 'Token ohne Ablaufdatum') : 'Einmal für den ganzen Server – Anleitung unten'),
      row('key', 'Web-API-Schlüssel', d.apiKeySet ? 'gesetzt' : 'fehlt noch', keySheet),
      row('clock', 'Letzter Lauf', run.running ? 'läuft gerade …' : run.at ? fmtDay(run.at) + (run.text ? ' · ' + run.text : '') : 'noch keiner'),
      row('users', 'Demos automatisch laden', d.accounts ? plural(d.accounts, 'Konto hat', 'Konten haben') + ' es eingeschaltet' : 'noch von keinem Konto eingeschaltet')));
    drawLogin();
    if (!helpSet) {
      help.open = !d.ready;
      helpSet = true;
    }
    const out = [];
    if (d.helper) out.push(notice('err', 'Auf dem Server fehlt etwas', d.helper));
    const pending = PENDING.includes(d.login.state);
    // während einer Anmeldung: neu anfangen (z. B. Passwort vertippt) – ersetzt die laufende
    const start = h('button', {class: 'btn block' + (b.loggedIn || pending ? '' : ' primary'), type: 'button', disabled: d.helper ? true : null},
      icon('shield'), pending ? 'Neu anfangen' : b.loggedIn || b.needsLogin ? 'Bot neu anmelden' : 'Bot anmelden');
    start.addEventListener('click', loginSheet);
    out.push(start);
    if (b.loggedIn || b.needsLogin) {
      const logout = h('button', {class: 'btn danger block', type: 'button'}, icon('close'), 'Bot abmelden');
      logout.addEventListener('click', () => {
        if (!confirm('Steam-Bot abmelden? Dann lädt der Server keine Demos mehr, bis du ihn wieder anmeldest. Der API-Schlüssel bleibt.')) return;
        send('del', BOT, undefined, 'Bot abgemeldet', logout);
      });
      out.push(logout);
    }
    actions.replaceChildren(...out);
  };

  draw();
  refresh().then(poll);
  return box;
}

function guide() {
  return h('details', {class: 'card guide'},
    h('summary', null, h('div', {class: 'h'}, h('b', null, 'So richtest du den Steam-Bot ein'),
      h('small', null, 'Einmal für den ganzen Server – jeder trägt danach nur noch seine eigenen Codes ein')), icon('down', 'chev')),
    h('ol', {class: 'steps', style: {margin: '0'}},
      h('li', null, h('b', null, 'Neues Steam-Konto'), ' nur für den Server anlegen (nicht dein eigenes!) – auf ', ext('https://store.steampowered.com/join/', 'store.steampowered.com/join'), ' mit eigener E-Mail-Adresse.'),
      h('li', null, h('b', null, 'Counter-Strike 2 hinzufügen'), ' – kostenlos: im Steam-Shop bei CS2 auf „Spielen“ bzw. „Zur Bibliothek hinzufügen“.'),
      h('li', null, h('b', null, '5 € aufladen'), ' (z. B. Steam-Guthabenkarte). Ohne mindestens 5 € Umsatz gibt Valve keinen API-Schlüssel heraus; das Guthaben bleibt auf dem Konto.'),
      h('li', null, h('b', null, 'Steam Guard mit der Steam-App'), ': die Steam-App aufs Handy, mit dem Bot-Konto anmelden, unter „Steam Guard“ den Authenticator einrichten.'),
      h('li', null, h('b', null, 'Web-API-Schlüssel holen'), ': als Bot angemeldet auf ', ext(KEY_URL, 'steamcommunity.com/dev/apikey'), ', als Domain z. B. die Adresse deines Servers eintragen, Schlüssel oben unter „Web-API-Schlüssel“ einfügen. Nimm den des Bots, nicht den deines eigenen Kontos.'),
      h('li', null, h('b', null, 'Bot anmelden'), ': Benutzername und Passwort des Bot-Kontos, dann den Code eingeben oder in der Steam-App bestätigen.')),
    h('p', {class: 'autodl-explain', style: {marginBottom: '0'}},
      'Passwort und Code bleiben höchstens 15 Minuten im Arbeitsspeicher des Servers und landen nie auf der Platte. Gespeichert wird nur ein ',
      'Anmelde-Token wie bei „Angemeldet bleiben“ – es läuft nach einigen Monaten ab (Datum oben), dann hier neu anmelden.'));
}
