// STAMMTISCH – Bilder verkleinern, Videos prüfen, Sprachnachrichten aufnehmen, hochladen.
//
// Der Speicher ist knapp (kostenlos: 1 GB für alle). Deshalb werden Bilder vor dem Hochladen auf dem Handy verkleinert
// (längere Seite höchstens 1600 Punkte, JPEG) – aus 4 MB werden so rund 0,3 MB. Videos gehen unverändert hoch,
// höchstens 50 MB. Zu jedem Bild und Video gibt es ein kleines Vorschaubild, damit Übersichten wenig Daten brauchen.
import {config} from '../config.js';
import {fitSize, extFor, mediaPath, uuid, fmtBytes} from './util.js';

const IMAGE_MAX = 1600, THUMB_MAX = 400;

function loadImage(file) {
  return new Promise((resolve, reject) => {
    const url = URL.createObjectURL(file);
    const img = new Image();
    img.onload = () => { URL.revokeObjectURL(url); resolve(img); };
    img.onerror = () => { URL.revokeObjectURL(url); reject(new Error('Dieses Bild lässt sich nicht lesen.')); };
    img.src = url;
  });
}

function toJpeg(source, width, height, max, quality) {
  const size = fitSize(width, height, max);
  const canvas = document.createElement('canvas');
  canvas.width = size.width;
  canvas.height = size.height;
  const ctx = canvas.getContext('2d');
  ctx.fillStyle = '#fff';                     // durchsichtige Stellen (PNG) werden weiß statt schwarz
  ctx.fillRect(0, 0, size.width, size.height);
  ctx.drawImage(source, 0, 0, size.width, size.height);
  return new Promise((resolve, reject) => {
    canvas.toBlob(blob => (blob ? resolve({blob, ...size}) : reject(new Error('Das Bild ließ sich nicht umwandeln.'))), 'image/jpeg', quality);
  });
}

// Bild → {blob, thumb, width, height}. Der Browser dreht Hochkant-Fotos dabei von selbst richtig.
export async function prepareImage(file, max = IMAGE_MAX) {
  const img = await loadImage(file);
  const w = img.naturalWidth, h = img.naturalHeight;
  const main = await toJpeg(img, w, h, max, 0.82);
  const thumb = await toJpeg(img, w, h, THUMB_MAX, 0.7);
  return {blob: main.blob, thumb: thumb.blob, width: main.width, height: main.height, kind: 'image', mime: 'image/jpeg'};
}

// Video → {blob, thumb (Standbild, falls der Browser eins hergibt), width, height, duration}
export async function prepareVideo(file) {
  if (file.size > config.maxUpload) {
    throw new Error(`Das Video ist ${fmtBytes(file.size)} groß – mehr als ${fmtBytes(config.maxUpload)} gehen nicht. Kürzer schneiden oder in geringerer Qualität aufnehmen.`);
  }
  const info = await new Promise(resolve => {
    const url = URL.createObjectURL(file);
    const video = document.createElement('video');
    let done = false;
    const finish = async ok => {
      if (done) return;
      done = true;
      let thumb = null;
      if (ok && video.videoWidth) {
        try { thumb = (await toJpeg(video, video.videoWidth, video.videoHeight, THUMB_MAX, 0.7)).blob; } catch (e) { /* ohne Standbild */ }
      }
      const out = {thumb, width: video.videoWidth || null, height: video.videoHeight || null, duration: isFinite(video.duration) ? video.duration : null};
      URL.revokeObjectURL(url);
      resolve(out);
    };
    video.muted = true;
    video.playsInline = true;
    video.preload = 'auto';
    video.onloadeddata = () => { try { video.currentTime = Math.min(0.5, (video.duration || 1) / 2); } catch (e) { finish(true); } };
    video.onseeked = () => finish(true);
    video.onerror = () => finish(false);
    setTimeout(() => finish(!!video.videoWidth), 6000);   // manche Handys liefern nie ein Standbild: dann eben ohne
    video.src = url;
  });
  return {blob: file, kind: 'video', mime: file.type || 'video/mp4', ...info};
}

