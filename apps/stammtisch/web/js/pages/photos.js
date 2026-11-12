// STAMMTISCH – Bilder: ein Album je Treffen, hochladen, ansehen, teilen, verschieben, löschen
import {h, fill, icon, card, button, emptyState, skeleton, sheet, closeSheet, actionSheet, confirmSheet, toast, spinner} from '../ui.js';
import {eventTitle, fmtDate, fmtStamp, fmtDuration, guessAlbumEvent, EVENT_GRACE_MS} from '../util.js';
import {prepareFile, uploadPrepared} from '../media.js';

const NONE = 'ohne';   // Album für Bilder ohne Treffen

// Vorschaubild als Kachel (für Raster und Streifen)
export function tile(ctx, m, onclick) {
  const img = h('img', {alt: m.kind === 'video' ? 'Video' : 'Bild', crossorigin: 'anonymous', loading: 'lazy'});
  ctx.url(m.thumb_path || (m.kind === 'image' ? m.path : null)).then(u => { if (u) img.src = u; });
  return h('button', {class: 'tile', type: 'button', onclick},
    img,
    m.kind === 'video' ? h('span', {class: 'playbadge'}, icon('play')) : null,
    m.kind === 'video' && m.duration ? h('span', {class: 'len'}, fmtDuration(m.duration)) : null);
}

function albumName(e) {
  return e ? (e.place_name || eventTitle(e)) : 'Ohne Treffen';
}

async function loadEvents(ctx) {
  const all = await ctx.cached('album-events', async () => {
    const cut = new Date(Date.now() - EVENT_GRACE_MS);
    const [upcoming, past] = await Promise.all([ctx.be.events(ctx.group.id, cut), ctx.be.pastEvents(ctx.group.id, cut, 200)]);
    return [...upcoming, ...past];
  });
  return all;
}

// ---- Übersicht: alle Alben -------------------------------------------------------------------------------------------
export async function render(ctx) {
  ctx.title('Bilder');
  const uploadBtn = h('button', {class: 'iconbtn accent', type: 'button', 'aria-label': 'Bilder hochladen'}, icon('plus'));
  const head = h('div', {class: 'page-head'},
    h('div', null, h('h1', {class: 'page-title'}, 'Bilder'), h('div', {class: 'page-sub'}, ctx.group.name)), uploadBtn);
  fill(ctx.main, head, ...skeleton(180, 180));

  const [media, events] = await Promise.all([ctx.cached('media', () => ctx.be.media(ctx.group.id)), loadEvents(ctx)]);
  if (!ctx.current()) return;
  uploadBtn.addEventListener('click', () => pickAndUpload(ctx, events, undefined, () => ctx.rerender()));

  // Bilder nach Treffen sortieren; Bilder gelöschter Treffen zählen zu „Ohne Treffen“
  const byEvent = new Map(events.map(e => [e.id, e]));
  const albums = new Map();
  for (const m of media) {
    const key = m.event_id && byEvent.has(m.event_id) ? m.event_id : NONE;
    if (!albums.has(key)) albums.set(key, []);
    albums.get(key).push(m);
  }
  const keys = [...albums.keys()].sort((a, b) => {
    if (a === NONE) return 1;
    if (b === NONE) return -1;
    return new Date(byEvent.get(b).starts_at) - new Date(byEvent.get(a).starts_at);
  });

  if (!keys.length) {
    fill(ctx.main, head, card(null, emptyState({
      title: 'Noch keine Bilder',
      text: 'Lade Fotos und kurze Videos vom Stammtisch hoch. Sie landen im Album des passenden Abends.',
      action: button('Bilder hochladen', {primary: true, icon: 'plus', onclick: () => pickAndUpload(ctx, events, undefined, () => ctx.rerender())}),
    }, 'image')));
    return;
  }

  const grid = h('div', {class: 'albums'});
  for (const key of keys) {
    const list = albums.get(key);
    const e = byEvent.get(key);
    const covers = list.filter(m => m.thumb_path || m.kind === 'image').slice(0, 4);
    const cover = h('div', {class: 'cover n' + Math.max(1, covers.length)});
    if (!covers.length) cover.append(icon('video'));
    for (const m of covers) {
      const img = h('img', {alt: '', crossorigin: 'anonymous', loading: 'lazy'});
      ctx.url(m.thumb_path || m.path).then(u => { if (u) img.src = u; });
      cover.append(img);
    }
    grid.append(h('a', {class: 'album', href: '#/bilder/' + key}, cover,
      h('b', null, albumName(e)),
      h('small', null, (e ? fmtDate(e.starts_at) + ' · ' : '') + (list.length === 1 ? '1 Bild' : list.length + ' Bilder'))));
  }
  fill(ctx.main, head, grid);

  let timer = 0;
  ctx.onLive(ev => {
    if (ev.table !== 'media' || !ev.row || ev.row.scope !== 'album') return;
    clearTimeout(timer);
    timer = setTimeout(() => { if (ctx.current() && !document.querySelector('.sheet, .viewer')) ctx.rerender(); }, 1500);
  });
  ctx.onLeave(() => clearTimeout(timer));
}

