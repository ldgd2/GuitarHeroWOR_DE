// Maquina de wui en JS: la vista previa del editor ejecuta aqui el mismo programa (.wuib) que el juego.
// Es una traduccion directa de tools/wui/wui/runtime (Python, la de referencia); re-wor tiene la de C++.
// Las pruebas de tools/wui/tests/conformance comparan las tres.
import {
  F, Layer, FuncRef, TaskRef, Block, Fault, fault, isNum, isNumB, raw, mk, typeName, receiverOf, toText, truthy,
  eq, cmp, formatValue, parseColor, colorHex, ease, PyRandom, OP, NO_FUNC, lineAt, tv, roundHalfEven,
} from './core.js';
import TABLES from './tables.js';
import { STDLIB } from './stdlib.js';

const UNSET = Symbol('unset');
const MAX_DEPTH = 200;

/** Contrato con quien dibuja (como runtime/host/protocol.py). Todo es opcional. */
export class Host {
  layerNames() { return []; }
  layerGet(_n, _p) { return null; }
  layerSet(_n, _p, _v) {}
  layerDesign(_n) { return {}; }
  layerContains(_n, _x, _y) { return false; }
  layerAt(_x, _y) { return null; }
  screenNames() { return []; }
  showScreen(_n, _tr, _prev) {}
  close() {}
  setFocus(_n) {}
  moveFocus(_d) {}
  lockFocus(_l) {}
  gameGet(_n) { return null; }
  gameSet(_n, _v) {}
  gameCall(_s, _p) { return null; }
  playerInfo(_p, _f) { return null; }
  bandInfo(_f) { return null; }
  gameState(_w) { return null; }
  playSound(_n, _v) {}
  vibrate(_p, _s, _t) {}
  log(_lvl, _m) {}
}

export const PROPS = Object.fromEntries(Object.entries(TABLES.props).map(([k, [type, writable, anim, dflt]]) =>
  [k, { type, writable, anim, dflt: tv(dflt) }]));
export const DEFAULTS = Object.fromEntries(Object.entries(PROPS).filter(([, p]) => p.writable).map(([k, p]) => [k, p.dflt]));
export const ANIMATABLE = new Set(Object.entries(PROPS).filter(([, p]) => p.anim).map(([k]) => k));
export const COLORS = new Set(Object.entries(PROPS).filter(([, p]) => p.type === 'color').map(([k]) => k));
const METHODS = Object.fromEntries(Object.entries(TABLES.methods).map(([k, ps]) =>
  [k, ps.map(([name, kind, has, d]) => ({ name, kind, has, dflt: tv(d) }))]));

// ================================================================== entrada
export class Inputs {
  constructor(buttons = {}, axes = {}, mouse = [0, 0, false], kinds = {}) {
    this.buttons = buttons; this.axes = axes; this.mouse = mouse; this.kinds = kinds;
  }
  static of(...buttons) { return new Inputs({ 1: new Set(buttons) }); }
}

class PadState {
  constructor() { this.now = new Inputs(); this.prev = new Inputs(); this.since = new Map(); this.time = 0; }
  update(inp, time) {
    this.prev = this.now; this.now = inp; this.time = time;
    const pressed = [], released = [];
    const players = [...new Set([...Object.keys(inp.buttons), ...Object.keys(this.prev.buttons)].map(Number))].sort((a, b) => a - b);
    for (const p of players) {
      const cur = inp.buttons[p] || new Set(), old = this.prev.buttons[p] || new Set();
      for (const b of [...cur].filter(b => !old.has(b)).sort()) { this.since.set(p + ':' + b, time); pressed.push([p, b]); }
      for (const b of [...old].filter(b => !cur.has(b)).sort()) { this.since.delete(p + ':' + b); released.push([p, b]); }
    }
    return [pressed, released];
  }
  buttons(p = 1) { return new Set(this.now.buttons[p] || []); }
  pressed(b, p = 1) { return !!this.now.buttons[p]?.has(b); }
  justPressed(b, p = 1) { return !!this.now.buttons[p]?.has(b) && !this.prev.buttons[p]?.has(b); }
  justReleased(b, p = 1) { return !this.now.buttons[p]?.has(b) && !!this.prev.buttons[p]?.has(b); }
  heldTime(b, p = 1) { const t = this.since.get(p + ':' + b); return t === undefined ? 0 : Math.max(0, this.time - t); }
  axis(n, p = 1) { return +(this.now.axes[p]?.[n] ?? 0); }
  connected(p = 1) { return p in this.now.buttons || p in this.now.kinds || p === 1; }
  kind(p = 1) { return this.now.kinds[p] || 'guitar'; }
}

