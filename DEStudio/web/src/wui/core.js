// Nucleo comun de la maquina de wui en JS (vista previa del editor). Misma semantica que la de Python
// (tools/wui/wui/shared) y la de C++ (re-wor/src/de/wui_vm.cpp): si cambias algo aqui, cambialo alli.
//
// Valores: null (nada), boolean, number entero (siempre Number.isInteger), F (decimal: en JS 1.0 y 1 son el mismo
// numero, asi que los decimales van envueltos), string, Array (lista), Layer, FuncRef, TaskRef.

export class F { constructor(v) { this.v = v; } }
export class Layer { constructor(name) { this.name = name; } }
export class FuncRef { constructor(index, name) { this.index = index; this.name = name; } }
export class TaskRef { constructor(id, name) { this.id = id; this.name = name; this.result = null; } }
export class Block { constructor(poll, why = '') { this.poll = poll; this.why = why; } }
export class Fault extends Error { constructor(m) { super(m); this.wui = true; } }

export const fault = m => { throw new Fault(m); };
export const isF = v => v instanceof F;
export const isInt = v => typeof v === 'number';
export const isNum = v => typeof v === 'number' || v instanceof F;          // sin bool (como is_number)
export const isNumB = v => isNum(v) || typeof v === 'boolean';                // con bool (los operadores)
export const raw = v => v instanceof F ? v.v : typeof v === 'boolean' ? (v ? 1 : 0) : v;
export const flt = x => new F(x);
/** resultado de una operacion: entero si los dos lo eran */
export const mk = (x, asFloat) => asFloat ? new F(x) : x;

export function typeName(v) {
  if (v === null || v === undefined) return 'nada';
  if (typeof v === 'boolean') return 'bool';
  if (typeof v === 'number') return 'entero';
  if (v instanceof F) return 'decimal';
  if (typeof v === 'string') return 'texto';
  if (Array.isArray(v)) return 'lista';
  if (v instanceof Layer) return 'capa';
  if (v instanceof FuncRef) return 'funcion';
  if (v instanceof TaskRef) return 'tarea';
  return typeof v;
}

export function receiverOf(v) {
  if (v instanceof Layer) return 'Layer';
  if (v instanceof TaskRef) return 'Task';
  if (Array.isArray(v)) return 'list';
  if (typeof v === 'string') return 'str';
  return typeName(v);
}

// ---------------------------------------------------------------- texto (como repr/str de Python)
export function reprFloat(x) {
  if (Number.isNaN(x)) return 'nan';
  if (!Number.isFinite(x)) return x > 0 ? 'inf' : '-inf';
  if (Object.is(x, -0)) return '-0.0';
  const [m, es] = x.toExponential().split('e');
  const e = +es, neg = m[0] === '-';
  const digits = m.replace('-', '').replace('.', '');
  let s;
  if (e >= -4 && e < 16) {
    if (e >= 0) {
      const ip = digits.slice(0, e + 1).padEnd(e + 1, '0'), fp = digits.slice(e + 1);
      s = ip + '.' + (fp || '0');
    } else s = '0.' + '0'.repeat(-e - 1) + digits;
  } else {
    s = digits[0] + (digits.length > 1 ? '.' + digits.slice(1) : '') + 'e' + (e < 0 ? '-' : '+') + String(Math.abs(e)).padStart(2, '0');
  }
  return (neg ? '-' : '') + s;
}

export function toText(v) {
  if (v === null || v === undefined) return 'nada';
  if (v === true) return 'True';
  if (v === false) return 'False';
  if (typeof v === 'number') return String(v);
  if (v instanceof F) return Number.isInteger(v.v) && Math.abs(v.v) < 1e15 ? String(v.v) + '.0' : reprFloat(v.v);
  if (typeof v === 'string') return v;
  if (Array.isArray(v)) return '[' + v.map(x => typeof x === 'string' ? pyRepr(x) : toText(x)).join(', ') + ']';
  if (v instanceof Layer) return `capa(${v.name})`;
  if (v instanceof FuncRef) return `funcion ${v.name}`;
  if (v instanceof TaskRef) return `tarea ${v.name || v.id}`;
  return String(v);
}

