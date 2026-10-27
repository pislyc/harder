// Builds test scenarios from the web app's own project/groove code and renders the JS reference.
// usage: node scen.js <outdir> [sr]
const fs = require('fs'), path = require('path');
const W = require('./webcore.js'), C = W.C, GEN = W.GEN;
const out = process.argv[2] || 'scen'; const SR = +(process.argv[3] || 48000);
fs.mkdirSync(out, { recursive: true });
const BLOCK = 128;
const engState = S => ({ tracks: S.tracks.map(t => ({ m: t.m, p: t.p })), master: S.master, patterns: S.patterns, cur: S.cur, bpm: S.bpm, swing: S.swing, mutes: S.mutes });
const barN = (bpm) => Math.round(SR * 60 / bpm * 4 / BLOCK) * BLOCK;
const opts = { den: 0.55, vari: 0.35, locks: true, fills: true, half: false };
const scen = [];
function add(name, S, msgs, n) { scen.push({ name, sr: SR, block: BLOCK, init: JSON.parse(JSON.stringify(engState(S))), msgs, n: Math.ceil(n / BLOCK) * BLOCK }); }
const q = x => Math.round(x / BLOCK) * BLOCK;

// A: default project, cues, fill, mute, duck param, pattern-level tempo changes
{
  const S = GEN.defaultProject(); GEN.setS(S); const bar = barN(S.bpm);
  add('default', S, [
    { at: 0, m: { t: 'play' } },
    { at: 3 * bar, m: { t: 'cue', i: 1 } },
    { at: 5 * bar + q(bar / 2), m: { t: 'fill', v: true } }, { at: 6 * bar, m: { t: 'fill', v: false } },
    { at: 6 * bar + q(bar / 3), m: { t: 'mute', tr: 2, v: 1 } },
    { at: 7 * bar, m: { t: 'param', tr: 8, i: 27, v: 90 } }, { at: 7 * bar, m: { t: 'param', tr: 8, i: 25, v: 0 } },
    { at: 8 * bar, m: { t: 'cue', i: 2 } },
    { at: 10 * bar, m: { t: 'param', tr: 8, i: 18, v: -30 } },
    { at: 11 * bar, m: { t: 'cue', i: 4 } },
  ], 14 * bar);
}
// B: every kit with two grooves
{
  const styles = Object.keys(W.GROOVES.styles);
  W.KITS.forEach((K, k) => {
    const S = GEN.defaultProject(); GEN.setS(S);
    S.tracks = GEN.kitTracks(K); S.master = GEN.kitMaster(K, S.master); S.kit = k;
    const s1 = styles[(k * 3) % styles.length], s2 = styles[(k * 3 + 7) % styles.length];
    S.patterns[0] = GEN.generatePattern(GEN.mulberry(100 + k), s1, Object.assign({}, opts, { vari: 0.6 }));
    S.patterns[1] = GEN.generatePattern(GEN.mulberry(200 + k), s2, Object.assign({}, opts, { den: 0.8, half: k % 2 === 1 }));
    S.cur = 0; S.bpm = S.patterns[0].bpm; S.swing = S.patterns[0].swing;
    const bar = barN(S.bpm);
    add('kit' + k + '_' + K.name, S, [{ at: 0, m: { t: 'play' } }, { at: 2 * bar, m: { t: 'cue', i: 1 } }], 6 * bar);
  });
}
// C: polymeter with heavy locks, retrigs, conditions
{
  const S = GEN.defaultProject(); GEN.setS(S);
  S.patterns[0] = GEN.polyPattern(GEN.mulberry(77), { den: 0.9, vari: 0.95, locks: true, fills: true }, 128, 58);
  S.cur = 0; S.bpm = 128; S.swing = 58; const bar = barN(128);
  add('poly', S, [{ at: 0, m: { t: 'play' } }, { at: 2 * bar, m: { t: 'fill', v: true } }, { at: 3 * bar, m: { t: 'fill', v: false } }], 8 * bar);
}
// D: every preset as a one-shot on track 1 (machine message = preset load), velocity 120
{
  const S = GEN.defaultProject(); GEN.setS(S); S.patterns.forEach(P => P.steps = P.steps.map(r => r.map(() => null)));
  const msgs = []; const each = q(SR * 1.1);
  W.PRESETS.forEach((pr, i) => {
    const p = GEN.presetParams(pr);
    msgs.push({ at: i * each, m: { t: 'machine', tr: 0, m: pr.m, p } });
    msgs.push({ at: i * each, m: { t: 'aud', tr: 0, vel: 120 } });
  });
  add('presets', S, msgs, W.PRESETS.length * each);
}
// E: performance: automation storm on SRC/FLTR/master every block, rolls with locks, solo, LFO
{
  const S = GEN.defaultProject(); GEN.setS(S); const bar = barN(S.bpm);
  S.tracks[5].p[33] = 50; S.tracks[5].p[34] = 8; S.tracks[5].p[35] = 1; S.tracks[5].p[36] = 0; S.tracks[5].p[32] = 6;
  S.tracks[1].p[33] = -40; S.tracks[1].p[34] = 10; S.tracks[1].p[36] = 2; S.tracks[1].p[35] = 6;
  const msgs = [{ at: 0, m: { t: 'play' } }];
  for (let at = 0; at < 6 * bar; at += BLOCK) {
    const u = at / (6 * bar);
    msgs.push({ at, m: { t: 'param', tr: 0, i: 0, v: Math.round((30 + 40 * Math.sin(u * 20)) * 10) / 10 } });
    msgs.push({ at, m: { t: 'param', tr: 3, i: 8, v: Math.round((70 + 50 * Math.sin(u * 13)) * 10) / 10 } });
    if (at % (BLOCK * 4) === 0) msgs.push({ at, m: { t: 'param', tr: 8, i: 9, v: Math.round(40 + 60 * u) } });
  }
  msgs.push({ at: bar, m: { t: 'roll', tr: 3, on: true, st: 0.5, vel: 100, L: { 1: 90 } } }, { at: 2 * bar, m: { t: 'roll', tr: 3, on: false } });
  msgs.push({ at: 2 * bar + q(bar / 4), m: { t: 'aud', tr: 6, vel: 127, L: { 40: 7, 8: 60 } } });
  msgs.push({ at: 3 * bar, m: { t: 'solo', tr: 0 } }, { at: 4 * bar, m: { t: 'solo', tr: -1 } });
  msgs.push({ at: 4 * bar, m: { t: 'bpm', v: 140 } }, { at: 5 * bar, m: { t: 'swing', v: 62 } });
  add('perform', S, msgs.sort((a, b) => a.at - b.at), 7 * bar);
}

// render JS reference
for (const sc of scen) {
  const t0 = Date.now();
  const e = C.createEngine(sc.sr); e.msg({ t: 'init', st: JSON.parse(JSON.stringify(sc.init)) });
  const L = new Float32Array(sc.n), R = new Float32Array(sc.n); let mi = 0;
  for (let pos = 0; pos < sc.n; pos += sc.block) {
    while (mi < sc.msgs.length && sc.msgs[mi].at <= pos) e.msg(JSON.parse(JSON.stringify(sc.msgs[mi++].m)));
    e.process(L.subarray(pos, pos + sc.block), R.subarray(pos, pos + sc.block), sc.block); e.out.length = 0;
  }
  fs.writeFileSync(path.join(out, sc.name + '.json'), JSON.stringify(sc));
  const I = new Float32Array(sc.n * 2); for (let i = 0; i < sc.n; i++) { I[2 * i] = L[i]; I[2 * i + 1] = R[i]; }
  fs.writeFileSync(path.join(out, sc.name + '.js.f32'), Buffer.from(I.buffer));
  console.log(sc.name, (sc.n / sc.sr).toFixed(1) + 's', 'rendered in', Date.now() - t0, 'ms');
}