// ================================================================== animaciones
class Animator {
  constructor(vm) { this.vm = vm; this.tweens = []; this.next = 1; }
  tween(layer, prop, to, time, easeName = 'out', delay = 0, loop = 0, yoyo = false, onEnd = null, fn = null, accumulate = false) {
    this.stop(layer, [prop]);
    const tw = { id: this.next++, layer, prop, start: null, end: to, duration: Math.max(0, +time), ease: easeName || 'out',
      delay: Math.max(0, +delay), loops: Math.trunc(loop), yoyo: !!yoyo, t0: this.vm.time, fn, accumulate, onEnd, cycle: 0, done: false, started: false };
    this.tweens.push(tw);
    if (tw.duration <= 0 && tw.delay <= 0 && !fn) {
      this.vm.layerSet(layer, prop, to, true);
      tw.done = true; this.tweens.splice(this.tweens.indexOf(tw), 1);
      onEnd?.(this.vm);
    }
    return tw.id;
  }
  value(tw, u) {
    if (tw.fn) return tw.fn(tw.accumulate ? tw.cycle + u : u);
    const k = ease(tw.ease, u), a = tw.start, b = tw.end;
    if (isNum(a) && isNum(b)) return new F(raw(a) + (raw(b) - raw(a)) * k);
    const ca = parseColor(a), cb = parseColor(b);
    if (ca && cb) return colorHex(ca.map((x, i) => roundHalfEven(x + (cb[i] - x) * k)));
    return k >= 1 ? b : a;
  }
  update() {
    const now = this.vm.time;
    for (const tw of [...this.tweens]) {
      if (tw.done) continue;
      const t = now - tw.t0 - tw.delay;
      if (t < 0) continue;
      if (!tw.started) {
        tw.started = true;
        if (tw.start === null) { tw.start = this.vm.layerGet(tw.layer, tw.prop); if (tw.start === null) tw.start = tw.end; }
      }
      const dur = tw.duration > 0 ? tw.duration : 1e-6;
      let cycle = Math.floor(t / dur), u, finished;
      if (tw.loops >= 0 && cycle > tw.loops) { u = 1; finished = true; cycle = tw.loops; }
      else { u = (t - cycle * dur) / dur; finished = false; }
      tw.cycle = cycle;
      if (tw.yoyo && cycle % 2 === 1) u = 1 - u;
      const val = finished && tw.fn && !tw.accumulate ? (tw.yoyo && tw.loops % 2 === 1 ? tw.start : tw.fn(1)) : this.value(tw, u);
      this.vm.layerSet(tw.layer, tw.prop, val, true);
      if (finished) { tw.done = true; this.tweens.splice(this.tweens.indexOf(tw), 1); tw.onEnd?.(this.vm); }
    }
  }
  stop(layer, props = null) { this.tweens = this.tweens.filter(t => !(t.layer === layer && (props === null || props.includes(t.prop)))); }
  busy(layer) { return this.tweens.some(t => t.layer === layer); }
  target(layer, prop) {
    for (let i = this.tweens.length - 1; i >= 0; i--) {
      const t = this.tweens[i];
      if (t.layer === layer && t.prop === prop && !t.fn && t.loops === 0) return t.end;
    }
    const v = this.vm.layerGet(layer, prop);
    return v === null ? new F(0) : v;
  }
  waitFor(ids) { const want = new Set(ids); return new Block(() => [!this.tweens.some(t => want.has(t.id)), null], 'animate'); }
}

// ================================================================== maquina
function near(word, options) {
  // como difflib.get_close_matches(word, options, 1, 0.6) aproximado: distancia de edicion relativa
  let best = null, score = 0;
  for (const o of options) {
    const d = lev(word, o), s = 1 - d / Math.max(word.length, o.length, 1);
    if (s > score) { score = s; best = o; }
  }
  return score >= 0.6 ? best : null;
}
function lev(a, b) {
  const m = a.length, n = b.length, d = Array.from({ length: m + 1 }, (_, i) => [i, ...Array(n).fill(0)]);
  for (let j = 1; j <= n; j++) d[0][j] = j;
  for (let i = 1; i <= m; i++) for (let j = 1; j <= n; j++)
    d[i][j] = Math.min(d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + (a[i - 1] === b[j - 1] ? 0 : 1));
  return d[m][n];
}

