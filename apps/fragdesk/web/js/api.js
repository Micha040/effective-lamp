// FragDesk Handy-App – Zugriff auf FragDesk (gleiche Adresse, Anmeldung per Cookie)

export class ApiError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

const memo = new Map();   // letzte Antworten (zum sofortigen Anzeigen beim Zurückgehen)

async function request(path, opts = {}) {
  const ctl = new AbortController();
  const timer = setTimeout(() => ctl.abort(), opts.timeout || 15000);
  let res;
  try {
    res = await fetch(path, {
      method: opts.method || 'GET',
      credentials: 'same-origin',
      cache: 'no-store',
      headers: opts.body ? {'Content-Type': 'application/json', 'X-FragDesk': '1'} : (opts.method === 'POST' ? {'X-FragDesk': '1'} : {}),
      body: opts.body ? JSON.stringify(opts.body) : undefined,
      signal: ctl.signal,
    });
  } catch (e) {
    throw new ApiError(0, 'Keine Verbindung zu FragDesk. Ist dein PC an und FragDesk offen? Unterwegs: Ist Tailscale an?');
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
