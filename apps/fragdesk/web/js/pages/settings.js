// FragDesk Handy-App – Verbindung, Konto (eigener Server), Darstellung, Entkoppeln
import {h, icon, card, chips, notice, isIOS, standalone, usageBar, fmtDay} from '../ui.js';
import * as api from '../api.js';
import {store, applyTheme} from '../app.js';

const count = (n, one, many) => `${n ?? '–'} ${n === 1 ? one : many}`;

const kv = (k, v) => h('div', {class: 'row', style: {minHeight: '46px'}},
  h('div', {class: 'main', style: {flex: 'none'}}, h('b', {style: {fontWeight: '500'}}, k)),
  h('div', {class: 'side', style: {flex: '1', minWidth: '0'}},
    h('b', {style: {fontWeight: '600', color: 'var(--text2)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis'}}, v)));

export async function render(ctx) {
  ctx.title('Einstellungen');
  const info = await ctx.refreshInfo().catch(() => ctx.info) || ctx.info || {};
  if (!ctx.current()) return;
  if (info.kind === 'server') return renderServer(ctx, info);
  const out = [
    h('h1', {class: 'page-title'}, 'Einstellungen'),
    h('div', {class: 'page-sub'}, 'FragDesk auf dem Handy'),
    h('div', {class: 'section'}, 'Verbindung'),
    h('section', {class: 'card flush'}, h('div', {class: 'list'},
      kv('Verbunden mit', 'FragDesk auf deinem PC'),
      kv('Adresse', location.host),
      kv('Spieler', (info.me && info.me.name) || '–'),
      kv('Matches', String(info.matches ?? '–')),
      kv('FragDesk-Version', info.version || '–'))),
  ];
  if (!window.isSecureContext) {
    out.push(notice('info', 'Tipp: HTTPS über Tailscale',
      'Mit der HTTPS-Adresse aus Tailscale sicherst du Videos direkt in Fotos, und die App startet auch ohne Verbindung mit den zuletzt geladenen Daten. ' +
      'Anleitung in FragDesk unter Einstellungen › Handy-App.'));
  }
  out.push(...look(ctx));
  out.push(h('div', {class: 'section'}, 'Gerät'));
  out.push(unpairButton('Dieses Handy von FragDesk entkoppeln? Zum erneuten Verbinden brauchst du einen neuen Code aus FragDesk.'));
  out.push(h('p', {class: 'muted', style: {fontSize: '12.5px', textAlign: 'center', marginTop: '22px'}},
    'Gekoppelte Geräte siehst und entfernst du auch in FragDesk unter Einstellungen › Handy-App.'));
  ctx.main.replaceChildren(...out);
}

// Eigener Server: Konto und Speicher aus /api/v1/me, für Admins der Weg zur Verwaltung
async function renderServer(ctx, info) {
  let me = api.cached('/api/v1/me');
  const draw = () => {
    const u = (me && me.user) || {}, q = (me && me.quota) || {}, n = (me && me.counts) || {};
    const out = [
      h('h1', {class: 'page-title'}, 'Einstellungen'),
      h('div', {class: 'page-sub'}, 'FragDesk auf dem Handy'),
      h('div', {class: 'section'}, 'Konto'),
      card(null,
        h('div', {class: 'account'},
          h('span', {class: 'avatar'}, (u.name || '?').trim().charAt(0).toUpperCase()),
          h('div', {class: 'main'}, h('b', null, u.name || (info.me && info.me.name) || '–', u.admin ? h('span', {class: 'pill neutral'}, 'Admin') : null),
            h('small', null, me ? `${count(n.matches, 'Match', 'Matches')} · ${count(n.clips, 'Video', 'Videos')}` : `${info.matches ?? '–'} Matches`))),
        me ? usageBar(q.used || 0, q.limit || 0) : null,
        me && q.limit > 0 && q.used >= q.limit
          ? h('div', {class: 'sub', style: {fontSize: '13px', marginTop: '10px'}}, 'Dein Speicher ist voll – neue Videos lädt FragDesk erst wieder hoch, wenn Platz ist. Lösche alte Videos in FragDesk oder frag den Admin nach mehr Platz.')
          : null),
    ];
    if (u.admin) {
      out.push(h('section', {class: 'card flush'}, h('div', {class: 'list'},
        h('a', {class: 'row', href: '#/admin'},
          h('span', {class: 'lead'}, icon('users')),
          h('div', {class: 'main'}, h('b', null, 'Server verwalten'), h('small', null, 'Freunde einladen, Konten und Speicher')),
          icon('chev', 'chev')))));
    }
    out.push(h('div', {class: 'section'}, 'Verbindung'));
    out.push(h('section', {class: 'card flush'}, h('div', {class: 'list'},
      kv('Verbunden mit', 'deinem FragDesk-Server'),
      kv('Adresse', location.host),
      kv('Server-Version', info.version || '–'),
      me && me.user ? kv('Konto seit', fmtDay(me.user.created)) : null)));
    out.push(...look(ctx));
    out.push(h('div', {class: 'section'}, 'Gerät'));
    out.push(unpairButton('Dieses Handy vom Server abmelden? Zum erneuten Verbinden brauchst du einen neuen Code aus FragDesk („Handy verbinden“).',
      'Dieses Handy abmelden'));
    out.push(h('p', {class: 'muted', style: {fontSize: '12.5px', textAlign: 'center', marginTop: '22px'}},
      'Neue Matches und Videos lädt FragDesk auf deinem PC im Hintergrund auf den Server – hier siehst du sie auch, wenn dein PC aus ist.'));
    ctx.main.replaceChildren(...out);
  };
  draw();
  me = await api.get('/api/v1/me').catch(() => me);
  if (!ctx.current()) return;
  draw();
}

function look(ctx) {
  const themes = ['auto', 'dark', 'light'];
  const out = [h('div', {class: 'section'}, 'Darstellung')];
  out.push(chips(['Automatisch', 'Dunkel', 'Hell'], Math.max(0, themes.indexOf(store.get('theme', 'auto'))), i => {
    store.set('theme', themes[i]);
    applyTheme();
    ctx.rerender();
  }));
  if (isIOS && !standalone) {
    out.push(h('div', {class: 'section'}, 'Als App'));
    out.push(card(null, h('ol', {class: 'steps', style: {margin: '0'}},
      h('li', null, 'In Safari unten auf ', icon('share'), ' „Teilen“ tippen'),
      h('li', null, '„Zum Home-Bildschirm“ wählen'),
      h('li', null, '„Hinzufügen“ tippen und FragDesk über das neue Symbol öffnen'))));
  }
  return out;
}

function unpairButton(question, label = 'Dieses Handy entkoppeln') {
  const btn = h('button', {class: 'btn danger block', type: 'button'}, icon('link'), label);
  btn.addEventListener('click', async () => {
    if (!confirm(question)) return;
    try { await api.post('/api/v1/logout'); } catch (e) { /* trotzdem neu laden */ }
    location.replace(location.pathname);
  });
  return btn;
}
