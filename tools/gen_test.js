// JS side of the generator equivalence test; compares with the C++ dump on stdin.
const W = require('./webcore.js'), G = W.GEN;
const out = []; let S = G.defaultProject(); G.setS(S); out.push(JSON.parse(JSON.stringify(S)));
const dens = [0.3, 0.55, 0.9], varis = [0.1, 0.6, 0.95], styles = Object.keys(W.GROOVES.styles);
for (let k = 0; k < W.KITS.length; k++) {
  S = G.defaultProject(); G.setS(S); S.tracks = G.kitTracks(W.KITS[k]); S.master = G.kitMaster(W.KITS[k], S.master);
  for (let st = 0; st < styles.length; st++) for (let sd = 1; sd <= 3; sd++) {
    const o = { den: dens[(st + sd) % 3], vari: varis[(st + 2 * sd + k) % 3], locks: sd !== 2, fills: sd !== 3, half: (st + k) % 4 === 0 };
    const P = G.generatePattern(G.mulberry(k * 1000 + st * 10 + sd), styles[st], o, sd === 3 ? [111, 61] : null); out.push(JSON.parse(JSON.stringify(P)));
    G.mutatePattern(P, G.mulberry(k * 7 + st * 13 + sd), o.vari); out.push(JSON.parse(JSON.stringify(P)));
  }
  for (let sd = 0; sd < 5; sd++) { const o = { den: dens[sd % 3], vari: varis[(sd + k) % 3], locks: sd % 2 === 0 }; out.push(JSON.parse(JSON.stringify(G.polyPattern(G.mulberry(50000 + k * 10 + sd), o, 120 + sd, 50 + sd)))); }
}
const cpp = JSON.parse(require('fs').readFileSync(0, 'utf8'));
// normalise: JS pattern steps rows have nulls; JS project has v/kit etc. Compare structurally with numeric tolerance.
let bad = 0, n = 0;
function eq(a, b, p) {
  n++;
  if (typeof a === 'number' && typeof b === 'number') { if (Math.abs(a - b) > 1e-9) { if (bad++ < 10) console.log('num', p, a, b); } return; }
  if (a === null || b === null || typeof a !== 'object') { if (a !== b && !(a == null && b == null)) { if (bad++ < 10) console.log('val', p, JSON.stringify(a), JSON.stringify(b)); } return; }
  const ka = Object.keys(a).filter(k => a[k] !== undefined), kb = Object.keys(b);
  const all = new Set([...ka, ...kb]);
  for (const k of all) eq(a[k], b[k], p + '.' + k);
}
if (cpp.length !== out.length) console.log('count', cpp.length, out.length);
for (let i = 0; i < out.length; i++) eq(out[i], cpp[i], '[' + i + ']');
console.log('compared', out.length, 'objects,', n, 'values,', bad, 'mismatches');
process.exit(bad ? 1 : 0);