// Datei vom Handy vorbereiten (Bild oder Video); alles andere wird abgelehnt
export function prepareFile(file) {
  if (/^image\//.test(file.type)) return prepareImage(file);
  if (/^video\//.test(file.type)) return prepareVideo(file);
  return Promise.reject(new Error('Nur Bilder und Videos.'));
}

// Vorbereitete Datei hochladen und in der Datenbank eintragen. scope: 'album' oder 'chat'. Gibt den Eintrag zurück.
export async function uploadPrepared(be, gid, prepared, {scope = 'album', eventId = null, onProgress} = {}) {
  const id = uuid();
  const now = new Date();
  const path = mediaPath(gid, scope, id, extFor(prepared.mime, prepared.kind === 'video' ? 'mp4' : 'jpg'), now);
  const thumbPath = prepared.thumb ? mediaPath(gid, scope, id + '_t', 'jpg', now) : null;
  const total = prepared.blob.size + (prepared.thumb ? prepared.thumb.size : 0);
  const uploaded = [];
  try {
    if (prepared.thumb) {
      await be.upload(thumbPath, prepared.thumb);
      uploaded.push(thumbPath);
    }
    const head = prepared.thumb ? prepared.thumb.size : 0;
    await be.upload(path, prepared.blob, p => onProgress && onProgress((head + p * prepared.blob.size) / total));
    uploaded.push(path);
    return await be.addMedia(gid, {
      id, event_id: eventId, scope, kind: prepared.kind, path, thumb_path: thumbPath, mime: prepared.mime,
      width: prepared.width || null, height: prepared.height || null, duration: prepared.duration || null, size_bytes: total,
    });
  } catch (e) {
    be.removeFiles(uploaded).catch(() => {});   // nichts Halbes im Speicher liegen lassen
    throw e;
  }
}

// Quadratisches Profilbild (256 Punkte, Mitte des Fotos)
export async function prepareAvatar(file) {
  const img = await loadImage(file);
  const side = Math.min(img.naturalWidth, img.naturalHeight);
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = 256;
  canvas.getContext('2d').drawImage(img, (img.naturalWidth - side) / 2, (img.naturalHeight - side) / 2, side, side, 0, 0, 256, 256);
  return new Promise((resolve, reject) => canvas.toBlob(b => (b ? resolve(b) : reject(new Error('Das Bild ließ sich nicht umwandeln.'))), 'image/jpeg', 0.85));
}

// ---- Sprachnachricht ---------------------------------------------------------------------------------------------------
// iPhones nehmen als MP4/AAC auf – das spielt jedes Handy ab. Android nimmt das, wenn es kann, sonst WebM/Opus
// (ältere iPhones spielen WebM nicht ab).
export const canRecord = typeof navigator !== 'undefined' && !!(navigator.mediaDevices && navigator.mediaDevices.getUserMedia) && typeof MediaRecorder !== 'undefined';

export const MAX_VOICE_SECONDS = 180;

export async function startRecording() {
  let stream;
  try {
    stream = await navigator.mediaDevices.getUserMedia({audio: true});
  } catch (e) {
    throw new Error('Kein Zugriff aufs Mikrofon. Erlaube ihn in den Einstellungen des Browsers für diese Seite.');
  }
  const type = ['audio/mp4', 'audio/webm;codecs=opus', 'audio/webm'].find(t => MediaRecorder.isTypeSupported(t)) || '';
  const rec = new MediaRecorder(stream, type ? {mimeType: type, audioBitsPerSecond: 48000} : undefined);
  const chunks = [];
  const started = Date.now();
  rec.ondataavailable = ev => { if (ev.data && ev.data.size) chunks.push(ev.data); };
  rec.start();
  const release = () => stream.getTracks().forEach(t => t.stop());
  return {
    seconds: () => (Date.now() - started) / 1000,
    // beenden und Aufnahme zurückgeben: {blob, kind: 'audio', mime, duration}
    stop: () => new Promise(resolve => {
      rec.onstop = () => {
        release();
        const mime = (rec.mimeType || type || 'audio/mp4').split(';')[0];
        resolve({blob: new Blob(chunks, {type: mime}), thumb: null, kind: 'audio', mime, duration: (Date.now() - started) / 1000});
      };
      rec.stop();
    }),
    cancel: () => { rec.onstop = release; try { rec.stop(); } catch (e) { release(); } },
  };
}