export function pyRepr(s) {
  const q = s.includes("'") && !s.includes('"') ? '"' : "'";
  let out = q;
  for (const ch of s) {
    if (ch === '\\') out += '\\\\';
    else if (ch === q) out += '\\' + q;
    else if (ch === '\n') out += '\\n';
    else if (ch === '\r') out += '\\r';
    else if (ch === '\t') out += '\\t';
    else if (ch < ' ' || ch === '\x7f') out += '\\x' + ch.charCodeAt(0).toString(16).padStart(2, '0');
    else out += ch;
  }
  return out + q;
}

export function truthy(v) {
  if (v === null || v === undefined || v === false) return false;
  if (v === true) return true;
  if (typeof v === 'number') return v !== 0;
  if (v instanceof F) return v.v !== 0;
  if (typeof v === 'string' || Array.isArray(v)) return v.length > 0;
  return true;
}

// ---------------------------------------------------------------- igualdad y orden
export function eq(a, b) {
  if (isNumB(a) && isNumB(b)) return raw(a) === raw(b);
  if (a === null || b === null) return a === b;
  if (typeof a !== typeof b) return false;
  if (typeof a === 'string') return a === b;
  if (Array.isArray(a)) return Array.isArray(b) && a.length === b.length && a.every((x, i) => eq(x, b[i]));
  if (a instanceof Layer) return b instanceof Layer && a.name === b.name;
  if (a instanceof FuncRef) return b instanceof FuncRef && a.index === b.index;
  return a === b;
}

export function cmp(a, b) {
  if (isNumB(a) && isNumB(b)) { const x = raw(a), y = raw(b); return x < y ? -1 : x > y ? 1 : 0; }
  if (typeof a === 'string' && typeof b === 'string') {
    // orden por punto de codigo (como Python), no por localizacion
    const A = [...a], B = [...b];
    for (let i = 0; i < Math.min(A.length, B.length); i++) {
      const x = A[i].codePointAt(0), y = B[i].codePointAt(0);
      if (x !== y) return x < y ? -1 : 1;
    }
    return A.length - B.length < 0 ? -1 : A.length > B.length ? 1 : 0;
  }
  if (Array.isArray(a) && Array.isArray(b)) {
    for (let i = 0; i < Math.min(a.length, b.length); i++) { if (!eq(a[i], b[i])) return cmp(a[i], b[i]); }
    return a.length < b.length ? -1 : a.length > b.length ? 1 : 0;
  }
  return null;
}

