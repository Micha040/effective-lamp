// FragDesk Handy-App – Zugriff auf FragDesk bzw. den eigenen FragDesk-Server (gleiche Adresse, Anmeldung per Cookie)

export class ApiError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

const memo = new Map();   // letzte Antworten (zum sofortigen Anzeigen beim Zurückgehen)

// "pc" (FragDesk liefert die App selbst aus) oder "server" (eigener FragDesk-Server) – gemerkt für Meldungen ohne Netz
let kind = 'pc';
try { kind = JSON.parse(localStorage.getItem('fd.kind')) === 'server' ? 'server' : 'pc'; } catch (e) { /* privat: egal */ }
export function setKind(k) {
  kind = k === 'server' ? 'server' : 'pc';
  try { localStorage.setItem('fd.kind', JSON.stringify(kind)); } catch (e) { /* privat: egal */ }
}
export function isServer() { return kind === 'server'; }

async function request(path, opts = {}) {
  const ctl = new AbortController();
  const timer = setTimeout(() => ctl.abort(), opts.timeout || 15000);
  const method = opts.method || 'GET';
  const headers = {};
  if (method !== 'GET') headers['X-FragDesk'] = '1';   // schreibende Anfragen nur mit eigenem Kopf
  if (opts.body) headers['Content-Type'] = 'application/json';
  let res;
  try {
    res = await fetch(path, {
      method,
      credentials: 'same-origin',
      cache: 'no-store',
      headers,
      body: opts.body ? JSON.stringify(opts.body) : undefined,
      signal: ctl.signal,
    });
  } catch (e) {
    throw new ApiError(0, kind === 'server' ? 'Keine Verbindung zum Server. Bist du online?'
      : 'Keine Verbindung zu FragDesk. Ist dein PC an und FragDesk offen? Unterwegs: Ist Tailscale an?');
  } finally {
    clearTimeout(timer);
  }
  let data = null;
  try { data = await res.json(); } catch (e) { /* leer */ }
  if (!res.ok) {
    const msg = (data && data.error) || `Fehler ${res.status}`;
    if (res.status === 401) window.dispatchEvent(new CustomEvent('fd:unpaired'));
    throw new ApiError(res.status, msg);
  }
  return data;
}

export async function get(path) {
  const data = await request(path);
  memo.set(path, data);
  return data;
}

export function cached(path) { return memo.get(path); }
export function forget() { memo.clear(); }

export function post(path, body) { return request(path, {method: 'POST', body: body || {}}); }
export function put(path, body) { return request(path, {method: 'PUT', body: body || {}}); }
export function del(path) { return request(path, {method: 'DELETE'}); }
