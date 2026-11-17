// Libreria de wui en JS: traduccion directa de tools/wui/wui/stdlib/*/impl.py. Cada funcion recibe
// (ctx, a, kw): a = argumentos ya ordenados segun la firma (los fijos con sus valores por defecto, despues los
// extra de *args), kw = los extra con nombre (**props). ctx = { vm, task }.
import {
  F, Layer, FuncRef, TaskRef, Block, fault, isNum, isNumB, raw, typeName, toText, truthy, eq, cmp, parseColor,
  formatValue, roundHalfEven, fixedStr,
} from './core.js';
import TABLES from './tables.js';

const BUTTONS = new Set(TABLES.buttons);
const FRETS = ['green', 'red', 'yellow', 'blue', 'orange'];
const SOUNDS = new Set(TABLES.sounds);

// ---------------------------------------------------------------- ayudantes
const num = (v, what = 'un numero') => {
  if (isNum(v)) return v;
  if (typeof v === 'boolean') return v ? 1 : 0;
  fault(`se esperaba ${what} y llego ${typeName(v)} (${toText(v)})`);
};
const n = v => raw(num(v));                       // numero de JS
const int = v => Math.trunc(n(v));
const flt = x => new F(x);
const same = v => v;                              // entero o decimal tal cual
const lst = v => { if (!Array.isArray(v)) fault(`se esperaba una lista y llego ${typeName(v)}`); return v; };
const mutable = v => { lst(v); if (Object.isFrozen(v)) fault('esta lista es constante'); return v; };
const pyStr = s => [...s];
const pyFloatParse = s => {
  const t = s.trim();
  if (!/^[+-]?(\d+\.?\d*(e[+-]?\d+)?|\.\d+(e[+-]?\d+)?|inf(inity)?|nan)$/i.test(t)) return null;
  return /inf/i.test(t) ? (t[0] === '-' ? -Infinity : Infinity) : /nan/i.test(t) ? NaN : parseFloat(t);
};
/** round de Python con decimales */
function pyRound(x, d) {
  if (d <= 0) { const k = 10 ** -d; return roundHalfEven(x / k) * k; }
  return +fixedStr(x, d);
}
const secs = (v, what = 'segundos') => {
  if (!isNum(v)) fault(`se esperaban ${what} (un numero) y llego ${typeName(v)} (${toText(v)})`);
  return raw(v);
};
const fnref = v => { if (!(v instanceof FuncRef)) fault(`se esperaba una funcion (su nombre, sin parentesis) y llego ${typeName(v)}`); return v; };
const task = v => { if (!(v instanceof TaskRef)) fault(`se esperaba una tarea y llego ${typeName(v)}`); return v; };
const L = v => { if (!(v instanceof Layer)) fault(`se esperaba una capa y llego ${typeName(v)} (${toText(v)})`); return v.name; };
const lnum = (v, what) => { if (!isNum(v)) fault(`${what}: se esperaba un numero y llego ${typeName(v)} (${toText(v)})`); return raw(v); };
const color = (v, what = 'color') => { if (!parseColor(v)) fault(`${what}: '${toText(v)}' no es un color (usa "#ff0000", "red" o [255, 0, 0])`); return v; };
const button = v => {
  if (!BUTTONS.has(v)) fault(`boton desconocido '${toText(v)}'`);
  return v;
};

function sortList(a, reverse) {
  const items = a.map((v, i) => [v, i]);
  items.sort((x, y) => {
    const c = cmp(x[0], y[0]);
    if (c === null) fault('sort(): la lista mezcla tipos que no se pueden comparar');
    return (reverse ? -c : c) || x[1] - y[1];
  });
  a.splice(0, a.length, ...items.map(x => x[0]));
}

function pyIndex(i, len) {
  i = int(i);
  return i < 0 ? Math.max(0, i + len) : Math.min(i, len);
}

function pySlice(seq, start, stop) {
  const arr = typeof seq === 'string' ? pyStr(seq) : seq;
  const len = arr.length;
  const s = start === null ? 0 : pyIndex(start, len), e = stop === null ? len : pyIndex(stop, len);
  const part = arr.slice(s, Math.max(s, e));
  return typeof seq === 'string' ? part.join('') : part;
}

