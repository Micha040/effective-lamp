// FragDesk Handy-App – eigenen Server verwalten (nur Admins): Freunde einladen, Einladungen zurückziehen, Konten mit
// Speicher, Limit ändern, Konten entfernen
import {h, icon, card, chips, sheet, closeSheet, toast, skeleton, usageBar, fmtBytes, fmtDay, copyText} from '../ui.js';
import * as api from '../api.js';

const GiB = 1024 ** 3;
const LIMITS = [5, 10, 15, 25, 50, 0];   // GB, 0 = unbegrenzt
let lastInvite = null;   // zuletzt erstellter Link (bleibt sichtbar, bis er eingelöst oder zurückgezogen ist)

const count = (n, one, many) => `${n} ${n === 1 ? one : many}`;

export async function render(ctx) {
  ctx.title('Server verwalten');
  const paths = ['/api/v1/me', '/api/v1/admin/users', '/api/v1/admin/invites'];
  const old = paths.map(p => api.cached(p));
  if (old.every(Boolean) && !ctx.fresh) draw(ctx, ...old);
  else ctx.main.replaceChildren(h('h1', {class: 'page-title'}, 'Server'), h('div', {class: 'page-sub'}, location.host), ...skeleton(190, 120, 260));
  const [me, users, invites] = await Promise.all(paths.map(p => api.get(p)));
  if (!ctx.current()) return;
  if (lastInvite && !invites.some(i => i.id === lastInvite.id)) lastInvite = null;   // eingelöst oder zurückgezogen
  draw(ctx, me, users, invites);
}

function draw(ctx, me, users, invites) {
  const used = users.reduce((s, u) => s + u.used, 0);
  const out = [
    h('h1', {class: 'page-title'}, 'Server'),
    h('div', {class: 'page-sub'}, `${location.host} · ${count(users.length, 'Konto', 'Konten')} · ${fmtBytes(used)} belegt`),
    h('div', {class: 'section'}, 'Freunde einladen'),
    inviteCard(ctx),
  ];
  if (invites.length) {
    out.push(h('div', {class: 'section'}, 'Offene Einladungen'));
    out.push(h('section', {class: 'card flush'}, h('div', {class: 'list'}, invites.map(inv => {
      const revoke = h('button', {class: 'iconbtn', type: 'button', 'aria-label': 'Einladung zurückziehen'}, icon('close'));
      revoke.addEventListener('click', async () => {
        if (!confirm('Diese Einladung zurückziehen? Der Link gilt dann nicht mehr.')) return;
        try {
          await api.del('/api/v1/admin/invites/' + encodeURIComponent(inv.id));
          toast('Einladung zurückgezogen');
        } catch (e) { toast(e.message, true); }
        ctx.rerender();
      });
      return h('div', {class: 'row'},
        h('span', {class: 'lead'}, icon(inv.admin ? 'shield' : 'link')),
        h('div', {class: 'main'}, h('b', null, inv.admin ? 'Einladung als Admin' : 'Einladung'),
          h('small', null, `${inv.byName ? 'von ' + inv.byName + ' · ' : ''}gilt bis ${fmtDay(inv.expires)}`)),
        revoke);
    }))));
  }
  out.push(h('div', {class: 'section'}, 'Konten'));
  const mine = me && me.user ? me.user.id : '';
  const sorted = users.slice().sort((a, b) => (b.id === mine) - (a.id === mine) || a.name.localeCompare(b.name, 'de'));   // du zuerst
  out.push(h('section', {class: 'card flush'}, h('div', {class: 'list'}, sorted.map(u => {
    const self = u.id === mine;
    const row = h('button', {class: 'row user', type: 'button'},
      h('span', {class: 'avatar'}, (u.name || '?').trim().charAt(0).toUpperCase()),
      h('div', {class: 'main'},
        h('b', null, u.name, u.admin ? h('span', {class: 'pill neutral'}, 'Admin') : null, self ? h('span', {class: 'pill neutral'}, 'Du') : null),
        h('small', null, `${count(u.matches, 'Match', 'Matches')} · ${count(u.clips, 'Video', 'Videos')} · zuletzt ${fmtDay(u.lastSeen).replace(/^(Heute|Gestern)/, s => s.toLowerCase())}`),
        usageBar(u.used, u.limit, null)),
      icon('chev', 'chev'));
    row.addEventListener('click', () => userSheet(ctx, u, self));
    return row;
  }))));
  out.push(h('p', {class: 'muted', style: {fontSize: '12.5px', textAlign: 'center', marginTop: '18px'}},
    'Jeder sieht nur sein eigenes Konto. Eingeladene Konten bekommen 15 GB, Admins unbegrenzt – das Limit änderst du hier pro Konto.'));
  ctx.main.replaceChildren(...out);
}