// ---- Ein Album -------------------------------------------------------------------------------------------------------
export async function renderAlbum(ctx) {
  const key = ctx.params[0];
  const eventId = key === NONE ? null : key;
  fill(ctx.main, ...skeleton(60, 300));
  const [media, events, event] = await Promise.all([
    ctx.cached('album.' + key, () => ctx.be.media(ctx.group.id, {eventId})),
    loadEvents(ctx),
    eventId ? ctx.be.event(eventId).catch(() => null) : null,
  ]);
  if (!ctx.current()) return;
  // „Ohne Treffen“ zeigt auch Bilder, deren Treffen gelöscht wurde
  let list = media;
  if (!eventId) {
    const live = new Set(events.map(e => e.id));
    const all = await ctx.be.media(ctx.group.id).catch(() => media);
    if (!ctx.current()) return;
    list = all.filter(m => !m.event_id || !live.has(m.event_id));
  }

  const name = albumName(event);
  ctx.title(name);
  const refresh = () => ctx.rerender();
  const uploadBtn = h('button', {class: 'iconbtn accent', type: 'button', 'aria-label': 'Bilder hochladen',
    onclick: () => pickAndUpload(ctx, events, eventId, refresh)}, icon('plus'));
  const head = h('div', {class: 'page-head'},
    h('div', null, h('h1', {class: 'page-title'}, name),
      event ? h('a', {class: 'page-sub', href: '#/treffen/' + event.id}, fmtDate(event.starts_at) + ' · ' + eventTitle(event), icon('chev'))
            : h('div', {class: 'page-sub'}, 'Bilder, die zu keinem Treffen gehören')),
    uploadBtn);

  if (!list.length) {
    fill(ctx.main, head, card(null, emptyState({
      title: 'Noch keine Bilder in diesem Album',
      action: button('Bilder hochladen', {primary: true, icon: 'plus', onclick: () => pickAndUpload(ctx, events, eventId, refresh)}),
    }, 'image')));
    return;
  }
  const grid = h('div', {class: 'grid'});
  list.forEach((m, i) => grid.append(tile(ctx, m, () => openViewer(ctx, list, i, {events, onChange: refresh}))));
  fill(ctx.main, head, grid,
    h('div', {class: 'about'}, list.length === 1 ? '1 Bild' : list.length + ' Bilder und Videos'));

  let timer = 0;
  ctx.onLive(ev => {
    if (ev.table !== 'media' || !ev.row || ev.row.scope !== 'album') return;
    clearTimeout(timer);
    timer = setTimeout(() => { if (ctx.current() && !document.querySelector('.sheet, .viewer')) ctx.rerender(); }, 1500);
  });
  ctx.onLeave(() => clearTimeout(timer));
}

// ---- Hochladen -------------------------------------------------------------------------------------------------------
// eventId: undefined = erst fragen, zu welchem Treffen; null = ohne Treffen; sonst dieses Album
function pickAndUpload(ctx, events, eventId, done) {
  const input = h('input', {type: 'file', accept: 'image/*,video/*', multiple: true, class: 'hidden'});
  document.body.append(input);
  input.addEventListener('change', () => {
    const files = [...input.files];
    input.remove();
    if (!files.length) return;
    if (eventId !== undefined) return runUpload(ctx, files, eventId, done);
    chooseAlbum(ctx, events, files, done);
  });
  input.click();
}

