// FragDesk Handy-App – Video abspielen und aufs Handy speichern
import {h, icon, toast} from '../ui.js';

let current = null;

export async function openPlayer(clip) {
  closePlayer();
  const url = '/media/clip/' + encodeURIComponent(clip.id);
  const video = h('video', {controls: true, playsinline: true, 'webkit-playsinline': true, preload: 'metadata', poster: '/media/thumb/' + encodeURIComponent(clip.id) + '.jpg'});
  const hint = h('div', {class: 'hint'});
  const saveBtn = h('button', {class: 'btn primary', type: 'button'}, icon('save'), 'Aufs Handy speichern');
  const close = h('button', {class: 'iconbtn', type: 'button', 'aria-label': 'Schließen'}, icon('close'));
  const toMatch = clip.match ? h('a', {class: 'btn', href: '#/match/' + clip.match + '/6'}, 'Zum Match') : null;
  const box = h('div', {class: 'player', role: 'dialog', 'aria-modal': 'true'},
    h('div', {class: 'bar'}, close, h('div', {class: 'tt'}, clip.title || 'Video'), h('span', {class: 'iconbtn spacer'})),
    video,
    h('div', {class: 'actions'}, saveBtn, toMatch, hint));
  close.addEventListener('click', closePlayer);
  if (toMatch) toMatch.addEventListener('click', closePlayer);
  document.body.append(box);
  document.body.style.overflow = 'hidden';
  current = {box, video};
  window.addEventListener('hashchange', closePlayer, {once: true});

  // Erst kurz fragen, ob das Video gerade geht (läuft CS2, liefert FragDesk keine Videos)
  try {
    const r = await fetch(url, {headers: {Range: 'bytes=0-0'}, credentials: 'same-origin', cache: 'no-store'});
    if (r.status === 503 || r.status === 404 || r.status === 401) {
      let msg = 'Das Video ist gerade nicht verfügbar.';
      try { msg = (await r.json()).error || msg; } catch (e) { /* leer */ }
      hint.textContent = msg;
      saveBtn.disabled = true;
      return;
    }
  } catch (e) {
    hint.textContent = 'Keine Verbindung zu FragDesk.';
    saveBtn.disabled = true;
    return;
  }
  if (!current || current.box !== box) return;
  video.src = url;
  video.play().catch(() => {});

  // Speichern: mit HTTPS über „Teilen“ (dort „Video sichern“ → Fotos), sonst als Download in die Dateien-App
  let file = null;
  saveBtn.addEventListener('click', async () => {
    const name = (clip.title || 'FragDesk').replace(/[\\/:*?"<>|]/g, '') + '.mp4';
    if (file) {   // zweiter Tipp: Teilen direkt im Tippen aufrufen (Safari verlangt das)
      try {
        await navigator.share({files: [file], title: clip.title});
      } catch (e) {
        if (e.name !== 'AbortError') toast('Teilen ging nicht: ' + e.message, true);
      }
      return;
    }
    if (window.isSecureContext && navigator.canShare && navigator.canShare({files: [new File([''], 'x.mp4', {type: 'video/mp4'})]})) {
      saveBtn.disabled = true;
      try {
        const res = await fetch(url, {credentials: 'same-origin'});
        if (!res.ok) throw new Error('Fehler ' + res.status);
        const total = Number(res.headers.get('Content-Length')) || 0;
        const reader = res.body.getReader();
        const parts = [];
        let got = 0;
        for (;;) {
          const {done, value} = await reader.read();
          if (done) break;
          parts.push(value);
          got += value.length;
          saveBtn.lastChild.textContent = total ? `Lade … ${Math.round(got / total * 100)} %` : 'Lade …';
        }
        file = new File(parts, name, {type: 'video/mp4'});
        saveBtn.lastChild.textContent = 'Jetzt sichern';
        hint.textContent = 'Tippe auf „Jetzt sichern“ und dann auf „Video sichern“ – das Video landet in Fotos.';
      } catch (e) {
        saveBtn.lastChild.textContent = 'Aufs Handy speichern';
        toast('Laden ging nicht: ' + e.message, true);
      } finally {
        saveBtn.disabled = false;
      }
      return;
    }
    const a = h('a', {href: url + '?dl=1', download: name});
    document.body.append(a);
    a.click();
    a.remove();
    hint.textContent = 'Das Video landet in der Dateien-App unter „Downloads“. Dort: Teilen › „Video sichern“ – dann ist es in Fotos.';
  });
}

export function closePlayer() {
  if (!current) return;
  current.video.pause();
  current.video.removeAttribute('src');
  current.video.load();
  current.box.remove();
  current = null;
  document.body.style.overflow = '';
}