// ---------------------------------------------------------------- formato (mini lenguaje de Python)
const SPEC = /^(?:(.)?([<>=^]))?([+\- ])?(#)?(0)?(\d+)?([,_])?(?:\.(\d+))?([bcdeEfFgGnosxX%])?$/su;

function group(int, sep) { return int.replace(/\B(?=(\d{3})+(?!\d))/g, sep); }

/** x con p decimales, redondeo exacto al par como Python (toFixed redondea los empates hacia arriba) */
export function fixedStr(x, p) {
  if (!Number.isFinite(x)) return Number.isNaN(x) ? 'nan' : x > 0 ? 'inf' : '-inf';
  const neg = x < 0 || Object.is(x, -0);
  const buf = new DataView(new ArrayBuffer(8));
  buf.setFloat64(0, Math.abs(x));
  const bits = buf.getBigUint64(0), ex = Number((bits >> 52n) & 0x7ffn);
  let mant = bits & 0xfffffffffffffn, e;
  if (ex === 0) e = -1074; else { mant |= 1n << 52n; e = ex - 1075; }
  const scale = 10n ** BigInt(p);
  let n;
  if (e >= 0) n = (mant << BigInt(e)) * scale;
  else {
    const num = mant * scale, den = 1n << BigInt(-e);
    n = num / den;
    const r = num - n * den, twice = r * 2n;
    if (twice > den || (twice === den && n % 2n === 1n)) n += 1n;
  }
  let s = n.toString();
  if (p > 0) { s = s.padStart(p + 1, '0'); s = s.slice(0, -p) + '.' + s.slice(-p); }
  return (neg ? '-' : '') + s;
}

function expStr(x, p, upper) {
  if (!Number.isFinite(x)) return fixedStr(x, 0);
  let [m, e] = x.toExponential(p).split('e');
  const n = +e;
  const s = m + (upper ? 'E' : 'e') + (n < 0 ? '-' : '+') + String(Math.abs(n)).padStart(2, '0');
  return s;
}

function genStr(x, p, alt, upper) {
  if (!Number.isFinite(x)) return fixedStr(x, 0);
  if (p === 0) p = 1;
  if (x === 0) return alt ? '0.' + '0'.repeat(p - 1) : '0';
  const e = +x.toExponential(p - 1).split('e')[1];
  let s;
  if (e >= -4 && e < p) s = fixedStr(x, Math.max(0, p - 1 - e));
  else s = expStr(x, p - 1, upper);
  if (!alt) {
    if (s.includes('e') || s.includes('E')) s = s.replace(/\.?0+(?=[eE])/, '');
    else if (s.includes('.')) s = s.replace(/\.?0+$/, '');
  }
  return s;
}

export function formatValue(v, spec) {
  if (spec.startsWith('!r')) return typeof v === 'string' ? pyRepr(v) : toText(v);
  if (!spec) return typeof v === 'string' ? v : toText(v);
  const m = SPEC.exec(spec);
  if (!m) fault(`el formato ':${spec}' no es valido`);
  let [, fill, align, sign, alt, zero, width, gsep, prec, type] = m;
  const bad = () => fault(`el formato ':${spec}' no sirve para ${typeName(v)} (${toText(v)})`);
  let body, neg = false, numeric = true;
  const P = prec === undefined ? undefined : +prec;
  if (typeof v === 'string') {
    if (type && type !== 's') bad();
    if (sign || gsep) bad();
    body = P === undefined ? v : [...v].slice(0, P).join('');
    numeric = false;
  } else if (isNumB(v)) {
    const isFloat = v instanceof F;
    let x = raw(v);
    if (!type) type = isFloat ? (P === undefined ? 'r' : 'g') : 'd';
    if ('dbxXoc'.includes(type)) {
      if (isFloat) bad();
      if (P !== undefined) bad();
      neg = x < 0; x = Math.abs(x);
      body = type === 'd' ? String(x) : type === 'x' ? x.toString(16) : type === 'X' ? x.toString(16).toUpperCase()
        : type === 'b' ? x.toString(2) : type === 'o' ? x.toString(8) : String.fromCodePoint(x);
      if (alt && 'xXbo'.includes(type)) body = '0' + (type === 'X' ? 'X' : type) + body;
      if (gsep && type === 'd') body = group(body, gsep);
    } else if (type === 's') bad();
    else {
      neg = x < 0 || Object.is(x, -0); x = Math.abs(x);
      const p = P === undefined ? 6 : P;
      if (type === 'r') body = toText(new F(x));
      else if (type === 'f' || type === 'F') body = fixedStr(x, p);
      else if (type === 'e' || type === 'E') body = expStr(x, p, type === 'E');
      else if (type === 'g' || type === 'G' || type === 'n') body = genStr(x, p, !!alt, type === 'G');
      else if (type === '%') body = fixedStr(x * 100, p) + '%';
      if (gsep) { const [ip, ...rest] = body.split('.'); body = (/^\d+$/.test(ip) ? group(ip, gsep) : ip) + (rest.length ? '.' + rest.join('.') : ''); }
      if (type === 'F') body = body.toUpperCase();
    }
  } else bad();
  let pre = numeric ? (neg ? '-' : sign === '+' ? '+' : sign === ' ' ? ' ' : '') : '';
  const W = width ? +width : 0;
  if (zero && !align) { fill = '0'; align = '='; }
  fill = fill || ' ';
  align = align || (numeric ? '>' : '<');
  const len = [...(pre + body)].length;
  if (len >= W) return pre + body;
  const pad = n => fill.repeat(n);
  const n = W - len;
  if (align === '<') return pre + body + pad(n);
  if (align === '>') return pad(n) + pre + body;
  if (align === '=') return pre + pad(n) + body;
  return pad(Math.floor(n / 2)) + pre + body + pad(n - Math.floor(n / 2));
}

// ---------------------------------------------------------------- colores
export const NAMED_COLORS = {
  white: 'ffffff', black: '000000', red: 'ff2a2a', green: '2ee05a', yellow: 'ffd21f', blue: '2a8cff', orange: 'ff8a1f',
  purple: '9b5cff', pink: 'ff5cc8', cyan: '2ae6ff', gray: '808080', grey: '808080', gold: 'e3a043', star: '6ad7ff',
  blanco: 'ffffff', negro: '000000', rojo: 'ff2a2a', verde: '2ee05a', amarillo: 'ffd21f', azul: '2a8cff',
  naranja: 'ff8a1f', morado: '9b5cff', rosa: 'ff5cc8', gris: '808080', dorado: 'e3a043',
};

/** round de Python (mitad al par) para enteros */
export const roundHalfEven = x => { const r = Math.round(x); return Math.abs(x % 1) === 0.5 ? (r % 2 === 0 ? r : r - 1) : r; };

export function parseColor(v) {
  if (Array.isArray(v) && v.length >= 3 && v.slice(0, 3).every(isNum)) return v.slice(0, 3).map(x => Math.max(0, Math.min(255, roundHalfEven(raw(x)))));
  if (typeof v !== 'string') return null;
  const k = v.trim().toLowerCase();
  let s = NAMED_COLORS[k] ?? v.trim().replace(/^#+/, '');
  if (s.length === 3) s = [...s].map(c => c + c).join('');
  if (s.length !== 6 || !/^[0-9a-fA-F]{6}$/.test(s)) return null;
  return [0, 2, 4].map(i => parseInt(s.slice(i, i + 2), 16));
}
export const colorHex = rgb => '#' + rgb.map(x => x.toString(16).padStart(2, '0')).join('');

// ---------------------------------------------------------------- curvas
const outBounce = t => {
  const n = 7.5625, d = 2.75;
  if (t < 1 / d) return n * t * t;
  if (t < 2 / d) { t -= 1.5 / d; return n * t * t + 0.75; }
  if (t < 2.5 / d) { t -= 2.25 / d; return n * t * t + 0.9375; }
  t -= 2.625 / d; return n * t * t + 0.984375;
};
const C1 = 1.70158, C3 = C1 + 1;
export const EASES = {
  linear: t => t, in: t => t * t, out: t => 1 - (1 - t) ** 2,
  inout: t => t < 0.5 ? 2 * t * t : 1 - (-2 * t + 2) ** 2 / 2,
  in_cubic: t => t ** 3, out_cubic: t => 1 - (1 - t) ** 3,
  inout_cubic: t => t < 0.5 ? 4 * t ** 3 : 1 - (-2 * t + 2) ** 3 / 2,
  in_back: t => C3 * t ** 3 - C1 * t * t, out_back: t => 1 + C3 * (t - 1) ** 3 + C1 * (t - 1) ** 2,
  out_elastic: t => t === 0 || t === 1 ? t : 2 ** (-10 * t) * Math.sin((t * 10 - 0.75) * (2 * Math.PI / 3)) + 1,
  out_bounce: outBounce, in_bounce: t => 1 - outBounce(1 - t), step: t => t >= 1 ? 1 : 0,
};
export const ease = (name, t) => (EASES[name] || EASES.out)(t < 0 ? 0 : t > 1 ? 1 : t);

// ---------------------------------------------------------------- azar (igual que random.Random(semilla) de Python)
export class PyRandom {
  constructor(seed = 0) {
    this.mt = new Uint32Array(624); this.i = 625;
    // init_by_array con la semilla en trozos de 32 bits (como Python con un entero)
    let s = Math.abs(Math.trunc(seed)); const key = [];
    do { key.push(s % 4294967296); s = Math.floor(s / 4294967296); } while (s > 0);
    this.initGenrand(19650218);
    const mt = this.mt; let i = 1, j = 0;
    for (let k = Math.max(624, key.length); k; k--) {
      const p = mt[i - 1] ^ (mt[i - 1] >>> 30);
      mt[i] = ((mt[i] ^ Math.imul(p, 1664525)) + key[j] + j) >>> 0;
      i++; j++;
      if (i >= 624) { mt[0] = mt[623]; i = 1; }
      if (j >= key.length) j = 0;
    }
    for (let k = 623; k; k--) {
      const p = mt[i - 1] ^ (mt[i - 1] >>> 30);
      mt[i] = ((mt[i] ^ Math.imul(p, 1566083941)) - i) >>> 0;
      i++;
      if (i >= 624) { mt[0] = mt[623]; i = 1; }
    }
    mt[0] = 0x80000000;
  }
  initGenrand(s) {
    const mt = this.mt; mt[0] = s >>> 0;
    for (let i = 1; i < 624; i++) { const p = mt[i - 1] ^ (mt[i - 1] >>> 30); mt[i] = (Math.imul(p, 1812433253) + i) >>> 0; }
    this.i = 624;
  }
  u32() {
    const mt = this.mt;
    if (this.i >= 624) {
      for (let k = 0; k < 624; k++) {
        const y = (mt[k] & 0x80000000) | (mt[(k + 1) % 624] & 0x7fffffff);
        mt[k] = mt[(k + 397) % 624] ^ (y >>> 1) ^ (y & 1 ? 0x9908b0df : 0);
      }
      this.i = 0;
    }
    let y = mt[this.i++];
    y ^= y >>> 11; y ^= (y << 7) & 0x9d2c5680; y ^= (y << 15) & 0xefc60000; y ^= y >>> 18;
    return y >>> 0;
  }
  random() { const a = this.u32() >>> 5, b = this.u32() >>> 6; return (a * 67108864 + b) / 9007199254740992; }
  getrandbits(k) {
    if (k <= 32) return this.u32() >>> (32 - k);
    let r = 0, mul = 1, left = k;
    while (left > 0) { const n = Math.min(32, left); let w = this.u32(); if (n < 32) w >>>= 32 - n; r += w * mul; mul *= 4294967296; left -= 32; }
    return r;
  }
  below(n) {
    if (n <= 0) return 0;
    const k = Math.floor(Math.log2(n)) + 1;
    let r = this.getrandbits(k);
    while (r >= n) r = this.getrandbits(k);
    return r;
  }
}

// ---------------------------------------------------------------- programa (.wuib)
export const OP = {
  NOP: 0, CONST: 1, NONE: 2, TRUE: 3, FALSE: 4, POP: 5, DUP: 6, SWAP: 7, LOADL: 10, STOREL: 11, LOADG: 12, STOREG: 13,
  NEG: 20, NOT: 21, POS: 22, ADD: 30, SUB: 31, MUL: 32, DIV: 33, IDIV: 34, MOD: 35, POW: 36,
  EQ: 40, NE: 41, LT: 42, LE: 43, GT: 44, GE: 45, IN: 46, NOTIN: 47,
  JMP: 50, JF: 51, JT: 52, JFK: 53, JTK: 54, CALL: 60, CALLB: 61, CALLM: 62, RET: 63, RETN: 64, FREF: 65, CALLV: 66,
  GETA: 70, SETA: 71, GETI: 72, SETI: 73, LIST: 74, FORMAT: 75, CONCAT: 76, WAIT: 80,
};
export const NO_FUNC = 0xffff;

/** bytes de un .wuib (ArrayBuffer, Uint8Array o base64) -> programa */
export function loadProgram(data) {
  if (typeof data === 'string') data = Uint8Array.from(atob(data), c => c.charCodeAt(0));
  const u8 = data instanceof Uint8Array ? data : new Uint8Array(data);
  const dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
  let p = 0;
  const need = n => { if (p + n > u8.length) throw new Error('programa cortado'); };
  const u8r = () => { need(1); return u8[p++]; };
  const u16 = () => { need(2); const v = dv.getUint16(p, true); p += 2; return v; };
  const u32 = () => { need(4); const v = dv.getUint32(p, true); p += 4; return v; };
  const dec = new TextDecoder();
  const str = () => { const n = u32(); need(n); const s = dec.decode(u8.subarray(p, p + n)); p += n; return s; };
  const cst = () => {
    const t = u8r();
    if (t === 0) return null;
    if (t === 1) return u8r() !== 0;
    if (t === 2) { need(8); const v = Number(dv.getBigInt64(p, true)); p += 8; return v; }
    if (t === 3) { need(8); const v = dv.getFloat64(p, true); p += 8; return new F(v); }
    if (t === 4) return str();
    if (t === 5) { const n = u16(); const a = []; for (let i = 0; i < n; i++) a.push(cst()); return Object.freeze(a); }
    throw new Error('tipo de constante desconocido ' + t);
  };
  if (dec.decode(u8.subarray(0, 4)) !== 'WUIB') throw new Error('no es un programa de wui (.wuib)');
  p = 4;
  const ver = u16(); u16();
  if (ver !== 2) throw new Error(`version ${ver} del programa; esta maquina entiende la 2`);
  const prog = { source: str(), consts: [], names: [], builtins: [], globals: [], functions: [], handlers: [], init: NO_FUNC };
  for (let n = u32(); n; n--) prog.consts.push(cst());
  for (const t of ['names', 'builtins', 'globals']) for (let n = u16(); n; n--) prog[t].push(str());
  for (let n = u16(); n; n--) {
    const name = str(), nparams = u8r(), nlocals = u16(), flags = u8r(), ni = u32();
    const code = [];
    need(ni * 8);
    for (let i = 0; i < ni; i++) { code.push([u8[p], u8[p + 1], dv.getUint16(p + 2, true), dv.getInt32(p + 4, true)]); p += 8; }
    const lines = [];
    for (let m = u32(); m; m--) lines.push([u32(), u32()]);
    const defaults = [];
    for (let m = u16(); m; m--) defaults.push(cst());
    const localNames = [];
    for (let m = u16(); m; m--) localNames.push(str());
    prog.functions.push({ name, nparams, nlocals, flags, code, lines, defaults, localNames });
  }
  for (let n = u16(); n; n--) {
    const event = str(), func = u16(), na = u16(), args = [];
    for (let i = 0; i < na; i++) args.push(cst());
    prog.handlers.push({ event, func, args });
  }
  prog.init = u16();
  return prog;
}

export function lineAt(fn, pc) {
  let ln = 0;
  for (const [at, l] of fn.lines) { if (at <= pc) ln = l; else break; }
  return ln;
}

/** valor de las tablas generadas ({"$f": 1.0} -> decimal) */
export const tv = v => v && typeof v === 'object' && '$f' in v ? new F(v.$f) : Array.isArray(v) ? v.map(tv) : v;
