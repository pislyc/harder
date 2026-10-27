// Web reference for the export: runs the web app's own export code (cut out of ui.js) on test projects.
// usage: node export_ref.js <outdir>   -> <outdir>/<case>.project.json, <case>.web.zip
const fs = require('fs'), path = require('path'), vm = require('vm');
const W = require('./webcore.js'), C = W.C, G = W.GEN;
const WEB = process.env.HARDER_WEB || '/home/claude/strobe';
const ui = fs.readFileSync(path.join(WEB, 'ui.js'), 'utf8');
const cut = (a, b) => { const i = ui.indexOf(a), j = ui.indexOf(b, i); if (i < 0 || j < 0) throw new Error('ui.js layout changed: ' + a); return ui.slice(i, j); };
let src = cut('const CRC_T', 'const idle =') + cut('async function renderLoop', 'function status(') + cut('const safe =', 'async function exportLoop')
  + cut('async function exportLoop', 'async function saveProject') + cut('function renderOneShot', 'function drawPreview');
src = src.replace("return new Blob([...parts, ...central, new Uint8Array(e.buffer)], { type: 'application/zip' });",
  "return Buffer.concat([...parts, ...central, new Uint8Array(e.buffer)].map(x => Buffer.from(x.buffer, x.byteOffset, x.byteLength)));");
src = src.replace(/const dl = await getDl\(\); if \(!dl\) return; setBusy\(true\);/g, 'const dl = null;')
  .replace("+$('xLoops').value", 'OPT.loops').replace("$('xStems').checked", 'OPT.stems').replace(/setBusy\(false\);/g, '');
if (src.includes('$(')) throw new Error('DOM access left in export code');
const out = process.argv[2] || 'export_ref'; fs.mkdirSync(out, { recursive: true });
const ctx = vm.createContext({ C, S: null, OPT: {}, RESULT: null, Math, JSON, Uint8Array, Uint32Array, Float32Array, DataView, ArrayBuffer, TextEncoder, Buffer, Promise, setTimeout, performance,
  console, clamp: (x, a, b) => x < a ? a : x > b ? b : x, mname: id => C.MACHINES[C.MACHINE_IDS.indexOf(id)].name,
  pat: () => ctx.S.patterns[ctx.S.cur], idle: () => Promise.resolve(), status: () => {},
  saveFile: (dl, filename, data) => { ctx.RESULT = { filename, data }; },
  engineState: () => { const S = ctx.S; return { tracks: S.tracks.map(t => ({ m: t.m, p: t.p })), master: S.master, patterns: S.patterns, cur: S.cur, bpm: S.bpm, swing: S.swing, mutes: S.mutes }; } });
vm.runInContext(src + '\nthis.exportLoop = exportLoop; this.exportKit = exportKit;', ctx);

// test projects
const cases = [];
{ const S = G.defaultProject(); cases.push({ name: 'default', S, loops: 1, stems: true }); }
const styles = Object.keys(W.GROOVES.styles);
W.KITS.forEach((K, k) => {
  const S = G.defaultProject(); G.setS(S); S.tracks = G.kitTracks(K); S.master = G.kitMaster(K, S.master); S.kit = k;
  const st = styles[(k * 5 + 2) % styles.length];
  S.patterns[0] = G.generatePattern(G.mulberry(700 + k), st, { den: 0.75, vari: 0.7, locks: true, fills: true, half: k % 3 === 1 });
  S.cur = 0; S.bpm = S.patterns[0].bpm; S.swing = S.patterns[0].swing;
  if (k === 3) S.mutes[2] = 1;
  cases.push({ name: 'kit' + k, S, loops: k % 2 ? 2 : 1, stems: k < 4 });
});
{ const S = G.defaultProject(); G.setS(S); S.patterns[1] = G.polyPattern(G.mulberry(4242), { den: 0.8, vari: 0.6, locks: true }, 133, 57); S.cur = 1; S.bpm = 133; S.swing = 57;
  cases.push({ name: 'poly', S, loops: 2, stems: true }); }
(async () => {
  for (const c of cases) {
    ctx.S = c.S; G.setS(c.S); ctx.OPT = { loops: c.loops, stems: c.stems };
    fs.writeFileSync(path.join(out, c.name + '.project.json'), JSON.stringify(c.S));
    fs.writeFileSync(path.join(out, c.name + '.opts.json'), JSON.stringify({ loops: c.loops, stems: c.stems }));
    ctx.RESULT = null; await ctx.exportLoop(); if (!ctx.RESULT) throw new Error('no loop export for ' + c.name);
    fs.writeFileSync(path.join(out, c.name + '.loop.web.zip'), ctx.RESULT.data); fs.writeFileSync(path.join(out, c.name + '.loop.name'), ctx.RESULT.filename);
    ctx.RESULT = null; await ctx.exportKit(); if (!ctx.RESULT) throw new Error('no kit export for ' + c.name);
    fs.writeFileSync(path.join(out, c.name + '.kit.web.zip'), ctx.RESULT.data);
    console.log(c.name, ctx.RESULT ? 'ok' : '');
  }
})();