// Einladen: Link erstellen, dann teilen (Messenger) oder kopieren
function inviteCard(ctx) {
  let asAdmin = false;
  const make = h('button', {class: 'btn primary block', type: 'button'}, icon('plus'), 'Einladungslink erstellen');
  const kinds = chips(['Freund', 'Admin'], 0, i => {
    asAdmin = i === 1;
    kinds.querySelectorAll('.chip').forEach((c, k) => c.classList.toggle('on', k === i));
  });
  kinds.style.margin = '0 0 10px';
  kinds.style.padding = '0';
  make.addEventListener('click', async () => {
    make.disabled = true;
    try {
      lastInvite = await api.post('/api/v1/admin/invite', {admin: asAdmin, days: 7});
      ctx.rerender();
    } catch (e) {
      toast(e.message, true);
      make.disabled = false;
    }
  });
  const body = [
    h('p', {class: 'sub', style: {margin: '0 0 12px', fontSize: '14px'}},
      'Ein Link pro Freund – er gilt 7 Tage und nur einmal. Dein Freund fügt ihn in FragDesk am PC ein: Einstellungen › Handy-App › Eigener Server › „Einladung einfügen“.'),
    kinds, make,
  ];
  if (lastInvite) body.push(linkBox(lastInvite));
  return card(null, ...body);
}

function linkBox(inv) {
  const input = h('input', {class: 'linkbox', readonly: true, value: inv.link, 'aria-label': 'Einladungslink'});
  input.addEventListener('focus', () => input.select());
  const copy = h('button', {class: 'btn', type: 'button'}, icon('link'), 'Kopieren');
  copy.addEventListener('click', async () => toast(await copyText(inv.link, input) ? 'Link kopiert' : 'Kopieren ging nicht – Link antippen und kopieren', false));
  const btns = [copy];
  if (navigator.share) {
    const share = h('button', {class: 'btn primary', type: 'button'}, icon('share'), 'Teilen');
    share.addEventListener('click', async () => {
      try {
        await navigator.share({
          title: 'Einladung zu FragDesk',
          text: 'Deine Einladung für meinen FragDesk-Server. In FragDesk am PC: Einstellungen › Handy-App › Eigener Server › „Einladung einfügen“ (vorher den Link kopieren).',
          url: inv.link,
        });
      } catch (e) {
        if (e.name !== 'AbortError') toast('Teilen ging nicht: ' + e.message, true);
      }
    });
    btns.unshift(share);
  }
  return h('div', {class: 'invite-new'},
    h('div', {class: 'sub', style: {fontSize: '13px', margin: '14px 0 6px'}},
      (inv.admin ? 'Admin-Einladung' : 'Einladung') + ' – gilt bis ' + fmtDay(inv.expires)),
    input, h('div', {class: 'btnrow'}, btns));
}

// Ein Konto: Zahlen, Speicherlimit, entfernen
function userSheet(ctx, u, self) {
  const pick = LIMITS.findIndex(gb => gb * GiB === u.limit);
  const limits = chips(LIMITS.map(gb => gb ? gb + ' GB' : 'Unbegrenzt'), pick, async i => {
    const limit = LIMITS[i] * GiB;
    if (limit && u.used > limit && !confirm(`${u.name} belegt schon ${fmtBytes(u.used)}. Mit ${fmtBytes(limit)} lädt FragDesk nichts Neues mehr hoch, bis Platz ist. Trotzdem?`)) return;
    try {
      await api.put('/api/v1/admin/users/' + encodeURIComponent(u.id), {limit});
      toast(`Limit für ${u.name}: ${limit ? fmtBytes(limit) : 'unbegrenzt'}`);
      closeSheet();
      ctx.rerender();
    } catch (e) { toast(e.message, true); }
  });
  limits.style.cssText = 'margin: 4px 0 14px; padding: 0; flex-wrap: wrap; overflow: visible';   // alle sechs auf einen Blick
  const body = [h('div', {class: 'section', style: {margin: '16px 0 8px'}}, 'Speicherlimit'), limits,
    pick < 0 && u.limit ? h('div', {class: 'muted', style: {fontSize: '12.5px', margin: '-6px 0 12px'}}, 'Zurzeit: ' + fmtBytes(u.limit)) : null];
  if (!self) {
    const remove = h('button', {class: 'btn danger block', type: 'button'}, icon('close'), 'Konto entfernen');
    remove.addEventListener('click', async () => {
      if (!confirm(`Konto „${u.name}“ mit allen Matches und Videos vom Server löschen? Das lässt sich nicht rückgängig machen.`)) return;
      try {
        await api.del('/api/v1/admin/users/' + encodeURIComponent(u.id));
        toast(`Konto „${u.name}“ entfernt`);
        closeSheet();
        ctx.rerender();
      } catch (e) { toast(e.message, true); }
    });
    body.push(remove);
  }
  sheet({
    title: u.name + (u.admin ? ' (Admin)' : ''),
    rows: [
      ['Speicher', u.limit ? `${fmtBytes(u.used)} von ${fmtBytes(u.limit)}` : `${fmtBytes(u.used)} · unbegrenzt`],
      ['Matches', String(u.matches)],
      ['Videos', String(u.clips)],
      ['Handys', String(u.devices ?? '–')],
      ['Dabei seit', fmtDay(u.created)],
      ['Zuletzt gesehen', fmtDay(u.lastSeen)],
    ],
    body: h('div', null, body),
  });
}
