// Loads the web build's engine, sound library, grooves and the project/groove generator from ui.js
// (unchanged source text), so the plugin can be checked against the exact code the web app runs.
const fs = require('fs'), path = require('path'), vm = require('vm');
const WEB = process.env.HARDER_WEB || '/home/claude/strobe';
const ctx = { console, Math, JSON, Array, Object, Number, String, Set, Map, isFinite, structuredClone };
vm.createContext(ctx);
for (const f of ['engine.js', 'presets.baked.js', 'grooves.js']) vm.runInContext(fs.readFileSync(path.join(WEB, f), 'utf8').replace(/^if \(typeof module.*$/m, ''), ctx, { filename: f });
const ui = fs.readFileSync(path.join(WEB, 'ui.js'), 'utf8');
const a = ui.indexOf('// ============ project state ============'), b = ui.indexOf('function validProject');
if (a < 0 || b < 0) throw new Error('ui.js layout changed');
const genSrc = ui.slice(a, b);
vm.runInContext(`
var C = StrobeCore();
var G = STROBE_GROOVES, PRESETS = STROBE_PRESETS, KITS = STROBE_KITS, ROLES = STROBE_ROLES;
var clamp = (x, a, b) => x < a ? a : x > b ? b : x;
var CI = name => C.COND.indexOf(name);
var presetBy = n => PRESETS.find(p => p.n === n);
var S = null;
${genSrc.replace(/function genOpts\(\)[^\n]*\n/, '')}
var GEN = { emptyPattern, presetParams, kitTracks, kitMaster, mulberry, genRow, generatePattern, polyPattern, euclid, mutatePattern, showcase, defaultProject,
  setS: s => { S = s; }, getS: () => S };
var EXP = { PRESETS: STROBE_PRESETS, KITS: STROBE_KITS, GROOVES: STROBE_GROOVES, ROLES: STROBE_ROLES, MATS: STROBE_MATS, MAT_DESC: STROBE_MAT_DESC,
  FUNCS: STROBE_FUNCS, MATERIAL: STROBE_MATERIAL, ROLE_NAMES: STROBE_ROLE_NAMES };
`, ctx, { filename: 'ui-gen.js' });
module.exports = Object.assign({ C: ctx.C, GEN: ctx.GEN, ctx }, ctx.EXP);