export class Machine {
  constructor(program, host = new Host(), { seed = 0, budget = 200000, maxTasks = 256 } = {}) {
    this.prog = program; this.host = host; this.budget = budget; this.maxTasks = maxTasks;
    this.rng = new PyRandom(seed);
    this.builtins = program.builtins.map(n => {
      const f = STDLIB[n];
      if (!f) throw new Error(`el programa usa '${n}', que esta maquina no tiene`);
      return f;
    });
    this.globals = program.globals.map(() => null);
    this.gindex = Object.fromEntries(program.globals.map((n, i) => [n, i]));
    this.time = 0; this.dt = 0; this.frame = 0;
    this.pad = new PadState(); this.anim = new Animator(this);
    this.mouse = [0, 0, false];
    this.screen = null; this.history = []; this.focused = null; this.hovered = null; this.focusLocked = false;
    this.closed = false; this.started = false;
    this.tasks = []; this.taskId = 0; this.errors = []; this.logs = [];
    this.holds = new Set(); this.repeat = new Map(); this.everyAt = new Map(); this.gameLast = new Map();
    this.watch = new Set(program.handlers.filter(h => h.event === 'on_change' && h.args.length).map(h => h.args[0]));
  }

  // ------------------------------------------------------------------ ciclo de vida
  start(screen = null) {
    if (this.started) return;
    this.started = true;
    if (this.prog.init !== NO_FUNC) {
      const w = this.watch; this.watch = new Set();
      try { this.callSync(this.prog.init, [], 'el codigo de arriba del todo'); }
      catch (e) { if (!e.wui) throw e; this.error(e.message, e.fn || INIT, e.line || 0, []); }
      finally { this.watch = w; }
    }
    this.fire('on_start', []);
    const names = this.host.screenNames();
    const first = screen || names[0] || null;
    if (first) {
      this.screen = first;
      this.host.showScreen(first, 'auto', null);
      this.fire('on_enter', [first], h => h.args[0] === null || h.args[0] === first);
    }
    this.runTasks();
  }

  step(dt, inputs = null) {
    if (!this.started) this.start();
    this.dt = Math.max(0, +dt); this.time += this.dt; this.frame++;
    inputs = inputs || new Inputs();
    this.mouse = inputs.mouse;
    const [pressed, released] = this.pad.update(inputs, this.time);
    if (!this.closed) {
      this.inputEvents(pressed, released);
      this.timers();
      this.fire('on_tick', [new F(this.dt)]);
      this.gameWatch();
    }
    this.runTasks();
    this.anim.update();
  }

  // ------------------------------------------------------------------ eventos
  fire(event, args, match = null) {
    let n = 0;
    for (const h of this.prog.handlers) if (h.event === event && (!match || match(h))) { this.spawnHandler(h, args); n++; }
    return n;
  }
  spawnHandler(h, args) {
    const fn = this.prog.functions[h.func];
    try { this.newTask(h.func, this.fill(fn, args.slice(0, fn.nparams)), `@${h.event} ${fn.name}`); }
    catch (e) { if (!e.wui) throw e; this.error(e.message, fn.name, 0, []); }
  }
  inputEvents(pressed, released) {
    const H = this.prog.handlers;
    for (const [p, b] of pressed) {
      this.fire('on_press', [b, p], h => (h.args[0] === null || h.args[0] === b) && h.args[1] === p);
      if (b === 'strum_up' || b === 'strum_down') this.fire('on_strum', [b === 'strum_up' ? -1 : 1, p], h => h.args[0] === p);
      H.forEach((h, i) => {
        if (h.event === 'on_repeat' && h.args[0] === b && h.args[3] === p) {
          this.spawnHandler(h, [b, p]); this.repeat.set(i + ':' + p, this.time + raw(h.args[2]));
        }
      });
      if (b === 'choose' && this.focused && !this.focusLocked) this.notifyClick(this.focused);
    }
    for (const [p, b] of released) {
      this.fire('on_release', [b, p], h => (h.args[0] === null || h.args[0] === b) && h.args[1] === p);
      for (const k of [...this.holds]) { const [i, pp] = k.split(':').map(Number); if (pp === p && H[i].args[0] === b) this.holds.delete(k); }
      for (const k of [...this.repeat.keys()]) { const [i, pp] = k.split(':').map(Number); if (pp === p && H[i].args[0] === b) this.repeat.delete(k); }
    }
    H.forEach((h, i) => {
      if (h.event !== 'on_hold') return;
      const [b, secs, p] = h.args, key = i + ':' + p;
      if (!this.holds.has(key) && this.pad.pressed(b, p) && this.pad.heldTime(b, p) >= raw(secs) - 1e-9) {
        this.holds.add(key); this.spawnHandler(h, [b, p]);
      }
    });
    for (const [k, at] of [...this.repeat]) {
      const [i, p] = k.split(':').map(Number), h = H[i];
      if (this.time >= at - 1e-9 && this.pad.pressed(h.args[0], p)) {
        this.spawnHandler(h, [h.args[0], p]);
        this.repeat.set(k, at + Math.max(0.01, raw(h.args[1])));
      }
    }
    const over = this.host.layerAt(this.mouse[0], this.mouse[1]);
    if (over !== this.hovered) {
      this.hovered = over;
      if (over) this.fire('on_hover', [new Layer(over)], h => h.args[0] === null || h.args[0] === over);
    }
  }
  timers() {
    this.prog.handlers.forEach((h, i) => {
      if (h.event !== 'every') return;
      const secs = Math.max(0.001, raw(h.args[0]));
      if (!this.everyAt.has(i)) this.everyAt.set(i, secs);
      const nxt = this.everyAt.get(i);
      if (this.time >= nxt - 1e-9) { this.everyAt.set(i, nxt + secs); this.spawnHandler(h, []); }
    });
  }
  gameWatch() {
    for (const h of this.prog.handlers) {
      if (h.event !== 'on_game') continue;
      const v = h.args[0], val = this.host.gameGet(String(v).replace(/^\$+/, ''));
      if (this.gameLast.has(v) && !eq(this.gameLast.get(v), val)) this.spawnHandler(h, [val, this.gameLast.get(v)]);
      this.gameLast.set(v, val);
    }
  }
  notifyFocus(name) {
    if (name === this.focused) return;
    const old = this.focused; this.focused = name;
    if (old) this.fire('on_blur', [new Layer(old)], h => h.args[0] === null || h.args[0] === old);
    if (name) this.fire('on_focus', [new Layer(name)], h => h.args[0] === null || h.args[0] === name);
  }
  notifyClick(name) { this.fire('on_click', [new Layer(name)], h => h.args[0] === null || h.args[0] === name); }