function chooseAlbum(ctx, events, files, done) {
  const guess = guessAlbumEvent(events);
  const past = events.filter(e => new Date(e.starts_at).getTime() < Date.now() + 2 * 3600e3 && !e.canceled)
    .sort((a, b) => new Date(b.starts_at) - new Date(a.starts_at)).slice(0, 8);
  const select = h('select', null,
    past.map(e => h('option', {value: e.id, selected: guess && e.id === guess.id ? true : null}, `${fmtDate(e.starts_at)} · ${albumName(e)}`)),
    h('option', {value: NONE, selected: guess ? null : true}, 'Ohne Treffen'));
  const go = button(files.length === 1 ? 'Hochladen' : `${files.length} Dateien hochladen`, {primary: true, block: true, onclick: () => {
    const value = select.value;
    closeSheet();
    runUpload(ctx, files, value === NONE ? null : value, done);
  }});
  sheet({title: 'Zu welchem Treffen?', text: 'Die Bilder landen im Album dieses Abends. Das lässt sich später noch ändern.',
    body: h('div', {class: 'stack'}, h('label', {class: 'field'}, h('span', null, 'Album'), select), go)});
}

// Dateien nacheinander vorbereiten und hochladen – mit Fortschritt je Datei. Bilder werden vorher verkleinert.
async function runUpload(ctx, files, eventId, done) {
  const rows = files.map(f => {
    const bar = h('i');
    const state = h('span', {class: 'muted'}, 'wartet');
    return {f, bar, state, el: h('div', {class: 'upload'}, h('span', {class: 'name'}, f.name || 'Datei'), state, h('span', {class: 'track'}, bar))};
  });
  const closeBtn = button('Im Hintergrund weiter', {block: true, onclick: () => closeSheet()});
  sheet({title: files.length === 1 ? 'Wird hochgeladen' : `${files.length} Dateien werden hochgeladen`,
    body: [h('div', {class: 'uploads'}, rows.map(r => r.el)), closeBtn]});
  let ok = 0, failed = 0;
  for (const r of rows) {
    try {
      r.state.textContent = 'wird vorbereitet';
      const prepared = await prepareFile(r.f);
      r.state.textContent = '';
      await uploadPrepared(ctx.be, ctx.group.id, prepared, {scope: 'album', eventId, onProgress: p => { r.bar.style.width = Math.round(p * 100) + '%'; }});
      r.bar.style.width = '100%';
      r.el.classList.add('done');
      ok++;
    } catch (e) {
      r.el.classList.add('fail');
      r.state.textContent = e.message;
      r.state.classList.remove('muted');
      failed++;
    }
  }
  closeBtn.lastChild.textContent = 'Fertig';
  if (!failed) closeSheet();
  toast(failed ? `${ok} hochgeladen, ${failed} fehlgeschlagen` : ok === 1 ? 'Hochgeladen' : `${ok} Dateien hochgeladen`, failed > 0);
  if (ok && done) done();
}

