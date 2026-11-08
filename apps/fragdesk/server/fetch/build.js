// fragdesk-fetch bündeln (aufgerufen von build.sh nach npm ci): eine Datei <ausgabe>/fragdesk-fetch.js für Node ≥ 18
// und <ausgabe>/fragdesk-fetch-NOTICES.txt mit den Lizenztexten aller gebündelten Pakete.
//
//   node build.js <ausgabeordner>
//
// Stolpersteine der Steam-Bibliotheken beim Bündeln (alle hier gelöst und in test/bundle.test.js geprüft):
//   - @doctormckay/steam-crypto liest system.pem zur Laufzeit neben sich – wird als Text eingebettet.
//   - steam-user/components/cdn_compression.js braucht adm-zip, lzma (über ein require mit Variable) und zstddec
//     (WebAssembly, geht mit --jitless nicht) – nur für Downloads vom Steam-CDN, die der Helfer nie macht: ersetzt
//     durch einen Platzhalter, der beim Aufruf einen Fehler wirft.
'use strict';

const fs = require('fs');
const path = require('path');
const esbuild = require('esbuild');

const here = __dirname;

function fail(msg) {
	console.error('Fehler: ' + msg);
	process.exit(1);
}

// Version wie beim Server aus apps/fragdesk/app.json
function appVersion() {
	try {
		return JSON.parse(fs.readFileSync(path.join(here, '..', '..', 'app.json'), 'utf8')).version || 'dev';
	} catch (e) {
		return 'dev';
	}
}

const steamCryptoPem = {
	name: 'steam-crypto-pem',
	setup(build) {
		build.onLoad({filter: /[\\/]@doctormckay[\\/]steam-crypto[\\/]index\.js$/}, (args) => {
			let src = fs.readFileSync(args.path, 'utf8');
			let pem = fs.readFileSync(path.join(path.dirname(args.path), 'system.pem'));
			let wanted = "require('fs').readFileSync(__dirname + '/system.pem')";
			if (!src.includes(wanted)) {
				fail('@doctormckay/steam-crypto hat sich geändert (system.pem wird anders gelesen) – build.js anpassen.');
			}
			src = src.replace(wanted, `Buffer.from(${JSON.stringify(pem.toString('base64'))}, 'base64')`);
			return {contents: src, loader: 'js'};
		});
	}
};

const noCdnCompression = {
	name: 'no-cdn-compression',
	setup(build) {
		build.onResolve({filter: /^\.\/cdn_compression\.js$/}, (args) => {
			if (!/[\\/]steam-user[\\/]components$/.test(args.resolveDir)) {
				return undefined;
			}
			return {path: 'steam-user-cdn-compression', namespace: 'fragdesk-stub'};
		});
		build.onLoad({filter: /.*/, namespace: 'fragdesk-stub'}, () => ({
			contents: "exports.unzip = async function() { throw new Error('fragdesk-fetch: Downloads vom Steam-CDN sind nicht eingebaut'); };\n",
			loader: 'js'
		}));
	}
};

// ---- Lizenztexte -----------------------------------------------------------------------------------------------------
function packageRoot(file) {
	let parts = file.split(/[\\/]/);
	let i = parts.lastIndexOf('node_modules');
	if (i < 0) {
		return null;
	}
	let n = parts[i + 1] && parts[i + 1].startsWith('@') ? 3 : 2;
	return parts.slice(0, i + n).join(path.sep);
}

function licenseText(root, pkg) {
	let files = fs.readdirSync(root).filter((f) => /^(licen[cs]e|copying|notice)/i.test(f)).sort();
	let texts = files.map((f) => fs.readFileSync(path.join(root, f), 'utf8').trim()).filter(Boolean);
	if (texts.length > 0) {
		return texts.join('\n\n');
	}
	// Kein Lizenztext im Paket: Lizenz laut package.json, bei MIT der Standardtext mit dem Autor
	let author = typeof pkg.author == 'string' ? pkg.author : (pkg.author && pkg.author.name) || 'den Autoren';
	if (pkg.license == 'MIT') {
		return `MIT License (kein Lizenztext im Paket, Autor laut package.json: ${author})\n\n` + MIT_TEXT;
	}
	return `Lizenz laut package.json: ${pkg.license || 'unbekannt'} (kein Lizenztext im Paket, Autor: ${author})`;
}

const MIT_TEXT = `Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.`;

function writeNotices(metafile, outFile, version) {
	let roots = new Map();
	for (let input of Object.keys(metafile.inputs)) {
		let root = packageRoot(path.resolve(here, input));
		if (root && !roots.has(root)) {
			roots.set(root, JSON.parse(fs.readFileSync(path.join(root, 'package.json'), 'utf8')));
		}
	}
	let list = [...roots.entries()].sort((a, b) => (a[1].name + a[1].version).localeCompare(b[1].name + b[1].version));
	let out = [];
	out.push(`fragdesk-fetch ${version} – Teil von FragDesk (efficient lamp, MIT-Lizenz)`);
	out.push('');
	out.push('fragdesk-fetch.js enthält die folgenden Open-Source-Pakete. Ihre Lizenztexte stehen unten.');
	out.push('');
	for (let [, pkg] of list) {
		out.push(`  ${pkg.name} ${pkg.version} (${pkg.license || 'siehe unten'})`);
	}
	for (let [root, pkg] of list) {
		out.push('');
		out.push('='.repeat(100));
		out.push(`${pkg.name} ${pkg.version}${pkg.homepage ? ' – ' + pkg.homepage : ''}`);
		out.push('='.repeat(100));
		out.push(licenseText(root, pkg));
	}
	fs.writeFileSync(outFile, out.join('\n') + '\n');
	return list.length;
}

async function main() {
	let outDir = process.argv[2];
	if (!outDir) {
		fail('Aufruf: node build.js <ausgabeordner>');
	}
	outDir = path.resolve(outDir);
	fs.mkdirSync(outDir, {recursive: true});
	let version = appVersion();
	let outFile = path.join(outDir, 'fragdesk-fetch.js');
	let result = await esbuild.build({
		entryPoints: [path.join(here, 'src', 'main.js')],
		outfile: outFile,
		bundle: true,
		platform: 'node',
		target: 'node18',
		format: 'cjs',
		minifyWhitespace: true,
		minifySyntax: true,
		legalComments: 'none',
		metafile: true,
		logLevel: 'warning',
		absWorkingDir: here,
		banner: {js: `#!/usr/bin/env node\n// fragdesk-fetch ${version} – FragDesk-Server: neue Matchmaking-Demos bei Valve holen. Lizenzen: fragdesk-fetch-NOTICES.txt`},
		define: {'process.env.FRAGDESK_FETCH_VERSION': JSON.stringify(version)},
		plugins: [steamCryptoPem, noCdnCompression]
	});
	if (result.errors.length) {
		fail('esbuild meldet Fehler');
	}
	let n = writeNotices(result.metafile, path.join(outDir, 'fragdesk-fetch-NOTICES.txt'), version);
	let size = fs.statSync(outFile).size;
	console.log(`${outFile}: ${(size / 1048576).toFixed(1)} MB, ${n} Pakete (Lizenzen in fragdesk-fetch-NOTICES.txt)`);
}

main().catch((err) => fail(err.stack || String(err)));
