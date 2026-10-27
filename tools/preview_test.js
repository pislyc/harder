// JS reference for the UI waveform preview: web renderOneShot (fresh engine per render) for every preset.
const W = require('./webcore.js'), C = W.C, G = W.GEN; const PSR = 22050, N = Math.round(PSR * 2.4);
const out = Buffer.alloc(W.PRESETS.length * N * 4); let o = 0;
for (const pr of W.PRESETS) {
  const t = { m: pr.m, p: G.presetParams(pr) }, e = C.createEngine(PSR);
  e.msg({ t: 'machine', tr: 0, m: t.m, p: t.p }); e.msg({ t: 'bpm', v: 125 }); e.trigTrack(0, null, t.p[41] || 120);
  const L = new Float32Array(N), R = new Float32Array(N), st = [...Array(8)].map(() => new Float32Array(N)); e.process(L, R, N, st);
  for (let i = 0; i < N; i++, o += 4) out.writeFloatLE(st[0][i], o);
}
require('fs').writeFileSync(process.argv[2], out);
