// Compares C++ and web export zips entry by entry: names, WAV headers and samples (24-bit LSB),
// MIDI events (tick, status, note, velocity), JSON content.
const fs = require('fs'), path = require('path');
function unzip(buf) {   // stored zips only (both sides write stored)
  const out = new Map(); let o = 0;
  while (buf.readUInt32LE(o) === 0x04034b50) {
    const size = buf.readUInt32LE(o + 18), nl = buf.readUInt16LE(o + 26), xl = buf.readUInt16LE(o + 28), crc = buf.readUInt32LE(o + 14);
    const name = buf.slice(o + 30, o + 30 + nl).toString('utf8'); out.set(name, { data: buf.slice(o + 30 + nl + xl, o + 30 + nl + xl + size), crc }); o += 30 + nl + xl + size;
  }
  return out;
}
function wavCmp(a, b) {
  if (!a.slice(0, 44).equals(b.slice(0, 44))) return { ok: false, why: 'header/length' };
  let maxd = 0, n1 = 0; const n = (a.length - 44) / 3;
  for (let i = 0; i < n; i++) { const o = 44 + i * 3; let x = a[o] | a[o + 1] << 8 | a[o + 2] << 16, y = b[o] | b[o + 1] << 8 | b[o + 2] << 16; if (x & 0x800000) x -= 0x1000000; if (y & 0x800000) y -= 0x1000000; const d = Math.abs(x - y); if (d) { n1++; maxd = Math.max(maxd, d); } }
  return { ok: maxd <= 1, maxd, n1, n };
}
function midiEvents(b) {
  const trkLen = b.readUInt32BE(18); let o = 22, end = 22 + trkLen, t = 0; const ev = [];
  while (o < end) { let d = 0, c; do { c = b[o++]; d = (d << 7) | (c & 127); } while (c & 128); t += d;
    if (b[o] === 0xFF) { const ty = b[o + 1], l = b[o + 2]; ev.push([t, 'meta' + ty, ...b.slice(o + 3, o + 3 + l)].join(',')); o += 3 + l; } else { ev.push([t, b[o], b[o + 1], b[o + 2]].join(',')); o += 3; } }
  return { header: b.slice(0, 18).toString('hex'), ev };
}
function deepEq(x, y, p, errs) {
  if (typeof x === 'number' && typeof y === 'number') { if (Math.abs(x - y) > 1e-9) errs.push(p + ' ' + x + ' vs ' + y); return; }
  if (x === null || y === null || typeof x !== 'object' || typeof y !== 'object') { if (!(x === y || (x == null && y == null))) errs.push(p + ' ' + JSON.stringify(x) + ' vs ' + JSON.stringify(y)); return; }
  const ks = new Set([...Object.keys(x).filter(k => x[k] !== undefined), ...Object.keys(y).filter(k => y[k] !== undefined)]); for (const k of ks) deepEq(x[k], y[k], p + '.' + k, errs);
}
const dir = process.argv[2]; let fails = 0, totalSamples = 0, lsbDiff = 0, midiReordered = 0;
for (const f of fs.readdirSync(dir).filter(f => f.endsWith('.web.zip'))) {
  const web = unzip(fs.readFileSync(path.join(dir, f))), cpp = unzip(fs.readFileSync(path.join(dir, f.replace('.web.zip', '.cpp.zip'))));
  const names = [...web.keys()].join('|'), cnames = [...cpp.keys()].join('|');
  if (names !== cnames) { console.log(f, 'ENTRIES DIFFER\n  web', names, '\n  cpp', cnames); fails++; continue; }
  const notes = [];
  for (const [name, w] of web) {
    const c = cpp.get(name);
    if (name.endsWith('.wav')) { const r = wavCmp(w.data, c.data); totalSamples += r.n || 0; lsbDiff += r.n1 || 0; if (!r.ok) { fails++; notes.push(name + ' WAV ' + JSON.stringify(r)); } else if (r.n1) notes.push(name + ': ' + r.n1 + ' of ' + r.n + ' samples 1 LSB'); }
    else if (name.endsWith('.mid')) { const a = midiEvents(w.data), b = midiEvents(c.data);
      if (w.data.equals(c.data)) notes.push(name + ': identical bytes');
      else if (a.header === b.header && JSON.stringify([...a.ev].sort()) === JSON.stringify([...b.ev].sort()) && a.ev.length === b.ev.length) { midiReordered++; notes.push(name + ': same events, simultaneous events in another order'); }
      else { fails++; notes.push(name + ' MIDI DIFFERS ' + a.ev.length + ' vs ' + b.ev.length); } }
    else if (name.endsWith('.json')) { const errs = []; deepEq(JSON.parse(w.data), JSON.parse(c.data), '', errs); if (errs.length) { fails++; notes.push(name + ' JSON ' + errs.slice(0, 3).join('; ')); } }
  }
  console.log(f.replace('.web.zip', ''), web.size, 'files', notes.length ? '| ' + notes.join(' | ') : '| identical');
}
console.log(`export compare: ${fails} failures; ${lsbDiff} of ${totalSamples} samples differ by 1 LSB (24-bit); ${midiReordered} MIDI files with simultaneous events reordered`);
process.exit(fails ? 1 : 0);
