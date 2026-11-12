// STAMMTISCH – Verlauf (wer hat wann was geändert, mit „Rückgängig“) und Papierkorb
import {h, fill, icon, card, row, button, avatar, emptyState, skeleton, actionSheet, toast, busy} from '../ui.js';
import {changeVerb, changeSubject, changedFields, canUndo, groupChanges, fmtStamp, fmtDate, fmtTime, eventTitle, dayDiff, PLACE_KINDS} from '../util.js';
import {tile} from './photos.js';
import {KIND_ICON} from './events.js';

const PAGE = 60;

export async function render(ctx) {
  ctx.title('Verlauf');
  fill(ctx.main, ...skeleton(300));
  let changes = await ctx.be.changelog(ctx.group.id, {limit: PAGE});
  if (!ctx.current()) return;
  let hasMore = changes.length >= PAGE;

  function entry(group) {
    const c = group.first;
    const n = group.items.length;
    const who = c.member_id ? ctx.member(c.member_id) : {name: 'Jemand'};
    let verb = changeVerb(c);
    if (n > 1 && c.entity === 'media') {      // mehrere Bilder am Stück: ein Eintrag
      const what = c.action === 'create' ? 'hochgeladen' : c.action === 'delete' ? 'gelöscht' : c.action === 'restore' ? 'wiederhergestellt' : 'verschoben';
      verb = `hat ${n} Bilder ${what}`;
    }
    const fields = n === 1 && c.action === 'update' ? changedFields(c) : [];
    const subject = c.entity === 'media' || c.entity === 'invite' || c.entity === 'device' || (c.entity === 'member') ? '' : changeSubject(c);
    const undone = group.items.every(x => x.undone_at);
    const undoable = group.items.filter(x => canUndo(x, ctx.isAdmin));
    const undoBtn = undoable.length ? button('Rückgängig', {small: true, icon: 'undo', onclick: ev => busy(ev.currentTarget, async () => {
      for (const x of undoable) await ctx.be.undo(x.id);
      toast('Rückgängig gemacht');
      changes = await ctx.be.changelog(ctx.group.id, {limit: Math.max(PAGE, changes.length + 5)});
      if (ctx.current()) draw();
    })}) : null;
    return h('div', {class: 'change' + (undone ? ' undone' : '')},
      avatar(who, ctx.url, 34),
      h('div', {class: 'body'},
        h('div', null, h('b', null, who.name), ' ', verb),
        subject ? h('span', {class: 'subject'}, subject) : null,
        fields.length ? h('dl', {class: 'diff'}, fields.map(f => [h('dt', null, f.label), h('dd', null, h('s', null, f.from), ' → ', f.to)])) : null,
        h('small', null, fmtStamp(c.at), c.undo_of ? ' · war ein Rückgängig' : '', undone ? ' · rückgängig gemacht' : ''),
        undoBtn));
  }

  function draw() {
    if (!changes.length) {
      fill(ctx.main, emptyState({title: 'Noch nichts passiert', text: 'Sobald jemand ein Treffen anlegt, ändert oder Bilder hochlädt, steht es hier – und lässt sich rückgängig machen.'}, 'history'));
      return;
    }
    const more = hasMore ? button('Ältere Einträge laden', {block: true, onclick: ev => busy(ev.currentTarget, async () => {
      const older = await ctx.be.changelog(ctx.group.id, {before: changes[changes.length - 1].id, limit: PAGE});
      hasMore = older.length >= PAGE;
      changes = [...changes, ...older];
      draw();
    })}) : null;
    fill(ctx.main,
      h('div', {class: 'page-head'}, h('div', null, h('h1', {class: 'page-title'}, 'Verlauf'),
        h('div', {class: 'page-sub'}, 'Jeder darf alles ändern – hier steht, wer was gemacht hat.'))),
      card({flush: true}, groupChanges(changes).map(entry)),
      more);
  }
  draw();
}

// ---- Papierkorb ------------------------------------------------------------------------------------------------------
export async function renderTrash(ctx) {
  ctx.title('Papierkorb');
  fill(ctx.main, ...skeleton(200));
  const trash = await ctx.be.trash(ctx.group.id);
  if (!ctx.current()) return;
  const total = trash.events.length + trash.places.length + trash.media.length;
  if (!total) {
    fill(ctx.main, emptyState({title: 'Der Papierkorb ist leer', text: 'Gelöschte Treffen, Orte und Bilder bleiben hier 30 Tage und lassen sich so lange wiederherstellen.'}, 'trash'));
    return;
  }
  const left = x => {
    const days = 30 + dayDiff(x.deleted_at);
    return days <= 0 ? 'wird beim nächsten Aufräumen entfernt' : days === 1 ? 'noch 1 Tag' : `noch ${days} Tage`;
  };
  const restore = (label, fn) => actionSheet(label, [{label: 'Wiederherstellen', icon: 'undo', run: async () => {
    try { await fn(); toast('Wiederhergestellt'); ctx.rerender(); } catch (e) { toast(e.message, true); }
  }}]);

  const out = [h('div', {class: 'page-head'}, h('div', null, h('h1', {class: 'page-title'}, 'Papierkorb'),
    h('div', {class: 'page-sub'}, 'Antippen zum Wiederherstellen. Nach 30 Tagen ist es endgültig weg.')))];
  if (trash.events.length) {
    out.push(h('div', {class: 'section'}, h('span', null, 'Treffen')));
    out.push(card({flush: true}, trash.events.map(e => row({icon: 'calendar', title: `${eventTitle(e)} · ${fmtDate(e.starts_at)}, ${fmtTime(e.starts_at)}`,
      sub: [e.place_name, left(e)].filter(Boolean).join(' · '), chev: false,
      onclick: () => restore(eventTitle(e), () => ctx.be.patchEvent(e.id, {deleted_at: null}))}))));
  }
  if (trash.places.length) {
    out.push(h('div', {class: 'section'}, h('span', null, 'Orte')));
    out.push(card({flush: true}, trash.places.map(p => row({icon: KIND_ICON[p.kind] || 'pin', title: p.name, sub: [PLACE_KINDS[p.kind], left(p)].join(' · '), chev: false,
      onclick: () => restore(p.name, () => ctx.be.patchPlace(p.id, {deleted_at: null}))}))));
  }
  if (trash.media.length) {
    out.push(h('div', {class: 'section'}, h('span', null, 'Bilder und Videos')));
    const grid = h('div', {class: 'grid'});
    for (const m of trash.media) grid.append(tile(ctx, m, () => restore(`${m.kind === 'video' ? 'Video' : 'Bild'} · ${left(m)}`, () => ctx.be.patchMedia(m.id, {deleted_at: null}))));
    out.push(grid);
  }
  fill(ctx.main, ...out);
}
