// STAMMTISCH – Chat der Gruppe: Text, Bilder, Videos, Sprachnachrichten, Antworten, Reaktionen
import {h, fill, icon, avatar, button, closeSheet, actionSheet, confirmSheet, toast, copyText, spinner, emptyState} from '../ui.js';
import {fmtTime, fmtChatDay, fmtDuration, dayDiff, linkify, colorFor} from '../util.js';
import {prepareFile, uploadPrepared, canRecord, startRecording, MAX_VOICE_SECONDS} from '../media.js';
import {openViewer} from './photos.js';

const EMOJIS = ['👍', '❤️', '😂', '🍻', '😮', '😢'];
const PAGE = 50;

export async function render(ctx) {
  const gid = ctx.group.id;
  ctx.title('Chat');
  fill(ctx.main, h('div', {class: 'page-head'}, h('div', null, h('h1', {class: 'page-title'}, 'Chat'), h('div', {class: 'page-sub'}, ctx.group.name))), spinner());

  let messages = await ctx.cached('chat', () => ctx.be.messages(gid, {limit: PAGE}));
  if (!ctx.current()) return;
  ctx.chatSeen();
  let hasOlder = messages.length >= PAGE;
  let replyTo = null, editing = null, recording = null, recTimer = 0;
  const cache = new Map();          // Nachricht → gezeichnetes Element (wird nur bei Änderung neu gebaut)
  const quoted = new Map();         // zitierte Nachrichten, die nicht (mehr) geladen sind
  let playing = null;               // laufende Sprachnachricht

  const list = h('div');
  const older = button('Ältere Nachrichten laden', {small: true, onclick: loadOlder});
  older.classList.add('older');
  const head = h('div', {class: 'page-head'}, h('div', null, h('h1', {class: 'page-title'}, 'Chat'), h('div', {class: 'page-sub'}, ctx.group.name)));

  // ---- Liste ----------------------------------------------------------------------------------------------------------
  const nearBottom = () => window.innerHeight + window.scrollY >= document.documentElement.scrollHeight - 160;
  // ?noscroll: für Bilder der Oberfläche (tools/render.sh) – Chrome ohne Fenster zeichnet gescrollte Seiten falsch
  const noScroll = new URLSearchParams(location.search).has('noscroll');
  const toBottom = () => { if (!noScroll) window.scrollTo(0, document.documentElement.scrollHeight); };

  function drawList() {
    const nodes = [];
    let prev = null;
    for (const m of messages) {
      const newDay = !prev || dayDiff(m.created_at, new Date(prev.created_at)) !== 0;
      if (newDay) nodes.push(h('div', {class: 'chat-day'}, h('span', null, fmtChatDay(m.created_at))));
      const first = newDay || prev.member_id !== m.member_id || new Date(m.created_at) - new Date(prev.created_at) > 5 * 60000;
      const q = m.reply_to ? findMessage(m.reply_to) : null;
      const sig = JSON.stringify([m.body, m.edited_at, m.deleted_at, m.reactions, first, m.media && m.media.id, q && [q.body, q.deleted_at]]);
      let entry = cache.get(m.id);
      if (!entry || entry.sig !== sig) {
        entry = {sig, el: messageEl(m, first)};
        cache.set(m.id, entry);
      }
      nodes.push(entry.el);
      prev = m;
    }
    if (!messages.length) nodes.push(emptyState({title: 'Noch keine Nachrichten', text: 'Schreib die erste – zum Beispiel, wohin es am Donnerstag geht.'}, 'chat'));
    list.replaceChildren(...nodes);
    older.classList.toggle('hidden', !hasOlder);
  }

  function findMessage(id) {
    const hit = messages.find(x => x.id === id) || quoted.get(id);
    if (hit) return hit;
    if (!quoted.has(id)) {            // ältere Nachricht nachladen, dann neu zeichnen
      quoted.set(id, null);
      ctx.be.message(id).then(m => { if (m && ctx.current()) { quoted.set(id, m); drawList(); } }).catch(() => {});
    }
    return null;
  }

  function preview(m) {
    if (!m) return 'Nachricht';
    if (m.deleted_at) return 'Gelöschte Nachricht';
    if (m.body) return m.body;
    const kind = m.media && m.media.kind;
    return kind === 'audio' ? 'Sprachnachricht' : kind === 'video' ? 'Video' : kind === 'image' ? 'Bild' : 'Nachricht';
  }

  function messageEl(m, first) {
    const own = m.member_id === ctx.me.id;
    const author = ctx.member(m.member_id);
    const bubble = h('div', {class: 'bubble'});
    if (m.deleted_at) {
      bubble.classList.add('gone');
      bubble.append('Nachricht gelöscht');
    } else {
      if (!own && first) bubble.append(h('span', {class: 'who-name', style: {color: colorFor(author.id)}}, author.name));
      if (m.reply_to) {
        const q = findMessage(m.reply_to);
        bubble.append(h('div', {class: 'quote'}, h('b', null, q ? ctx.member(q.member_id).name : ''), h('span', null, preview(q))));
      }
      if (m.media && !m.media.deleted_at) {
        bubble.append(mediaEl(m));
        if (m.media.kind !== 'audio') bubble.classList.add('media');
      }
      if (m.body) {
        bubble.append(h('div', {class: 'text'}, linkify(m.body).map(part =>
          part.href ? h('a', {href: part.href, target: '_blank', rel: 'noopener noreferrer'}, part.text) : part.text)));
      }
      bubble.append(h('span', {class: 'meta'}, (m.edited_at ? 'bearbeitet · ' : '') + fmtTime(m.created_at)));
      bubble.addEventListener('click', ev => { if (!ev.target.closest('a, button')) messageMenu(m); });
    }
    const col = h('div', {class: 'col'}, bubble, reactionsEl(m));
    return h('div', {class: 'msg' + (own ? ' own' : '') + (first ? ' first' : '')},
      own ? null : h('div', {class: 'avatar-slot'}, first ? avatar(author, ctx.url, 30) : null), col);
  }

  function mediaEl(m) {
    const media = m.media;
    if (media.kind === 'audio') return voiceEl(media);
    const img = h('img', {alt: media.kind === 'video' ? 'Video' : 'Bild', crossorigin: 'anonymous', loading: 'lazy'});
    if (media.width && media.height) img.style.aspectRatio = media.width + ' / ' + media.height;
    ctx.url(media.thumb_path || (media.kind === 'image' ? media.path : null)).then(u => { if (u) img.src = u; });
    return h('button', {class: 'shot', type: 'button', onclick: () => openViewer(ctx, [media], 0, {plain: true})},
      img, media.kind === 'video' ? h('span', {class: 'playbadge'}, icon('play')) : null);
  }

  // Sprachnachricht: lädt erst beim Abspielen
  function voiceEl(media) {
    const btn = h('button', {type: 'button', 'aria-label': 'Abspielen'}, icon('play'));
    const fill = h('i');
    const time = h('small', null, fmtDuration(media.duration));
    let audio = null;
    const show = on => btn.replaceChildren(icon(on ? 'pause' : 'play'));
    btn.addEventListener('click', async () => {
      if (audio && !audio.paused) { audio.pause(); return; }
      if (!audio) {
        const u = await ctx.url(media.path);
        if (!u) { toast('Die Sprachnachricht lässt sich gerade nicht laden.', true); return; }
        audio = new Audio(u);
        audio.addEventListener('timeupdate', () => {
          const total = isFinite(audio.duration) && audio.duration ? audio.duration : media.duration || 1;
          fill.style.width = Math.min(100, (audio.currentTime / total) * 100) + '%';
          time.textContent = fmtDuration(audio.currentTime);
        });
        audio.addEventListener('pause', () => show(false));
        audio.addEventListener('play', () => show(true));
        audio.addEventListener('ended', () => { fill.style.width = '0'; time.textContent = fmtDuration(media.duration); });
        audio.addEventListener('error', () => toast('Dieses Handy kann die Sprachnachricht nicht abspielen.', true));
      }
      if (playing && playing !== audio) playing.pause();
      playing = audio;
      audio.play().catch(() => {});
    });
    return h('div', {class: 'voice'}, btn, h('div', {class: 'wave'}, fill), time);
  }

  function reactionsEl(m) {
    const counts = new Map();
    for (const r of m.reactions || []) {
      if (!r.active) continue;
      const c = counts.get(r.emoji) || {n: 0, mine: false};
      c.n++;
      if (r.member_id === ctx.me.id) c.mine = true;
      counts.set(r.emoji, c);
    }
    if (!counts.size || m.deleted_at) return null;
    return h('div', {class: 'reacts'}, [...counts].map(([emoji, c]) =>
      h('button', {type: 'button', class: c.mine ? 'mine' : '', onclick: () => react(m, emoji, !c.mine)}, emoji, c.n > 1 ? h('small', null, c.n) : null)));
  }

  async function react(m, emoji, on) {
    const mine = (m.reactions || []).find(r => r.member_id === ctx.me.id && r.emoji === emoji);
    if (mine) mine.active = on;
    else (m.reactions = m.reactions || []).push({member_id: ctx.me.id, emoji, active: on});
    drawList();
    try { await ctx.be.react(m.id, emoji, on); } catch (e) { toast(e.message, true); }
  }

  function messageMenu(m) {
    const own = m.member_id === ctx.me.id;
    const mineSet = new Set((m.reactions || []).filter(r => r.active && r.member_id === ctx.me.id).map(r => r.emoji));
    const emojiRow = h('div', {class: 'emoji-row'}, EMOJIS.map(e =>
      h('button', {type: 'button', class: mineSet.has(e) ? 'mine' : '', onclick: () => { closeSheet(); react(m, e, !mineSet.has(e)); }}, e)));
    actionSheet(null, [
      {label: 'Antworten', icon: 'reply', run: () => { replyTo = m; editing = null; drawContext(); input.focus(); }},
      m.body ? {label: 'Text kopieren', icon: 'copy', run: async () => toast((await copyText(m.body)) ? 'Kopiert' : 'Kopieren hat nicht geklappt')} : null,
      own && m.body ? {label: 'Bearbeiten', icon: 'edit', run: () => { editing = m; replyTo = null; input.value = m.body; grow(); drawContext(); input.focus(); }} : null,
      own || ctx.isAdmin ? {label: 'Löschen', icon: 'trash', danger: true, run: () => removeMessage(m)} : null,
    ], emojiRow);
  }

  async function removeMessage(m) {
    if (!(await confirmSheet({title: 'Nachricht löschen?', text: 'Alle sehen dann nur noch „Nachricht gelöscht“.', ok: 'Löschen', danger: true}))) return;
    try {
      await ctx.be.deleteMessage(m.id);
      m.deleted_at = new Date().toISOString();
      drawList();
    } catch (e) { toast(e.message, true); }
  }

  async function loadOlder() {
    if (!messages.length) return;
    older.disabled = true;
    try {
      const before = document.documentElement.scrollHeight;
      const more = await ctx.be.messages(gid, {before: messages[0].created_at, limit: PAGE});
      hasOlder = more.length >= PAGE;
      messages = [...more, ...messages];
      drawList();
      window.scrollTo(0, window.scrollY + document.documentElement.scrollHeight - before);   // Stelle halten
    } catch (e) { toast(e.message, true); }
    older.disabled = false;
  }

  // ---- Eingabe --------------------------------------------------------------------------------------------------------
  const input = h('textarea', {rows: 1, placeholder: 'Nachricht', maxlength: 4000, enterkeyhint: 'enter', 'aria-label': 'Nachricht'});
  const file = h('input', {type: 'file', accept: 'image/*,video/*', multiple: true, class: 'hidden'});
  const attach = h('button', {class: 'iconbtn', type: 'button', 'aria-label': 'Bild oder Video anhängen', onclick: () => file.click()}, icon('plus'));
  const action = h('button', {class: 'iconbtn accent', type: 'button'});
  const context = h('div', {class: 'context hidden'});
  const inner = h('div', {class: 'inner'});
  const composer = h('div', {class: 'composer'}, context, inner, file);

  function grow() {
    input.style.height = '40px';
    input.style.height = Math.min(120, input.scrollHeight + 2) + 'px';
    drawAction();
  }
  function drawAction() {
    const voice = !input.value.trim() && !editing && canRecord;
    action.replaceChildren(icon(voice ? 'mic' : 'send'));
    action.setAttribute('aria-label', voice ? 'Sprachnachricht aufnehmen' : 'Senden');
    action.onclick = voice ? record : send;
  }
  function drawContext() {
    const m = editing || replyTo;
    context.classList.toggle('hidden', !m);
    if (!m) return;
    context.replaceChildren(
      h('div', null, h('b', null, editing ? 'Nachricht bearbeiten' : 'Antwort an ' + ctx.member(m.member_id).name), h('span', null, preview(m))),
      h('button', {class: 'iconbtn', type: 'button', 'aria-label': 'Abbrechen', onclick: () => {
        if (editing) { input.value = ''; grow(); }
        editing = replyTo = null;
        drawContext();
        drawAction();
      }}, icon('close')));
  }
  function drawComposer() {
    if (recording) {
      const time = h('b', null, '0:00');
      clearInterval(recTimer);
      recTimer = setInterval(() => {
        if (!recording) return;
        time.textContent = fmtDuration(recording.seconds());
        if (recording.seconds() >= MAX_VOICE_SECONDS) finishRecording(true);
      }, 250);
      inner.replaceChildren(
        h('button', {class: 'iconbtn', type: 'button', 'aria-label': 'Aufnahme verwerfen', onclick: () => finishRecording(false)}, icon('trash')),
        h('div', {class: 'rec'}, h('i'), time, h('span', null, 'Aufnahme läuft …')),
        h('button', {class: 'iconbtn accent', type: 'button', 'aria-label': 'Sprachnachricht senden', onclick: () => finishRecording(true)}, icon('send')));
    } else {
      inner.replaceChildren(attach, input, action);
      drawAction();
    }
  }
  input.addEventListener('input', grow);

  function addMessage(m) {
    const i = messages.findIndex(x => x.id === m.id);
    if (i >= 0) messages[i] = {...messages[i], ...m, media: m.media || messages[i].media, reactions: m.reactions || messages[i].reactions};
    else messages.push(m);
  }

  async function send() {
    const body = input.value.trim();
    if (!body) return;
    const wasEditing = editing, wasReply = replyTo;
    input.value = '';
    editing = replyTo = null;
    grow();
    drawContext();
    try {
      if (wasEditing) {
        await ctx.be.editMessage(wasEditing.id, body);
        wasEditing.body = body;
        wasEditing.edited_at = new Date().toISOString();
      } else {
        addMessage(await ctx.be.sendMessage(gid, {body, reply_to: wasReply ? wasReply.id : null}));
      }
      drawList();
      if (!wasEditing) toBottom();
    } catch (e) {
      input.value = body;       // nichts verlieren
      editing = wasEditing;
      replyTo = wasReply;
      grow();
      drawContext();
      toast(e.message, true);
    }
  }

  // Bilder und Videos: verkleinern, hochladen, als Nachricht senden (der getippte Text wird zur ersten Bildunterschrift)
  file.addEventListener('change', async () => {
    const files = [...file.files];
    file.value = '';
    if (!files.length) return;
    let caption = input.value.trim();
    const reply = replyTo;
    input.value = '';
    replyTo = null;
    grow();
    drawContext();
    for (const [i, f] of files.entries()) {
      toast(files.length > 1 ? `Lade ${i + 1} von ${files.length} hoch …` : 'Wird hochgeladen …');
      try {
        const prepared = await prepareFile(f);
        const media = await uploadPrepared(ctx.be, gid, prepared, {scope: 'chat'});
        addMessage(await ctx.be.sendMessage(gid, {body: caption, media_id: media.id, reply_to: i === 0 && reply ? reply.id : null}));
        caption = '';
        drawList();
        toBottom();
      } catch (e) {
        toast(e.message, true);
        if (caption) { input.value = caption; grow(); }
        return;
      }
    }
    toast('Gesendet');
  });

  async function record() {
    try {
      recording = await startRecording();
      drawComposer();
    } catch (e) { toast(e.message, true); }
  }
  async function finishRecording(sendIt) {
    const rec = recording;
    if (!rec) return;
    recording = null;
    clearInterval(recTimer);
    drawComposer();
    if (!sendIt || rec.seconds() < 0.7) { rec.cancel(); return; }
    const reply = replyTo;
    replyTo = null;
    drawContext();
    try {
      toast('Sprachnachricht wird gesendet …');
      const prepared = await rec.stop();
      const media = await uploadPrepared(ctx.be, gid, prepared, {scope: 'chat'});
      addMessage(await ctx.be.sendMessage(gid, {body: '', media_id: media.id, reply_to: reply ? reply.id : null}));
      drawList();
      toBottom();
    } catch (e) { toast(e.message, true); }
  }

  // ---- Aufbau ---------------------------------------------------------------------------------------------------------
  fill(ctx.main, head, older, list);
  document.body.append(composer);
  drawComposer();
  drawList();
  requestAnimationFrame(toBottom);

  // Tastatur des Handys: Eingabezeile direkt darüber halten (iOS verschiebt feste Leisten sonst nicht mit)
  const vv = window.visualViewport;
  const place = () => {
    const keyboard = vv ? Math.max(0, window.innerHeight - vv.height - vv.offsetTop) : 0;
    composer.style.bottom = keyboard > 80 ? keyboard + 'px' : '';
  };
  if (vv) { vv.addEventListener('resize', place); vv.addEventListener('scroll', place); }

  ctx.onLeave(() => {
    composer.remove();
    clearInterval(recTimer);
    if (recording) recording.cancel();
    if (playing) playing.pause();
    if (vv) { vv.removeEventListener('resize', place); vv.removeEventListener('scroll', place); }
    ctx.chatSeen();
  });

  // ---- Live: Nachrichten und Reaktionen der anderen -------------------------------------------------------------------
  ctx.onLive(async ev => {
    if (ev.table === 'messages' && ev.row) {
      const known = messages.some(x => x.id === ev.row.id);
      const stick = nearBottom();
      let row = ev.row;
      if (!known && row.deleted_at) return;
      if (!known && row.media_id) {             // Anhang gehört dazu: einmal vollständig holen
        try { row = (await ctx.be.message(row.id)) || row; } catch (e) { /* dann eben ohne */ }
        if (!ctx.current()) return;
      }
      addMessage(known ? row : {reactions: [], media: null, ...row});
      drawList();
      if (!known && stick) toBottom();
      ctx.chatSeen();
    } else if (ev.table === 'reactions' && ev.row) {
      const m = messages.find(x => x.id === ev.row.message_id);
      if (!m) return;
      const r = (m.reactions || []).find(x => x.member_id === ev.row.member_id && x.emoji === ev.row.emoji);
      if (r) r.active = ev.row.active;
      else (m.reactions = m.reactions || []).push({member_id: ev.row.member_id, emoji: ev.row.emoji, active: ev.row.active});
      drawList();
    } else if (ev.table === 'members') {
      cache.clear();                            // Namen oder Bilder können sich geändert haben
      setTimeout(() => { if (ctx.current()) drawList(); }, 500);
    }
  });
}