  // ------------------------------------------------------------------ api para la libreria
  fault(m) { fault(m); }
  log(level, message) { this.logs.push([level, message]); this.host.log(level, message); }
  checkLayer(name) {
    const names = this.host.layerNames();
    if (names.length && !names.includes(name)) {
      const n = near(name, names);
      fault(`no hay ninguna capa llamada '${name}'` + (n ? ` (quisiste decir '${n}'?)` : ''));
    }
  }
  layerGet(name, prop) {
    if (prop === 'focused') return this.focused === name;
    if (prop === 'animating') return this.anim.busy(name);
    if (!PROPS[prop]) fault(`las capas no tienen la propiedad '${prop}'`);
    const v = this.host.layerGet(name, prop);
    return v === null || v === undefined ? (DEFAULTS[prop] ?? null) : v;
  }
  layerSet(name, prop, value, fromAnim = false) {
    const spec = PROPS[prop];
    if (!spec) fault(`las capas no tienen la propiedad '${prop}'`);
    if (!spec.writable) fault(`la propiedad '${prop}' de las capas solo se puede leer`);
    if (COLORS.has(prop)) {
      const rgb = parseColor(value);
      if (!rgb) fault(`${prop}: '${toText(value)}' no es un color (usa "#ff0000", "red" o [255, 0, 0])`);
      value = colorHex(rgb);
    } else if (spec.type === 'numero') {
      if (typeof value === 'boolean' || !isNum(value)) fault(`${prop}: se esperaba un numero y llego ${typeName(value)} (${toText(value)})`);
      value = new F(raw(value));
    } else if (spec.type === 'bool') value = truthy(value);
    else if (spec.type === 'texto') value = toText(value);
    if (!fromAnim) this.anim.stop(name, [prop]);
    this.host.layerSet(name, prop, value);
  }
  goto(name, transition = 'auto', push = true) {
    const names = this.host.screenNames();
    if (names.length && !names.includes(name)) {
      const n = near(name, names);
      fault(`no hay ninguna pantalla llamada '${name}'` + (n ? ` (quisiste decir '${n}'?)` : ''));
    }
    if (name === this.screen) return;
    const old = this.screen;
    if (old) { this.fire('on_exit', [old], h => h.args[0] === null || h.args[0] === old); if (push) this.history.push(old); }
    this.screen = name; this.focused = null;
    this.host.showScreen(name, transition, old);
    this.fire('on_enter', [name], h => h.args[0] === null || h.args[0] === name);
  }
  back(transition = 'auto') { if (this.history.length) this.goto(this.history.pop(), transition, false); else this.close(); }
  close() {
    if (this.closed) return;
    this.closed = true;
    if (this.screen) { const old = this.screen; this.fire('on_exit', [old], h => h.args[0] === null || h.args[0] === old); }
    this.fire('on_close', []);
    this.host.close();
  }
  setFocus(name) { if (name) this.checkLayer(name); this.host.setFocus(name); this.notifyFocus(name); }
  emit(name, value) { this.fire('on_message', [value], h => h.args[0] === name); }

