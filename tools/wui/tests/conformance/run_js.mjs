// Ejecuta los casos de conformidad con la maquina JS del editor y compara con el diario de Python.
//   node tests/conformance/run_js.mjs <carpeta de trace.py>
import { readFileSync, readdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const web = resolve(here, '../../../../DEStudio/web/src/wui');
const { Machine, Host, Inputs, DEFAULTS } = await import(pathToFileURL(join(web, 'vm.js')));
const { loadProgram, toText, F, raw } = await import(pathToFileURL(join(web, 'core.js')));

const FPS = 60;
const FLOAT = /-?\d+\.\d+(?:e[+-]?\d+)?|-?\d+e[+-]?\d+/g;
// como norm() de trace.py: 9 cifras significativas en formato %g de Python
function g9(x) {
  if (x === 0) return '0';
  let e = Math.floor(Math.log10(Math.abs(x)));
  let m = +(x / 10 ** e).toPrecision(9);
  if (Math.abs(m) >= 10) { m /= 10; e += 1; }
  if (e < -4 || e >= 9) {
    let ms = m.toPrecision(9).replace(/\.?0+$/, '');
    return ms + 'e' + (e < 0 ? '-' : '+') + String(Math.abs(e)).padStart(2, '0');
  }
  let s = x.toFixed(Math.max(0, 8 - e));
  if (s.includes('.')) s = s.replace(/\.?0+$/, '');
  return s;
}
const norm = s => s.replace(FLOAT, m => { const r = g9(parseFloat(m)); return /[.einf]/.test(r) ? r : r + '.0'; });
const txt = v => v && typeof v === 'object' && !Array.isArray(v) && !(v instanceof F) && v.constructor === Object
  ? '{' + Object.keys(v).sort().map(k => `${k}=${txt(v[k])}`).join(', ') + '}' : toText(v);

class SimHost extends Host {
  constructor(layers, screens, game) {
    super();
    this.layers = new Map(layers.map(n => [n, { name: n, x: new F(640), y: new F(360), w: new F(200), h: new F(80), kind: 'shape',
      props: { ...DEFAULTS, x: new F(640), y: new F(360), focusable: false } }]));
    this.screens = screens; this.game = { ...game }; this.journal = [];
  }
  layerNames() { return [...this.layers.keys()]; }
  layerGet(n, p) {
    const L = this.layers.get(n);
    if (!L) return null;
    if (['w', 'h', 'kind', 'name'].includes(p)) return L[p];
    return p in L.props ? L.props[p] : null;
  }
  layerSet(n, p, v) { const L = this.layers.get(n); if (L) { L.props[p] = v; this.journal.push(['set', n, p, v]); } }
  layerDesign(n) { const L = this.layers.get(n); return L ? { ...DEFAULTS, x: L.x, y: L.y } : {}; }
  layerContains(n, x, y) {
    const L = this.layers.get(n); if (!L) return false;
    const s = raw(L.props.scale ?? 1) || 1, px = raw(L.props.x), py = raw(L.props.y);
    return Math.abs(x - px) <= raw(L.w) * s / 2 && Math.abs(y - py) <= raw(L.h) * s / 2;
  }
  layerAt(x, y) {
    for (const n of [...this.layers.keys()].reverse()) if (this.layers.get(n).props.visible !== false && this.layerContains(n, x, y)) return n;
    return null;
  }
  screenNames() { return [...this.screens]; }
  showScreen(n, tr) { this.journal.push(['screen', n, tr]); }
  close() { this.journal.push(['close']); }
  setFocus(n) { this.journal.push(['focus', n]); }
  moveFocus(d) { this.journal.push(['move_focus', d]); }
  lockFocus(l) { this.journal.push(['lock_focus', l]); }
  gameGet(n) { return n in this.game ? this.game[n] : null; }
  gameSet(n, v) { this.game[n] = v; this.journal.push(['game_set', n, v]); }
  gameCall(s, p) { this.journal.push(['game_call', s, p]); return null; }
  playerInfo() { return 0; }
  bandInfo() { return 0; }
  gameState(w) { return this.game['#' + w] ?? null; }
  playSound(n) { this.journal.push(['sound', n]); }
  vibrate(p, s, t) { this.journal.push(['vibrate', p, new F(s), new F(t)]); }
}

function run(c, prog) {
  const host = new SimHost(c.layers, c.screens, { '#language': 'es' });
  const vm = new Machine(prog, host, { seed: c.seed });
  vm.start();
  const held = new Set();
  const script = [...c.script].sort((a, b) => a[0] - b[0]);
  let i = 0;
  const frames = Math.round(c.seconds * FPS);
  for (let f = 0; f < frames; f++) {
    const t = f / FPS;
    while (i < script.length && script[i][0] <= t + 1e-9) {
      const [, kind, val] = script[i];
      if (kind === 'down') held.add(val);
      else if (kind === 'up') held.delete(val);
      else if (kind === 'focus') vm.notifyFocus(val);
      else if (kind === 'click') vm.notifyClick(val);
      i++;
    }
    vm.step(1 / FPS, new Inputs({ 1: new Set(held) }));
  }
  return {
    logs: vm.logs.map(([l, m]) => `${l}: ${norm(m)}`),
    journal: host.journal.map(j => norm(j.map(txt).join(' '))),
    errors: vm.errors.map(e => `${e.function}:${e.line}: ${e.message}`),
    globals: Object.fromEntries(prog.globals.map(n => [n, norm(txt(vm.globalValue(n)))])),
  };
}

const dir = process.argv[2];
let bad = 0, total = 0;
for (const f of readdirSync(dir).filter(f => f.endsWith('.case.json')).sort()) {
  const name = f.replace('.case.json', '');
  total++;
  const c = JSON.parse(readFileSync(join(dir, f), 'utf8'));
  const exp = JSON.parse(readFileSync(join(dir, name + '.expected.json'), 'utf8'));
  let got;
  try { got = run(c, loadProgram(readFileSync(join(dir, name + '.wuib')))); }
  catch (e) { console.log(`FALLA ${name}: ${e.stack}`); bad++; continue; }
  const diffs = [];
  for (const k of ['logs', 'errors', 'journal']) {
    const a = exp[k], b = got[k];
    for (let i = 0; i < Math.max(a.length, b.length); i++) {
      if (a[i] !== b[i]) { diffs.push(`${k}[${i}]\n    python: ${a[i]}\n    js:     ${b[i]}`); break; }
    }
  }
  for (const k of Object.keys(exp.globals)) if (exp.globals[k] !== got.globals[k]) diffs.push(`global ${k}: python ${exp.globals[k]} / js ${got.globals[k]}`);
  if (diffs.length) { bad++; console.log(`DISTINTO ${name}\n  ` + diffs.join('\n  ')); }
  else console.log(`igual    ${name}`);
}
console.log(`${total - bad}/${total} casos iguales`);
process.exit(bad ? 1 : 0);
