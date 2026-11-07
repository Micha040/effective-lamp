// FragDesk Handy-App – Verbindung, Darstellung, Entkoppeln
import {h, icon, card, chips, notice, isIOS, standalone} from '../ui.js';
import * as api from '../api.js';
import {store, applyTheme} from '../app.js';

export async function render(ctx) {
  ctx.title('Einstellungen');
  const info = await ctx.refreshInfo().catch(() => ctx.info) || ctx.info || {};
  if (!ctx.current()) return;
  const kv = (k, v) => h('div', {class: 'row', style: {minHeight: '46px'}}, h('div', {class: 'main'}, h('b', {style: {fontWeight: '500'}}, k)),
    h('div', {class: 'side'}, h('b', {style: {fontWeight: '600', color: 'var(--text2)'}}, v)));
  const themes = ['auto', 'dark', 'light'];
  const out = [
    h('h1', {class: 'page-title'}, 'Einstellungen'),
    h('div', {class: 'page-sub'}, 'FragDesk auf dem Handy'),
    h('div', {class: 'section'}, 'Verbindung'),
    h('section', {class: 'card flush'}, h('div', {class: 'list'},
      kv('Verbunden mit', info.kind === 'server' ? 'deinem Server' : 'FragDesk auf deinem PC'),
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
  out.push(h('div', {class: 'section'}, 'Darstellung'));
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
  out.push(h('div', {class: 'section'}, 'Gerät'));
  const btn = h('button', {class: 'btn danger block', type: 'button'}, icon('link'), 'Dieses Handy entkoppeln');
  btn.addEventListener('click', async () => {
    if (!confirm('Dieses Handy von FragDesk entkoppeln? Zum erneuten Verbinden brauchst du einen neuen Code aus FragDesk.')) return;
    try { await api.post('/api/v1/logout'); } catch (e) { /* trotzdem neu laden */ }
    location.replace(location.pathname);
  });
  out.push(btn);
  out.push(h('p', {class: 'muted', style: {fontSize: '12.5px', textAlign: 'center', marginTop: '22px'}},
    'Gekoppelte Geräte siehst und entfernst du auch in FragDesk unter Einstellungen › Handy-App.'));
  ctx.main.replaceChildren(...out);
}