  spawn(fn, args, delay = 0, every = null) {
    const f = this.prog.functions[fn.index];
    const t = this.newTask(fn.index, this.fill(f, args), f.name);
    t.startAt = this.time + Math.max(0, delay);
    t.every = every;
    if (every !== null && delay <= 0) t.startAt = this.time + every;
    return t.ref;
  }
  stopTask(ref) { for (const t of this.tasks) if (t.ref === ref) { t.done = true; t.stopped = true; } }
  stopAll(except) { for (const t of this.tasks) if (t !== except) { t.done = true; t.stopped = true; } }
  taskRunning(ref) { return this.tasks.some(t => t.ref === ref && !t.done); }
  taskResult(ref) { return ref.result ?? null; }
  callNow(fn, args) { return this.callSync(fn.index, args, `${fn.name}()`); }
  callSync(fi, args, what) {
    const f = this.prog.functions[fi];
    const t = this.makeTask(-1, f.name, fi, this.fill(f, args));
    this.enter(t, fi, t.args);
    this.run(t);
    if (!t.done) fault(`${what} no puede esperar (wait) aqui`);
    return t.result;
  }
  fill(f, args) {
    const n = f.nparams;
    if (args.length > n) fault(`${f.name}() recibe como mucho ${n} argumento(s) y le llegan ${args.length}`);
    const missing = n - args.length;
    if (missing > f.defaults.length) fault(`${f.name}() necesita ${n - f.defaults.length} argumento(s) y le llegan ${args.length}`);
    return missing ? [...args, ...f.defaults.slice(f.defaults.length - missing)] : [...args];
  }
  makeTask(id, name, func, args) {
    return { id, name, func, args, frames: [], stack: [], block: null, pushResult: true, startAt: this.time,
      every: null, done: false, result: null, ref: new TaskRef(id, name), ran: -1, stopped: false };
  }
  newTask(fi, args, name) {
    if (this.tasks.filter(t => !t.done).length >= this.maxTasks) {
      this.error(`demasiadas tareas a la vez (${this.maxTasks}): ¿un spawn dentro de un bucle?`, name, 0, []);
      const t = this.makeTask(-1, name, fi, args); t.done = true; return t;
    }
    const t = this.makeTask(++this.taskId, name, fi, args);
    this.tasks.push(t);
    return t;
  }

  // ------------------------------------------------------------------ ejecucion
  runTasks() {
    let rounds = 0, ran = true;
    while (ran && rounds < 64) {
      ran = false; rounds++;
      for (const t of [...this.tasks]) {
        if (t.done || t.startAt > this.time + 1e-9 || t.ran === this.frame) continue;
        if (t.block) {
          let ok, val;
          try { [ok, val] = t.block.poll(this); }
          catch (e) { if (!e.wui) throw e; this.taskError(t, e); continue; }
          if (!ok) { t.ran = this.frame; continue; }
          t.block = null;
          if (t.pushResult) t.stack.push(val);
        }
        if (!t.frames.length) this.enter(t, t.func, t.args);
        t.ran = this.frame; ran = true;
        this.run(t);
        if (t.done && t.every !== null && !t.stopped) {
          t.done = false; t.frames = []; t.stack = []; t.startAt = this.time + t.every; t.ran = this.frame;
        }
      }
    }
    for (const t of this.tasks) if (t.done) t.ref.result = t.result;
    this.tasks = this.tasks.filter(t => !t.done);
  }
  enter(t, fi, args) {
    if (t.frames.length >= MAX_DEPTH) fault('demasiadas llamadas anidadas (una funcion que se llama a si misma sin parar?)');
    const f = this.prog.functions[fi];
    const loc = [...args]; while (loc.length < f.nlocals) loc.push(UNSET);
    t.frames.push({ fi, fn: f, code: f.code, locals: loc, base: t.stack.length, pc: 0 });
  }
  taskError(t, e) {
    const fr = t.frames[t.frames.length - 1];
    const trace = t.frames.map(f => `${f.fn.name}() linea ${lineAt(f.fn, Math.max(0, f.pc - 1))}`);
    const fname = fr ? fr.fn.name : t.name, line = fr ? lineAt(fr.fn, Math.max(0, fr.pc - 1)) : 0;
    t.done = true; t.frames = [];
    this.error(e.message, fname, line, trace);
  }
  error(message, fname, line, trace) {
    this.errors.push({ message, function: fname, line, time: this.time, trace });
    this.log('error', `${message} en ${fname}()` + (line ? `, linea ${line}` : ''));
  }
  run(t) {
    try { this.exec(t); }
    catch (e) {
      if (!e.wui) throw e;
      if (t.id === -1) {
        const fr = t.frames[t.frames.length - 1];
        e.fn = fr ? fr.fn.name : t.name; e.line = fr ? lineAt(fr.fn, Math.max(0, fr.pc - 1)) : 0;
        throw e;
      }
      this.taskError(t, e);
    }
  }

