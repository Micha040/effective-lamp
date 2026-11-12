// Prüft, ob die Dateien der Web-App zusammenpassen – Fehler, die sonst erst auf dem Handy auffallen würden:
// vergessene Dateien im Service Worker, verschiedene Versionsnummern, kaputte Verweise, geheime Schlüssel im Quelltext.
// Aufruf: node tests/check.mjs
import {readFileSync, readdirSync, statSync, existsSync} from 'node:fs';
import {join, relative, dirname, resolve} from 'node:path';
import {fileURLToPath} from 'node:url';

const app = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const web = join(app, 'web');
const problems = [];
const read = p => readFileSync(p, 'utf8');
const walk = dir => readdirSync(dir).flatMap(n => (statSync(join(dir, n)).isDirectory() ? walk(join(dir, n)) : [join(dir, n)]));
const files = walk(web).map(p => relative(web, p).split('\\').join('/'));

// 1. Service Worker: alles, was die App braucht, steht in SHELL – und alles in SHELL gibt es
const sw = read(join(web, 'sw.js'));
const shell = [...sw.slice(sw.indexOf('const SHELL'), sw.indexOf('];', sw.indexOf('const SHELL'))).matchAll(/'([^']+)'/g)].map(m => m[1]);
for (const f of files) {
  if (f === 'sw.js' || /^vendor\/LICENSE/.test(f)) continue;
  if (!shell.includes(f)) problems.push(`sw.js: ${f} fehlt in SHELL (wäre ohne Netz nicht da)`);
}
for (const s of shell) if (s !== './' && !files.includes(s)) problems.push(`sw.js: SHELL nennt ${s}, die Datei gibt es nicht`);

// 2. Versionsnummer: config.js, sw.js und README stimmen überein
const version = (read(join(web, 'config.js')).match(/VERSION = '([^']+)'/) || [])[1];
const swVersion = (sw.match(/const VERSION = '([^']+)'/) || [])[1];
const readmeVersion = (read(join(app, 'README.md')).match(/Version (\d+\.\d+\.\d+)/) || [])[1];
if (!version) problems.push('config.js: VERSION nicht gefunden');
if (version !== swVersion) problems.push(`Version: config.js sagt ${version}, sw.js sagt ${swVersion}`);
if (version !== readmeVersion) problems.push(`Version: config.js sagt ${version}, README.md sagt ${readmeVersion}`);

// 3. Verweise: jede importierte Datei und alles aus index.html und dem Manifest gibt es
for (const f of files.filter(x => x.endsWith('.js') && !x.startsWith('vendor/'))) {
  for (const m of read(join(web, f)).matchAll(/(?:from|import\()\s*'(\.{1,2}\/[^']+)'/g)) {
    if (!existsSync(resolve(dirname(join(web, f)), m[1]))) problems.push(`${f}: importiert ${m[1]}, die Datei gibt es nicht`);
  }
}
for (const m of read(join(web, 'index.html')).matchAll(/(?:href|src)="([^"#:]+)"/g)) {
  if (!files.includes(m[1])) problems.push(`index.html: verweist auf ${m[1]}, die Datei gibt es nicht`);
}
for (const icon of JSON.parse(read(join(web, 'manifest.webmanifest'))).icons) {
  if (!files.includes(icon.src)) problems.push(`manifest.webmanifest: Symbol ${icon.src} gibt es nicht`);
}

// 4. Keine geheimen Schlüssel: Im Quelltext darf nur der öffentliche Schlüssel stehen
const repoFiles = [...walk(web), ...walk(join(app, 'supabase')), ...walk(join(app, 'tools')), ...walk(join(app, 'tests'))]
  .filter(p => !p.includes(`${join('web', 'vendor')}`) && !p.endsWith('.png') && !p.endsWith('check.mjs'));
for (const p of repoFiles) {
  const text = read(p);
  const name = relative(app, p);
  if (/sb_secret_[A-Za-z0-9_-]{8,}/.test(text)) problems.push(`${name}: enthält einen geheimen Supabase-Schlüssel (sb_secret_…)`);
  for (const m of text.matchAll(/eyJ[A-Za-z0-9_-]{10,}\.(eyJ[A-Za-z0-9_-]{10,})\.[A-Za-z0-9_-]{10,}/g)) {
    let payload = {};
    try { payload = JSON.parse(Buffer.from(m[1], 'base64url').toString('utf8')); } catch (e) { /* kein lesbarer Inhalt */ }
    if (payload.role === 'service_role') problems.push(`${name}: enthält den geheimen service_role-Schlüssel`);
  }
}
const key = (read(join(web, 'config.js')).match(/supabaseKey: '([^']*)'/) || [])[1] || '';
if (!key.startsWith('sb_publishable_')) problems.push('config.js: supabaseKey muss der öffentliche Schlüssel sein (sb_publishable_…)');

// 5. Datenbank: jede Tabelle hat Zugriffsregeln eingeschaltet
const sql = readdirSync(join(app, 'supabase/migrations')).sort().map(f => read(join(app, 'supabase/migrations', f))).join('\n');
for (const m of sql.matchAll(/create table public\.(\w+)/g)) {
  if (!new RegExp(`alter table public\\.${m[1]} enable row level security`).test(sql)) problems.push(`Datenbank: Tabelle ${m[1]} hat keine Zugriffsregeln (row level security)`);
}

if (problems.length) {
  for (const p of problems) console.error('FEHLER: ' + p);
  process.exit(1);
}
console.log(`Web-App stimmig: ${files.length} Dateien, Version ${version}, ${shell.length} Einträge im Service Worker`);
