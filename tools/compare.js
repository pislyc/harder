// Compare two interleaved stereo float32 renders: sample error, level, spectrum-envelope agreement.
// usage: node compare.js a.f32 b.f32 sr [name]
const fs = require('fs');
const [a, b, srS, name] = process.argv.slice(2); const sr = +srS || 48000;
const A = new Float32Array(fs.readFileSync(a).buffer.slice(0)), B = new Float32Array(fs.readFileSync(b).buffer.slice(0));
const n = Math.min(A.length, B.length);
let maxd = 0, maxAt = 0, se = 0, sa = 0, sb = 0, sab = 0, pa = 0, pb = 0;
for (let i = 0; i < n; i++) {
  const x = A[i], y = B[i], d = Math.abs(x - y);
  if (d > maxd) { maxd = d; maxAt = i; }
  se += d * d; sa += x * x; sb += y * y; sab += x * y; pa = Math.max(pa, Math.abs(x)); pb = Math.max(pb, Math.abs(y));
}
const db = v => (10 * Math.log10(v + 1e-30)).toFixed(1);
const corr = sab / Math.sqrt(sa * sb + 1e-30);
// 100 ms window RMS envelopes (dB) - catches level/decay drift even where the waveform decorrelates (chaotic FM feedback)
const W = Math.round(sr * 0.1) * 2; let envMax = 0, envAt = 0;
for (let i = 0; i + W <= n; i += W) {
  let ea = 0, eb = 0; for (let k = i; k < i + W; k++) { ea += A[k] * A[k]; eb += B[k] * B[k]; }
  if (ea / W < 1e-8 && eb / W < 1e-8) continue;   // both below -80 dBFS
  const d = Math.abs(10 * Math.log10((ea + 1e-20) / (eb + 1e-20))); if (d > envMax) { envMax = d; envAt = i; }
}
const res = { name: name || a, seconds: +(n / 2 / sr).toFixed(1), maxAbsDiff: +maxd.toExponential(2), maxAtSec: +(maxAt / 2 / sr).toFixed(3),
  errVsSignal_dB: +db(se / (sa + 1e-30)), correlation: +corr.toFixed(7), rmsA_dB: +db(sa / n), rmsB_dB: +db(sb / n), peakA: +pa.toFixed(4), peakB: +pb.toFixed(4),
  env100ms_maxDiff_dB: +envMax.toFixed(2), envAtSec: +(envAt / 2 / sr).toFixed(1) };
console.log(JSON.stringify(res));