  exec(t) {
    const S = t.stack, P = this.prog, K = P.consts, N = P.names, G = this.globals;
    let budget = this.budget;
    while (t.frames.length) {
      const fr = t.frames[t.frames.length - 1], code = fr.code, L = fr.locals;
      inner: for (;;) {
        if (--budget < 0) fault(`${fr.fn.name}() lleva demasiadas instrucciones sin esperar: si es un bucle, pon wait() dentro`);
        const [op, a, b, c] = code[fr.pc++];
        switch (op) {
          case OP.CONST: S.push(K[c]); break;
          case OP.LOADL: {
            const v = L[b];
            if (v === UNSET) fault(`la variable '${fr.fn.localNames?.[b] ?? '#' + b}' se usa antes de darle un valor`);
            S.push(v); break;
          }
          case OP.STOREL: L[b] = S.pop(); break;
          case OP.LOADG: S.push(G[b]); break;
          case OP.STOREG: {
            const v = S.pop(), old = G[b]; G[b] = v;
            const gname = P.globals[b];
            if (this.watch.size && this.watch.has(gname) && !(typeName(old) === typeName(v) && eq(old, v)))
              this.fire('on_change', [v, old], h => h.args[0] === gname);
            break;
          }
          case OP.JMP: fr.pc = c; break;
          case OP.JF: if (!truthy(S.pop())) fr.pc = c; break;
          case OP.JT: if (truthy(S.pop())) fr.pc = c; break;
          case OP.JFK: if (!truthy(S[S.length - 1])) fr.pc = c; else S.pop(); break;
          case OP.JTK: if (truthy(S[S.length - 1])) fr.pc = c; else S.pop(); break;
          case OP.ADD: case OP.SUB: case OP.MUL: case OP.DIV: case OP.IDIV: case OP.MOD: case OP.POW: {
            const r = S.pop(); S[S.length - 1] = binary(op, S[S.length - 1], r); break;
          }
          case OP.EQ: case OP.NE: case OP.LT: case OP.LE: case OP.GT: case OP.GE: case OP.IN: case OP.NOTIN: {
            const r = S.pop(); S[S.length - 1] = compare(op, S[S.length - 1], r); break;
          }
          case OP.NONE: S.push(null); break;
          case OP.TRUE: S.push(true); break;
          case OP.FALSE: S.push(false); break;
          case OP.POP: S.pop(); break;
          case OP.DUP: S.push(S[S.length - 1]); break;
          case OP.SWAP: { const x = S[S.length - 1]; S[S.length - 1] = S[S.length - 2]; S[S.length - 2] = x; break; }
          case OP.NEG: {
            const v = S[S.length - 1];
            if (!isNum(v)) fault(`no se puede poner '-' delante de ${typeName(v)}`);
            S[S.length - 1] = v instanceof F ? new F(-v.v) : (v === 0 ? 0 : -v); break;
          }
          case OP.POS: if (!isNum(S[S.length - 1])) fault(`no se puede poner '+' delante de ${typeName(S[S.length - 1])}`); break;
          case OP.NOT: S[S.length - 1] = !truthy(S[S.length - 1]); break;
          case OP.CALLB: {
            const impl = this.builtins[b];
            const args = S.splice(S.length - a, a);
            let kw = {};
            if (c >= 0) { const kn = K[c]; const vals = args.splice(args.length - kn.length, kn.length); kn.forEach((k, i) => { kw[k] = vals[i]; }); }
            const r = impl({ vm: this, task: t }, args, kw);
            if (r instanceof Block) { t.block = r; t.pushResult = true; return; }
            S.push(r === undefined ? null : r); break;
          }
          case OP.CALLM: {
            const r = this.callMethod(t, S, a, N[b], c >= 0 ? K[c] : []);
            if (r instanceof Block) { t.block = r; t.pushResult = true; return; }
            S.push(r === undefined ? null : r); break;
          }
          case OP.CALL: { const args = S.splice(S.length - a, a); this.enter(t, b, args); break inner; }
          case OP.CALLV: {
            const args = S.splice(S.length - a, a), fnv = S.pop();
            if (!(fnv instanceof FuncRef)) fault(`no se puede llamar a ${typeName(fnv)} (${toText(fnv)}) como si fuera una funcion`);
            this.enter(t, fnv.index, this.fill(P.functions[fnv.index], args)); break inner;
          }
          case OP.FREF: S.push(new FuncRef(b, P.functions[b].name)); break;
          case OP.RET: case OP.RETN: {
            const v = op === OP.RET ? S.pop() : null;
            t.frames.pop(); S.length = fr.base;
            if (t.frames.length) S.push(v); else { t.result = v; t.done = true; }
            break inner;
          }
          case OP.WAIT: {
            const secs = S.pop();
            if (!isNum(secs)) fault(`wait() necesita segundos (un numero), no ${typeName(secs)}`);
            const until = this.time + Math.max(0, raw(secs)), first = this.frame;
            t.block = new Block(vm => [vm.frame > first && vm.time >= until - 1e-9, null], 'wait');
            t.pushResult = false; return;
          }
          case OP.GETA: S[S.length - 1] = this.getAttr(S[S.length - 1], N[b]); break;
          case OP.SETA: { const v = S.pop(), obj = S.pop(); this.setAttr(obj, N[b], v); break; }
          case OP.GETI: { const i = S.pop(); S[S.length - 1] = getIndex(S[S.length - 1], i); break; }
          case OP.SETI: { const v = S.pop(), i = S.pop(); setIndex(S.pop(), i, v); break; }
          case OP.LIST: S.push(b ? S.splice(S.length - b, b) : []); break;
          case OP.FORMAT: S[S.length - 1] = formatValue(S[S.length - 1], K[c]); break;
          case OP.CONCAT: { const parts = S.splice(S.length - b, b); S.push(parts.map(p => typeof p === 'string' ? p : toText(p)).join('')); break; }
          case OP.NOP: break;
          default: fault(`instruccion desconocida ${op}`);
        }
      }
    }
  }