// ---- Großansicht -----------------------------------------------------------------------------------------------------
// list: Bilder/Videos, index: womit anfangen. opts.plain: ohne Verschieben/Löschen (Bilder aus dem Chat)
export function openViewer(ctx, list, index, opts = {}) {
  let i = index;
  let changed = false;
  const stage = h('div', {class: 'stage'});
  const title = h('div', {class: 'tt'});
  const viewer = h('div', {class: 'viewer', role: 'dialog', 'aria-modal': 'true'},
    h('div', {class: 'vbar'},
      h('button', {class: 'iconbtn', type: 'button', 'aria-label': 'Schließen', onclick: close}, icon('close')),
      title,
      h('button', {class: 'iconbtn', type: 'button', 'aria-label': 'Mehr', onclick: menu}, icon('more'))),
    stage);

  function show() {
    const m = list[i];
    const by = m.member_id ? ctx.member(m.member_id).name : '';
    title.replaceChildren(h('b', null, by || 'Bild'), h('small', null, fmtStamp(m.created_at) + (list.length > 1 ? ` · ${i + 1} von ${list.length}` : '')));
    const wait = spinner();
    stage.replaceChildren(wait);
    ctx.url(m.path).then(u => {
      if (list[i] !== m || !viewer.isConnected) return;
      if (!u) { stage.replaceChildren(h('div', {class: 'muted'}, 'Lässt sich gerade nicht laden.')); return; }
      const el = m.kind === 'video'
        ? h('video', {src: u, controls: true, playsinline: true, autoplay: true, crossorigin: 'anonymous'})
        : h('img', {src: u, alt: '', crossorigin: 'anonymous'});
      el.addEventListener(m.kind === 'video' ? 'loadeddata' : 'load', () => wait.remove());
      el.addEventListener('error', () => { if (el.isConnected) stage.replaceChildren(h('div', {class: 'muted', style: {padding: '0 30px', textAlign: 'center'}},
        m.kind === 'video' ? 'Dieses Handy kann das Video nicht abspielen. Über „Mehr“ lässt es sich speichern.' : 'Lässt sich gerade nicht laden.')); });
      stage.append(el);
      if (m.kind !== 'video' && list.length > 1) {
        stage.append(h('button', {class: 'nav prev', type: 'button', 'aria-label': 'Vorheriges', onclick: () => step(-1)}),
          h('button', {class: 'nav next', type: 'button', 'aria-label': 'Nächstes', onclick: () => step(1)}));
      }
    });
  }
  function step(d) {
    const n = i + d;
    if (n < 0 || n >= list.length) return;
    i = n;
    show();
  }
  function close() {
    viewer.remove();
    document.removeEventListener('keydown', onKey);
    if (changed && opts.onChange) opts.onChange();
  }
  const onKey = ev => {
    if (ev.key === 'Escape') close();
    else if (ev.key === 'ArrowLeft') step(-1);
    else if (ev.key === 'ArrowRight') step(1);
  };

  // Wischen: links/rechts blättert, nach unten schließt
  let x0 = null, y0 = null;
  stage.addEventListener('touchstart', ev => { if (ev.touches.length === 1) { x0 = ev.touches[0].clientX; y0 = ev.touches[0].clientY; } }, {passive: true});
  stage.addEventListener('touchend', ev => {
    if (x0 == null) return;
    const dx = ev.changedTouches[0].clientX - x0, dy = ev.changedTouches[0].clientY - y0;
    x0 = null;
    if (Math.abs(dx) > 60 && Math.abs(dx) > Math.abs(dy) * 1.5) step(dx < 0 ? 1 : -1);
    else if (dy > 110 && Math.abs(dy) > Math.abs(dx) * 1.5) close();
  }, {passive: true});

  async function save(m) {
    try {
      const u = await ctx.url(m.path);
      const blob = await (await fetch(u)).blob();
      const name = `stammtisch-${m.created_at.slice(0, 10)}.${(m.path.split('.').pop() || 'jpg')}`;
      const f = new File([blob], name, {type: blob.type || m.mime || 'image/jpeg'});
      if (navigator.canShare && navigator.canShare({files: [f]})) { await navigator.share({files: [f]}); return; }
      const a = h('a', {href: URL.createObjectURL(blob), download: name});
      document.body.append(a);
      a.click();
      a.remove();
    } catch (e) {
      if (e && e.name !== 'AbortError') toast('Speichern hat nicht geklappt.', true);
    }
  }

  function menu() {
    const m = list[i];
    const events = opts.events || [];
    actionSheet(null, [
      {label: 'Speichern oder teilen', icon: 'share', run: () => save(m)},
      !opts.plain && events.length ? {label: 'In anderes Album verschieben', icon: 'image', run: () => move(m, events)} : null,
      !opts.plain ? {label: 'Löschen', icon: 'trash', danger: true, run: () => remove(m)} : null,
    ]);
  }

  function move(m, events) {
    const past = events.filter(e => new Date(e.starts_at).getTime() < Date.now() + 2 * 3600e3)
      .sort((a, b) => new Date(b.starts_at) - new Date(a.starts_at)).slice(0, 12);
    actionSheet('Verschieben nach', [
      ...past.filter(e => e.id !== m.event_id).map(e => ({label: `${fmtDate(e.starts_at)} · ${albumName(e)}`, icon: 'calendar', run: () => apply(e.id)})),
      m.event_id ? {label: 'Ohne Treffen', icon: 'image', run: () => apply(null)} : null,
    ]);
    async function apply(eventId) {
      try {
        await ctx.be.patchMedia(m.id, {event_id: eventId});
        toast('Verschoben');
        dropCurrent();
      } catch (e) { toast(e.message, true); }
    }
  }

  async function remove(m) {
    if (!(await confirmSheet({title: m.kind === 'video' ? 'Video löschen?' : 'Bild löschen?', text: 'Es landet im Papierkorb und lässt sich 30 Tage lang wiederherstellen.', ok: 'Löschen', danger: true}))) return;
    try {
      await ctx.be.patchMedia(m.id, {deleted_at: new Date().toISOString()});
      toast('In den Papierkorb gelegt');
      dropCurrent();
    } catch (e) { toast(e.message, true); }
  }

  // Das gezeigte Bild gehört nicht mehr in diese Liste: zum nächsten springen oder schließen
  function dropCurrent() {
    changed = true;
    list.splice(i, 1);
    if (!list.length) return close();
    if (i >= list.length) i = list.length - 1;
    show();
  }

  document.addEventListener('keydown', onKey);
  document.body.append(viewer);
  show();
}