function maybeWait(ctx, ids, wait) { return truthy(wait) && ids.length ? ctx.vm.anim.waitFor(ids) : null; }
function fx(ctx, name, values, t) {
  t = lnum(t, 'time');
  for (const [k, v] of Object.entries(values)) {
    if (t > 0) ctx.vm.anim.tween(name, k, v, t, 'out'); else ctx.vm.layerSet(name, k, v);
  }
}
function to(ctx, name, props, t, ease, wait) {
  const ids = Object.entries(props).map(([k, v]) => ctx.vm.anim.tween(name, k, v, lnum(t, 'time'), toText(ease)));
  return maybeWait(ctx, ids, wait);
}

// ---------------------------------------------------------------- la libreria
export const STDLIB = {
  // ----- conversiones y registro
  int: (c, a) => {
    const v = a[0];
    if (typeof v === 'string') { const x = pyFloatParse(v); if (x === null || !Number.isFinite(x)) fault(`'${v}' no es un numero`); return Math.trunc(x); }
    return Math.trunc(n(v));
  },
  float: (c, a) => {
    const v = a[0];
    if (typeof v === 'string') { const x = pyFloatParse(v); if (x === null) fault(`'${v}' no es un numero`); return flt(x); }
    return flt(n(v));
  },
  str: (c, a) => toText(a[0]),
  bool: (c, a) => truthy(a[0]),
  len: (c, a) => {
    const v = a[0];
    if (Array.isArray(v)) return v.length;
    if (typeof v === 'string') return pyStr(v).length;
    fault(`len() necesita una lista o un texto, no ${typeName(v)}`);
  },
  range: (c, a) => {
    let [start, stop, step] = a;
    if (stop === null) { stop = start; start = 0; }
    start = int(start); stop = int(stop); step = int(step);
    if (step === 0) fault('range() con paso 0');
    const cnt = step > 0 ? Math.max(0, Math.ceil((stop - start) / step)) : Math.max(0, Math.ceil((start - stop) / -step));
    if (cnt > 100000) fault('range() demasiado grande (100000 como mucho)');
    return Array.from({ length: cnt }, (_, i) => start + i * step);
  },
  slice: (c, a) => {
    const [seq, s, e] = a;
    if (!Array.isArray(seq) && typeof seq !== 'string') fault(`[a:b] necesita una lista o un texto, no ${typeName(seq)}`);
    return pySlice(seq, s === null ? null : num(s), e === null ? null : num(e));
  },
  type: (c, a) => typeName(a[0]),
  log: (c, a) => { const sep = typeof a[0] === 'string' ? a[0] : ' '; c.vm.log('info', a.slice(1).map(toText).join(sep)); },
  print: (c, a) => STDLIB.log(c, a),
  warn: (c, a) => { c.vm.log('warn', a.map(toText).join(' ')); },

  // ----- matematicas
  abs: (c, a) => { const v = num(a[0]); return v instanceof F ? flt(Math.abs(v.v)) : Math.abs(v); },
  min: (c, a) => many(a, -1),
  max: (c, a) => many(a, 1),
  sum: (c, a) => {
    if (!Array.isArray(a[0])) fault('sum() necesita una lista');
    let total = 0, fl = false;
    for (const v of a[0]) { const x = num(v); if (x instanceof F) fl = true; total += raw(x); }
    return fl ? flt(total) : total;
  },
  round: (c, a) => {
    const x = num(a[0]), d = int(a[1]);
    if (!(x instanceof F)) return d >= 0 ? raw(x) : pyRound(raw(x), d);     // round de un entero: entero
    const v = pyRound(x.v, d);
    return d <= 0 ? Math.round(v) : flt(v);
  },
  floor: (c, a) => Math.floor(n(a[0])),
  ceil: (c, a) => Math.ceil(n(a[0])),
  sqrt: (c, a) => { const x = n(a[0]); if (x < 0) fault('sqrt() de un numero negativo'); return flt(Math.sqrt(x)); },
  sin: (c, a) => flt(Math.sin(n(a[0]) * Math.PI / 180)),
  cos: (c, a) => flt(Math.cos(n(a[0]) * Math.PI / 180)),
  atan2: (c, a) => flt(Math.atan2(n(a[0]), n(a[1])) * 180 / Math.PI),
  clamp: (c, a) => { const [x, lo, hi] = a.map(num); return raw(x) < raw(lo) ? lo : raw(x) > raw(hi) ? hi : x; },
  lerp: (c, a) => { const [x, y, t] = a.map(num); return arith(x, y, t, (p, q, r) => p + (q - p) * r); },
  remap: (c, a) => {
    const [x, lo, hi, cc, d] = a.map(num);
    if (raw(hi) === raw(lo)) return flt(raw(cc));
    return flt(raw(cc) + (raw(x) - raw(lo)) / (raw(hi) - raw(lo)) * (raw(d) - raw(cc)));
  },
  approach: (c, a) => {
    const [x, t, s] = a.map(num);
    const sa = Math.abs(raw(s)), fl = [x, s].some(v => v instanceof F);
    const r = raw(x) < raw(t) ? Math.min(raw(t), raw(x) + sa) : Math.max(raw(t), raw(x) - sa);
    // min/max de Python devuelven el objeto original: el tipo sale del que gana
    const won = raw(x) < raw(t) ? (raw(t) <= raw(x) + sa ? t : null) : (raw(t) >= raw(x) - sa ? t : null);
    if (won !== null) return won;
    return fl ? flt(r) : r;
  },
  sign: (c, a) => { const x = n(a[0]); return (x > 0 ? 1 : 0) - (x < 0 ? 1 : 0); },
  random: (c, a) => { const lo = n(a[0]), hi = n(a[1]); return flt(lo + c.vm.rng.random() * (hi - lo)); },
  randint: (c, a) => {
    const lo = int(a[0]), hi = int(a[1]);
    if (hi < lo) fault(`randint(${lo}, ${hi}): el segundo tiene que ser mayor`);
    return lo + c.vm.rng.below(hi - lo + 1);
  },
  choice: (c, a) => {
    if (!Array.isArray(a[0]) || !a[0].length) fault('choice() necesita una lista con elementos');
    return a[0][c.vm.rng.below(a[0].length)];
  },
  shuffle: (c, a) => {
    if (!Array.isArray(a[0])) fault('shuffle() necesita una lista');
    const x = mutable(a[0]);
    for (let i = x.length - 1; i > 0; i--) { const j = c.vm.rng.below(i + 1); [x[i], x[j]] = [x[j], x[i]]; }
  },
  format_number: (c, a) => {
    const x = num(a[0]), d = int(a[1]);
    return formatValue(x instanceof F ? x : flt(raw(x)), (truthy(a[2]) ? ',' : '') + `.${d}f`);
  },
  time_text: (c, a) => {
    const s = Math.max(0, int(a[0]));
    const h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60, sec = s % 60, p2 = v => String(v).padStart(2, '0');
    if (truthy(a[1]) || h) return `${h}:${p2(m)}:${p2(sec)}`;
    return `${Math.floor(s / 60)}:${p2(sec)}`;
  },
  percent: (c, a) => {
    const v = n(a[0]), t = n(a[1]), d = int(a[2]);
    const p = !t ? 0 : v * 100 / t;
    return formatValue(flt(p), `.${d}f`) + '%';
  },

  // ----- tiempo y tareas
  time: c => flt(c.vm.time),
  dt: c => flt(c.vm.dt),
  frame: c => c.vm.frame,
  wait: (c, a) => {
    const until = c.vm.time + Math.max(0, secs(a[0])), first = c.vm.frame;
    return new Block(vm => [vm.frame > first && vm.time >= until - 1e-9, null], 'wait');
  },
  wait_frames: (c, a) => {
    const target = c.vm.frame + Math.max(1, Math.trunc(secs(a[0], 'fotogramas')));
    return new Block(vm => [vm.frame >= target, null], 'wait_frames');
  },
  wait_press: (c, a) => {
    const [timeout, player, ...buttons] = a;
    const p = int(player);
    const until = isNum(timeout) && raw(timeout) >= 0 ? c.vm.time + secs(timeout) : null, first = c.vm.frame;
    return new Block(vm => {
      if (vm.frame > first) {
        for (const b of (buttons.length ? buttons : [...vm.pad.buttons(p)].sort())) if (vm.pad.justPressed(b, p)) return [true, b];
      }
      if (until !== null && vm.time >= until) return [true, null];
      return [false, null];
    }, 'wait_press');
  },
  wait_release: (c, a) => { const b = a[0], p = int(a[1]); return new Block(vm => [!vm.pad.pressed(b, p), null], 'wait_release'); },
  wait_until: (c, a) => {
    const fn = fnref(a[0]), timeout = a[1];
    const until = isNum(timeout) && raw(timeout) >= 0 ? c.vm.time + secs(timeout) : null;
    return new Block(vm => {
      if (truthy(vm.callNow(fn, []))) return [true, true];
      if (until !== null && vm.time >= until) return [true, false];
      return [false, null];
    }, 'wait_until');
  },
  spawn: (c, a) => c.vm.spawn(fnref(a[0]), a.slice(1)),
  after: (c, a) => c.vm.spawn(fnref(a[1]), a.slice(2), secs(a[0])),
  every_seconds: (c, a) => c.vm.spawn(fnref(a[1]), a.slice(2), 0, Math.max(0, secs(a[0]))),
  stop: (c, a) => { if (a[0] !== null) c.vm.stopTask(task(a[0])); },
  stop_all: c => { c.vm.stopAll(c.task); },
  'Task.stop': (c, a) => { c.vm.stopTask(task(a[0])); },
  'Task.join': (c, a) => {
    const t = task(a[0]), timeout = a[1];
    const until = isNum(timeout) && raw(timeout) >= 0 ? c.vm.time + secs(timeout) : null;
    return new Block(vm => {
      if (!vm.taskRunning(t)) return [true, vm.taskResult(t)];
      if (until !== null && vm.time >= until) return [true, null];
      return [false, null];
    }, 'join');
  },

  // ----- mando
  'pad.pressed': (c, a) => c.vm.pad.pressed(button(a[0]), int(a[1])),
  'pad.just_pressed': (c, a) => c.vm.pad.justPressed(button(a[0]), int(a[1])),
  'pad.just_released': (c, a) => c.vm.pad.justReleased(button(a[0]), int(a[1])),
  'pad.held_time': (c, a) => flt(c.vm.pad.heldTime(button(a[0]), int(a[1]))),
  'pad.any_pressed': (c, a) => c.vm.pad.buttons(int(a[0])).size > 0,
  'pad.buttons': (c, a) => [...c.vm.pad.buttons(int(a[0]))].sort(),
  'pad.frets': (c, a) => { const on = c.vm.pad.buttons(int(a[0])); return FRETS.filter(f => on.has(f)); },
  'pad.chord': (c, a) => {
    const p = int(a[0]), want = new Set(a.slice(1).map(button));
    const on = FRETS.filter(f => c.vm.pad.pressed(f, p));
    return on.length === want.size && on.every(f => want.has(f));
  },
  'pad.strum': (c, a) => { const p = int(a[0]); return c.vm.pad.justPressed('strum_up', p) ? -1 : c.vm.pad.justPressed('strum_down', p) ? 1 : 0; },
  'pad.whammy': (c, a) => flt(c.vm.pad.axis('whammy', int(a[0]))),
  'pad.tilt': (c, a) => flt(c.vm.pad.axis('tilt', int(a[0]))),
  'pad.axis': (c, a) => flt(c.vm.pad.axis(toText(a[0]), int(a[1]))),
  'pad.connected': (c, a) => c.vm.pad.connected(int(a[0])),
  'pad.kind': (c, a) => c.vm.pad.kind(int(a[0])),
  'pad.vibrate': (c, a) => { c.vm.host.vibrate(int(a[2]), n(a[0]), n(a[1])); },

  // ----- capas
  layer: (c, a) => {
    if (typeof a[0] !== 'string') fault(`layer() necesita el nombre de la capa (texto), no ${typeName(a[0])}`);
    c.vm.checkLayer(a[0]); return new Layer(a[0]);
  },
  layers: (c, a) => { const p = truthy(a[0]) ? toText(a[0]) : ''; return c.vm.host.layerNames().filter(x => x.startsWith(p)).map(x => new Layer(x)); },
  exists: (c, a) => c.vm.host.layerNames().includes(a[0]),
  'Layer.show': (c, a) => {
    const nm = L(a[0]), t = lnum(a[1], 'time');
    c.vm.layerSet(nm, 'visible', true);
    if (t > 0) { c.vm.layerSet(nm, 'alpha', flt(0)); c.vm.anim.tween(nm, 'alpha', flt(1), t, 'out'); }
  },
  'Layer.hide': (c, a) => {
    const nm = L(a[0]), t = lnum(a[1], 'time');
    if (t > 0) c.vm.anim.tween(nm, 'alpha', flt(0), t, 'in', 0, 0, false, vm => vm.layerSet(nm, 'visible', false));
    else c.vm.layerSet(nm, 'visible', false);
  },
  'Layer.toggle': (c, a) => { const nm = L(a[0]); c.vm.layerSet(nm, 'visible', !truthy(c.vm.layerGet(nm, 'visible'))); },
  'Layer.fade_in': (c, a) => {
    const nm = L(a[0]), t = lnum(a[1], 'time');
    if (!truthy(c.vm.layerGet(nm, 'visible'))) c.vm.layerSet(nm, 'alpha', flt(0));
    c.vm.layerSet(nm, 'visible', true);
    return maybeWait(c, [c.vm.anim.tween(nm, 'alpha', flt(1), t, 'out')], a[2]);
  },
  'Layer.fade_out': (c, a) => {
    const nm = L(a[0]), t = lnum(a[1], 'time');
    const i = c.vm.anim.tween(nm, 'alpha', flt(0), t, 'in', 0, 0, false, vm => vm.layerSet(nm, 'visible', false));
    return maybeWait(c, [i], a[2]);
  },
  'Layer.set': (c, a, kw) => { const nm = L(a[0]); for (const [k, v] of Object.entries(kw)) c.vm.layerSet(nm, k, v); },
  'Layer.get': (c, a) => c.vm.layerGet(L(a[0]), toText(a[1])),
  'Layer.reset': (c, a) => {
    const nm = L(a[0]), t = lnum(a[1], 'time');
    c.vm.anim.stop(nm);
    const design = c.vm.host.layerDesign(nm) || {};
    for (const [k, d] of Object.entries(DEFAULTS_)) {
      const v = k in design ? design[k] : d;
      if (t > 0 && ANIM_.has(k)) c.vm.anim.tween(nm, k, v, t, 'out'); else c.vm.layerSet(nm, k, v);
    }
  },
  'Layer.animate': (c, a, kw) => {
    const nm = L(a[0]);
    const [t, ez, delay, loop, yoyo, wait] = a.slice(1, 7);
    if (!Object.keys(kw).length) fault('animate() sin propiedades: p. ej. animate(x=300, alpha=0.5, time=0.4)');
    const ids = [];
    for (const [k, v] of Object.entries(kw)) {
      if (!ANIM_.has(k)) fault(`la propiedad '${k}' no se puede animar` + (k in DEFAULTS_ ? ' (usa set())' : ''));
      ids.push(c.vm.anim.tween(nm, k, v, lnum(t, 'time'), toText(ez), lnum(delay, 'delay'), Math.trunc(lnum(loop, 'loop')), truthy(yoyo)));
    }
    return maybeWait(c, ids, wait);
  },
  'Layer.stop': (c, a) => { c.vm.anim.stop(L(a[0])); },
  'Layer.move_to': (c, a) => to(c, L(a[0]), { x: flt(lnum(a[1], 'x')), y: flt(lnum(a[2], 'y')) }, a[3], a[4], a[5]),
  'Layer.move_by': (c, a) => {
    const nm = L(a[0]), x = raw(c.vm.anim.target(nm, 'x')), y = raw(c.vm.anim.target(nm, 'y'));
    return to(c, nm, { x: flt(x + lnum(a[1], 'dx')), y: flt(y + lnum(a[2], 'dy')) }, a[3], a[4], a[5]);
  },
  'Layer.scale_to': (c, a) => to(c, L(a[0]), { scale: flt(lnum(a[1], 'scale')) }, a[2], a[3], a[4]),
  'Layer.rotate_to': (c, a) => to(c, L(a[0]), { rot: flt(lnum(a[1], 'degrees')) }, a[2], a[3], a[4]),
  'Layer.glow': (c, a) => {
    const nm = L(a[0]);
    c.vm.layerSet(nm, 'glow_color', color(a[2]));
    fx(c, nm, { glow: flt(lnum(a[1], 'size')), glow_alpha: flt(lnum(a[3], 'alpha')) }, a[4]);
  },
  'Layer.unglow': (c, a) => fx(c, L(a[0]), { glow: flt(0) }, a[1]),
  'Layer.blur': (c, a) => fx(c, L(a[0]), { blur: flt(Math.max(0, lnum(a[1], 'px'))) }, a[2]),
  'Layer.shadow': (c, a) => {
    const nm = L(a[0]);
    c.vm.layerSet(nm, 'shadow_color', color(a[2]));
    fx(c, nm, { shadow: flt(lnum(a[1], 'blur')), shadow_x: flt(lnum(a[3], 'x')), shadow_y: flt(lnum(a[4], 'y')), shadow_alpha: flt(lnum(a[5], 'alpha')) }, a[6]);
  },
  'Layer.outline': (c, a) => {
    const nm = L(a[0]);
    c.vm.layerSet(nm, 'outline_color', color(a[2]));
    fx(c, nm, { outline: flt(lnum(a[1], 'width')) }, a[3]);
  },
  'Layer.tint': (c, a) => fx(c, L(a[0]), { tint: color(a[1]) }, a[2]),
  'Layer.untint': (c, a) => fx(c, L(a[0]), { tint: '#ffffff' }, a[1]),
  'Layer.pulse': (c, a) => {
    const nm = L(a[0]), prop = toText(a[1]);
    if (!ANIM_.has(prop) || ['tint', 'glow_color', 'shadow_color', 'outline_color'].includes(prop)) fault(`pulse(): '${prop}' no es una propiedad numerica animable`);
    const amount = lnum(a[2], 'amount'), period = lnum(a[3], 'period'), times = Math.trunc(lnum(a[4], 'times'));
    const base = raw(c.vm.anim.target(nm, prop));
    const loops = times < 0 ? -1 : Math.max(0, times * 2 - 1);
    c.vm.anim.tween(nm, prop, flt(base + amount), Math.max(0.02, period / 2), 'inout', 0, loops, true);
  },
  'Layer.shake': (c, a) => {
    const nm = L(a[0]), amount = lnum(a[1], 'amount'), t = lnum(a[2], 'time');
    for (const [prop, phase] of [['x', 0], ['y', 1.7]]) {
      const base = c.vm.anim.target(nm, prop), b = raw(base);
      c.vm.anim.tween(nm, prop, base, t, 'linear', 0, 0, false, null, u => flt(b + Math.sin(u * 50 + phase) * amount * (1 - u)));
    }
  },
  'Layer.flash': (c, a) => {
    const nm = L(a[0]), t = lnum(a[2], 'time');
    c.vm.layerSet(nm, 'glow_color', color(a[1]));
    c.vm.layerSet(nm, 'glow', flt(Math.max(24, raw(c.vm.layerGet(nm, 'glow') ?? 0) || 0)));
    c.vm.layerSet(nm, 'glow_alpha', flt(1));
    c.vm.layerSet(nm, 'brightness', flt(1.8));
    c.vm.anim.tween(nm, 'glow_alpha', flt(0), t, 'out');
    c.vm.anim.tween(nm, 'brightness', flt(1), t, 'out');
  },
  'Layer.bounce': (c, a) => {
    const nm = L(a[0]), h = lnum(a[1], 'height'), t = lnum(a[2], 'time');
    const base = c.vm.anim.target(nm, 'y'), b = raw(base);
    c.vm.anim.tween(nm, 'y', base, t, 'linear', 0, 0, false, null, u => flt(b - Math.sin(u * Math.PI) * h));
  },
  'Layer.spin': (c, a) => {
    const nm = L(a[0]), speed = lnum(a[1], 'speed');
    c.vm.anim.stop(nm, ['rot']);
    if (speed) {
      const b = raw(c.vm.layerGet(nm, 'rot') ?? 0) || 0;
      c.vm.anim.tween(nm, 'rot', flt(b), 1, 'linear', 0, -1, false, null, u => flt(b + speed * u), true);
    }
  },
  'Layer.set_text': (c, a) => { c.vm.layerSet(L(a[0]), 'text', toText(a[1])); },
  'Layer.set_image': (c, a) => { c.vm.layerSet(L(a[0]), 'image', toText(a[1])); },
  'Layer.focus': (c, a) => { c.vm.setFocus(L(a[0])); },
  'Layer.contains': (c, a) => !!c.vm.host.layerContains(L(a[0]), lnum(a[1], 'x'), lnum(a[2], 'y')),
  'Layer.busy': (c, a) => c.vm.anim.busy(L(a[0])),

  // ----- pantallas, foco y raton
  goto: (c, a) => { if (typeof a[0] !== 'string') fault(`goto() necesita el nombre de la pantalla, no ${typeName(a[0])}`); c.vm.goto(a[0], toText(a[1])); },
  back: (c, a) => { c.vm.back(toText(a[0])); },
  close: c => { c.vm.close(); },
  screen: c => c.vm.screen,
  screens: c => [...c.vm.host.screenNames()],
  history: c => [...c.vm.history],
  focus: (c, a) => {
    let t = a[0];
    if (t instanceof Layer) t = t.name;
    if (t !== null && typeof t !== 'string') fault(`focus() necesita una capa o su nombre, no ${typeName(t)}`);
    c.vm.setFocus(t);
  },
  focused: c => c.vm.focused ? new Layer(c.vm.focused) : null,
  focus_move: (c, a) => {
    const d = toText(a[0]).toLowerCase();
    if (!['up', 'down', 'left', 'right'].includes(d)) fault(`focus_move(): direccion '${d}' (usa up, down, left o right)`);
    c.vm.host.moveFocus(d);
  },
  lock_focus: (c, a) => { c.vm.focusLocked = truthy(a[0]); c.vm.host.lockFocus(c.vm.focusLocked); },
  mouse_x: c => c.vm.mouse[0],
  mouse_y: c => c.vm.mouse[1],
  mouse_down: c => !!c.vm.mouse[2],
  hovered: c => { const nm = c.vm.host.layerAt(c.vm.mouse[0], c.vm.mouse[1]); return nm ? new Layer(nm) : null; },

  // ----- eventos y sonido
  emit: (c, a) => { c.vm.emit(toText(a[0]), a[1]); },
  sound: (c, a) => {
    const nm = toText(a[0]);
    if (!SOUNDS.has(nm)) c.vm.log('warn', `sound(): '${nm}' no es un sonido conocido del menu`);
    c.vm.host.playSound(nm, n(a[1]));
  },

  // ----- juego
  'game.get': (c, a) => { const v = c.vm.host.gameGet(gname(a[0], 'game.get')); return v === null || v === undefined ? a[1] : v; },
  'game.set': (c, a) => {
    const v = a[1];
    if (!(isNum(v) || typeof v === 'string' || typeof v === 'boolean')) fault(`game.set(): solo numeros, textos o bool (llego ${typeName(v)})`);
    c.vm.host.gameSet(gname(a[0], 'game.set'), v);
  },
  'game.call': (c, a, kw) => c.vm.host.gameCall(gname(a[0], 'game.call'), { ...kw }) ?? null,
  'game.player': (c, a) => {
    const f = toText(a[0]);
    if (!TABLES.player_fields.includes(f)) fault(`game.player(): campo '${f}' (hay: ${TABLES.player_fields.join(', ')})`);
    return c.vm.host.playerInfo(int(a[1]), f) ?? null;
  },
  'game.band': (c, a) => {
    const f = toText(a[0]);
    if (!TABLES.band_fields.includes(f)) fault(`game.band(): campo '${f}' (hay: ${TABLES.band_fields.join(', ')})`);
    return c.vm.host.bandInfo(f) ?? null;
  },
  'game.open_menu': (c, a) => { c.vm.host.gameCall('ui_event', { event: 'menu_change', data: { state: gname(a[0], 'game.open_menu') } }); },
  'game.in_song': c => truthy(c.vm.host.gameState('in_song')),
  'game.paused': c => truthy(c.vm.host.gameState('paused')),
  'game.language': c => toText(c.vm.host.gameState('language') || 'en'),

  // ----- metodos de listas
  'list.append': (c, a) => { mutable(a[0]).push(a[1]); },
  'list.insert': (c, a) => { const x = mutable(a[0]); x.splice(pyIndex(a[1], x.length), 0, a[2]); },
  'list.pop': (c, a) => {
    const x = mutable(a[0]);
    if (!x.length) fault('pop() en una lista vacia');
    const i = int(a[1]);
    if (!(-x.length <= i && i < x.length)) fault(`pop(${i}): la lista tiene ${x.length} elementos`);
    return x.splice(i < 0 ? i + x.length : i, 1)[0];
  },
  'list.remove': (c, a) => { const x = mutable(a[0]); const i = x.findIndex(v => eq(v, a[1])); if (i < 0) return false; x.splice(i, 1); return true; },
  'list.clear': (c, a) => { mutable(a[0]).length = 0; },
  'list.index': (c, a) => lst(a[0]).findIndex(v => eq(v, a[1])),
  'list.count': (c, a) => lst(a[0]).filter(v => eq(v, a[1])).length,
  'list.contains': (c, a) => lst(a[0]).some(v => eq(v, a[1])),
  'list.reverse': (c, a) => { mutable(a[0]).reverse(); },
  'list.sort': (c, a) => { sortList(mutable(a[0]), truthy(a[1])); },
  'list.copy': (c, a) => [...lst(a[0])],
  'list.join': (c, a) => { if (typeof a[1] !== 'string') fault('join() necesita un texto como separador'); return lst(a[0]).map(toText).join(a[1]); },

  // ----- metodos de textos
  'str.upper': (c, a) => a[0].toUpperCase(),
  'str.lower': (c, a) => a[0].toLowerCase(),
  'str.strip': (c, a) => a[0].replace(/^\s+|\s+$/g, ''),
  'str.replace': (c, a) => a[0].split(toText(a[1])).join(toText(a[2])),
  'str.split': (c, a) => truthy(a[1]) ? a[0].split(a[1]) : a[0].split(/\s+/).filter(Boolean),
  'str.startswith': (c, a) => a[0].startsWith(toText(a[1])),
  'str.endswith': (c, a) => a[0].endsWith(toText(a[1])),
  'str.find': (c, a) => { const i = a[0].indexOf(toText(a[1])); return i < 0 ? -1 : pyStr(a[0].slice(0, i)).length; },
  'str.contains': (c, a) => a[0].includes(toText(a[1])),
  'str.pad': (c, a) => {
    const [s, width, fill] = a;
    const ch = (pyStr(toText(fill))[0]) || ' ';
    const w = int(width), len = pyStr(s).length;
    return len >= w ? s : ch.repeat(w - len) + s;
  },
};

function many(a, sgn) {
  const vals = a.length === 1 && Array.isArray(a[0]) ? a[0] : a;
  if (!vals.length) fault('min()/max() sin valores');
  let best = num(vals[0]);
  for (const v of vals.slice(1)) { const x = num(v); if (sgn > 0 ? raw(x) > raw(best) : raw(x) < raw(best)) best = x; }
  return typeof best === 'boolean' ? (best ? 1 : 0) : best;
}

function arith(x, y, t, f) {
  const fl = [x, y, t].some(v => v instanceof F);
  const r = f(raw(x), raw(y), raw(t));
  return fl || !Number.isInteger(r) ? flt(r) : r;
}

function gname(v, what) {
  if (typeof v !== 'string' || !v) fault(`${what}: se esperaba un nombre (texto) y llego ${typeName(v)}`);
  return v.replace(/^\$+/, '');
}

const DEFAULTS_ = {};
const ANIM_ = new Set();
for (const [k, [type, writable, anim, d]] of Object.entries(TABLES.props)) {
  if (writable) DEFAULTS_[k] = d && typeof d === 'object' && '$f' in d ? new F(d.$f) : d;
  if (anim) ANIM_.add(k);
  void type;
}
void same; void isNumB; void eq;