  callMethod(t, S, argc, name, kwnames) {
    const args = S.splice(S.length - argc, argc), obj = S.pop();
    const key = receiverOf(obj) + '.' + name, sig = METHODS[key], impl = STDLIB[key];
    if (!sig || !impl) fault(`${typeName(obj)} no tiene el metodo .${name}()`);
    const pos = kwnames.length ? args.slice(0, args.length - kwnames.length) : args;
    const kw = {};
    kwnames.forEach((k, i) => { kw[k] = args[args.length - kwnames.length + i]; });
    const [full, extra] = bindRuntime(name, sig, [obj, ...pos], kw);
    return impl({ vm: this, task: t }, full, extra);
  }
  getAttr(obj, name) {
    if (obj instanceof Layer) return name === 'name' ? obj.name : this.layerGet(obj.name, name);
    if (obj instanceof TaskRef) { if (name === 'running') return this.taskRunning(obj); if (name === 'result') return this.taskResult(obj); }
    fault(`${typeName(obj)} no tiene la propiedad '${name}'`);
  }
  setAttr(obj, name, v) {
    if (obj instanceof Layer) { this.layerSet(obj.name, name, v); return; }
    fault(`no se puede cambiar '${name}' de ${typeName(obj)}`);
  }
  globalValue(name) { return this.globals[this.gindex[name]]; }
  setGlobal(name, v) { this.globals[this.gindex[name]] = v; }
}
const INIT = '__init__';

function bindRuntime(name, sig, pos, kw) {
  const fixed = sig.filter(p => p.kind === 'normal' || p.kind === 'kwonly');
  const normal = fixed.filter(p => p.kind === 'normal');
  const varargs = sig.some(p => p.kind === 'varargs'), varkw = sig.some(p => p.kind === 'varkw');
  const slots = new Map(), extraPos = [];
  pos.forEach((v, i) => {
    if (i < normal.length) slots.set(normal[i].name, v);
    else if (varargs) extraPos.push(v);
    else fault(`.${name}() recibe como mucho ${normal.length - 1} argumento(s)`);
  });
  const extraKw = {}, names = new Set(fixed.map(p => p.name));
  for (const [k, v] of Object.entries(kw)) {
    if (slots.has(k)) fault(`.${name}(): '${k}' esta dos veces`);
    if (names.has(k)) slots.set(k, v);
    else if (varkw) extraKw[k] = v;
    else fault(`.${name}() no tiene un argumento '${k}'`);
  }
  const out = fixed.map(p => {
    if (slots.has(p.name)) return slots.get(p.name);
    if (!p.has) fault(`.${name}(): falta el argumento '${p.name}'`);
    return p.dflt;
  });
  return [[...out, ...extraPos], extraKw];
}

// ================================================================== operadores (como operators.py)
const SYMBOL = { [OP.ADD]: '+', [OP.SUB]: '-', [OP.MUL]: '*', [OP.DIV]: '/', [OP.IDIV]: '//', [OP.MOD]: '%', [OP.POW]: '**', [OP.LT]: '<', [OP.LE]: '<=', [OP.GT]: '>', [OP.GE]: '>=' };
const pyMod = (x, y) => { const r = x % y; return r !== 0 && (r < 0) !== (y < 0) ? r + y : r; };

export function binary(op, a, b) {
  if (isNumB(a) && isNumB(b)) {
    const fl = a instanceof F || b instanceof F, x = raw(a), y = raw(b);
    switch (op) {
      case OP.ADD: return mk(x + y, fl);
      case OP.SUB: return mk(x - y, fl);
      case OP.MUL: return mk(x * y, fl);
      default: break;
    }
    if ((op === OP.DIV || op === OP.IDIV || op === OP.MOD) && y === 0) fault('division entre cero');
    if (op === OP.DIV) return new F(x / y);
    if (op === OP.IDIV) return mk(Math.floor(x / y), fl);
    if (op === OP.MOD) return mk(pyMod(x, y), fl);
    if (op === OP.POW) {
      if (x === 0 && y < 0) fault('potencia demasiado grande o de cero');
      const r = x ** y;
      if (Number.isNaN(r)) fault('potencia de un negativo con decimales');
      if (!Number.isFinite(r)) fault('potencia demasiado grande o de cero');
      return mk(r, fl || y < 0);
    }
  }
  if (op === OP.ADD) {
    if (typeof a === 'string' && typeof b === 'string') return a + b;
    if (Array.isArray(a) && Array.isArray(b)) return [...a, ...b];
    if (typeof a === 'string' || typeof b === 'string')
      fault(`no se puede sumar ${typeName(a)} (${toText(a)}) y ${typeName(b)} (${toText(b)}): para unir texto y numeros usa str(x) o un f-string: f"puntos: {x}"`);
  }
  if (op === OP.MUL) {
    const rep = (s, n) => { n = Math.max(0, Math.min(n, 10000)); return typeof s === 'string' ? s.repeat(n) : Array.from({ length: n }, () => s).flat(); };
    if ((typeof a === 'string' || Array.isArray(a)) && typeof b === 'number') return rep(a, b);
    if ((typeof b === 'string' || Array.isArray(b)) && typeof a === 'number') return rep(b, a);
  }
  if (op === OP.MOD && typeof a === 'string') fault('el formato con % no se puede usar: usa un f-string');
  fault(`no se puede hacer ${typeName(a)} ${SYMBOL[op]} ${typeName(b)} (${toText(a)} ${SYMBOL[op]} ${toText(b)})`);
}

export function compare(op, a, b) {
  if (op === OP.EQ) return eq(a, b);
  if (op === OP.NE) return !eq(a, b);
  if (op === OP.IN || op === OP.NOTIN) {
    let r;
    if (Array.isArray(b)) r = b.some(x => eq(a, x));
    else if (typeof b === 'string') { if (typeof a !== 'string') fault(`'in' con un texto necesita otro texto, no ${typeName(a)}`); r = b.includes(a); }
    else fault(`'in' necesita una lista o un texto a la derecha, no ${typeName(b)}`);
    return op === OP.IN ? r : !r;
  }
  if ((isNumB(a) && isNumB(b)) || (typeof a === 'string' && typeof b === 'string')) {
    const c = cmp(a, b);
    return op === OP.LT ? c < 0 : op === OP.LE ? c <= 0 : op === OP.GT ? c > 0 : c >= 0;
  }
  fault(`no se puede comparar ${typeName(a)} ${SYMBOL[op]} ${typeName(b)} (${toText(a)}, ${toText(b)})`);
}

const strLen = s => [...s].length;
function index(seq, i) {
  if (typeof i === 'boolean' || !(typeof i === 'number')) {
    if (i instanceof F && Number.isInteger(i.v)) i = i.v;
    else fault(`el indice tiene que ser un entero, no ${typeName(i)} (${toText(i)})`);
  }
  const n = typeof seq === 'string' ? strLen(seq) : seq.length;
  if (!(-n <= i && i < n)) fault(`indice ${i} fuera de rango (hay ${n} elementos: de 0 a ${n - 1})`);
  return i < 0 ? i + n : i;
}
export function getIndex(seq, i) {
  if (Array.isArray(seq)) return seq[index(seq, i)];
  if (typeof seq === 'string') return [...seq][index(seq, i)];
  fault(`no se puede usar [ ] con ${typeName(seq)}`);
}
export function setIndex(seq, i, v) {
  if (Array.isArray(seq)) {
    if (Object.isFrozen(seq)) fault('esta lista es constante');
    seq[index(seq, i)] = v; return;
  }
  if (typeof seq === 'string') fault('los textos no se pueden cambiar letra a letra: crea uno nuevo');
  fault(`no se puede usar [ ] = con ${typeName(seq)}`);
}

export { Fault, F, Layer, FuncRef, TaskRef, Block, toText };
