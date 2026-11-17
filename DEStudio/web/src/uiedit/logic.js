// Logica de la interfaz (Python de wui) en la vista previa: la maquina JS (src/wui) sobre las capas del proyecto.
// Hace lo mismo que el anfitrion de re-wor (LogicHost en src/de/ui3d.cpp): si cambias algo aqui, cambialo alli.
import { api } from '../studio/files.js';
import { F, raw, toText, loadProgram } from '../wui/core.js';
import { Host, Inputs, Machine } from '../wui/vm.js';
import { KEYS } from './runtime.js';
import { W, H } from './model.js';

const safe = n => (n || '').toLowerCase().replace(/[^a-z0-9_]+/g, '_').replace(/^_+|_+$/g, '').slice(0, 32) || 'pantalla';
export const layerName = l => String(l.name || l.id).split(/\s+/).filter(Boolean).join(' ');

/** lo que necesita el compilador: nombres de pantallas y capas (sin imagenes ni animaciones) */
const slim = proj => ({
  name: proj.name,
  screens: (proj.screens || []).map(s => ({ name: s.name, layers: (s.layers || []).map(l => ({ id: l.id, name: l.name })) })),
});

/** compila en el servidor (tools/wui): {ok, wuib (base64), diagnostics} */
export const compileLogic = (proj, source) => api('/api/ui/wui/compile', { project: slim(proj), source });
export const logicApi = () => api('/api/ui/wui/api');

// teclado de la vista previa -> botones del mando (nombres de wui)
export const PAD_KEYS = {
  1: ['green'], 2: ['red'], 3: ['yellow'], 4: ['blue'], 5: ['orange'],
  ArrowUp: ['up', 'strum_up'], ArrowDown: ['down', 'strum_down'], ArrowLeft: ['left'], ArrowRight: ['right'],
  Enter: ['choose', 'green'], ' ': ['choose', 'green'], Escape: ['cancel', 'red'], Backspace: ['back', 'star'], Tab: ['start'],
};

class PreviewHost extends Host {
  constructor(L) { super(); this.L = L; }
  get r() { return this.L.r; }
  target() { const r = this.r; return r.mode === 'outro' && r.pending >= 0 ? r.pending : r.screen; }
  doc() { return this.r.p.screens[this.target()]; }
  find(n) { return (this.doc().layers || []).find(l => layerName(l) === n) || null; }
  design(l) {
    const b = l.base || {};
    return { x: this.r.p.mode === '3d' ? W / 2 : +(b.x ?? W / 2), y: this.r.p.mode === '3d' ? H / 2 : +(b.y ?? H / 2),
      w: +(b.w ?? (this.r.p.mode === '3d' ? 400 : 200)), h: +(b.h ?? (this.r.p.mode === '3d' ? 225 : 120)) };
  }
  layerNames() { return [...new Set((this.doc().layers || []).map(layerName))]; }
  layerGet(n, p) {
    const o = this.L.ov[n];
    if (o && p in o) return o[p];
    const l = this.find(n);
    if (!l) return null;
    const d = this.design(l);
    if (p === 'x' || p === 'y' || p === 'w' || p === 'h') return new F(d[p]);
    if (p === 'kind') return l.type || 'shape';
    if (p === 'name') return layerName(l);
    if (p === 'focusable') return !!l.ui?.focus;
    return null;
  }
  layerSet(n, p, v) { (this.L.ov[n] ||= {})[p] = v; }
  layerDesign(n) { const l = this.find(n); if (!l) return {}; const d = this.design(l); return { x: new F(d.x), y: new F(d.y) }; }
  layerContains(n, x, y) {
    const l = this.find(n);
    if (!l) return false;
    const d = this.design(l), o = this.L.ov[n] || {};
    const s = raw(o.scale ?? 1) || 1, cx = raw(o.x ?? d.x), cy = raw(o.y ?? d.y);
    return Math.abs(x - cx) <= d.w * s * raw(o.sx ?? 1) / 2 && Math.abs(y - cy) <= d.h * s * raw(o.sy ?? 1) / 2;
  }
  layerAt(x, y) {
    const ls = [...(this.doc().layers || [])].reverse();
    for (const l of ls) {
      const n = layerName(l);
      if (this.L.ov[n]?.visible === false) continue;
      if (this.layerContains(n, x, y)) return n;
    }
    return null;
  }
  screenNames() { return this.r.p.screens.map(s => safe(s.name)); }
  showScreen(n) {
    const i = this.r.p.screens.findIndex(s => safe(s.name) === n);
    if (i < 0 || i === this.target()) return;
    this.L.ov = {};
    this.r.goto(i);
  }
  close() { this.r.close(); }
  setFocus(n) {
    if (!n) return;
    const i = this.r.F.findIndex(l => layerName(l) === n);
    if (i >= 0) { this.r.focus = i; this.L.vmFocus = i; }
  }
  moveFocus(d) { this.r.input(KEYS[d]); }
  lockFocus(l) { this.L.locked = !!l; }
  gameGet(n) { return n in this.r.vars ? this.r.vars[n] : null; }
  gameSet(n, v) { this.r.vars[n] = typeof v === 'boolean' ? (v ? 1 : 0) : raw(v); this.L.say(`Variable del juego ${n} = ${toText(v)}`); }
  gameCall(s, p) {
    const args = Object.entries(p || {}).map(([k, v]) => ` ${k} = ${toText(v)}`).join('');
    this.L.say(`Script del juego: ${s}${args}`);
    return null;
  }
  playerInfo() { return 0; }
  bandInfo() { return 0; }
  gameState(w) { return w === 'language' ? 'es' : null; }
  playSound(n) { this.L.say(`Sonido: ${n}`); }
  vibrate(p, s, t) { this.L.say(`Vibrar mando ${p}: ${s} durante ${t} s`); }
  log(level, m) { this.L.say(`${level === 'info' ? 'log' : level}: ${m}`); }
}

export class PreviewLogic {
  /** r: Runtime de la vista previa; bytes: el programa (.wuib) */
  constructor(r, bytes, say) {
    this.r = r; this.say = say; this.ov = {}; this.locked = false; this.vmFocus = -2;
    this.mouse = [0, 0, false];
    this.vm = new Machine(loadProgram(bytes), new PreviewHost(this), { seed: (Math.random() * 2 ** 31) | 0 });
    this.vm.start(safe(r.doc.name));
  }
  /** cada fotograma, despues de r.step */
  step(dt, held) {
    const r = this.r, vm = this.vm;
    if (r.mode === 'hidden') return;
    try {
      const host = vm.host, cur = safe(r.p.screens[host.target()].name);
      if (!vm.closed && vm.screen !== cur) vm.goto(cur, 'auto');
      const f = r.mode === 'outro' ? -1 : r.focus;
      if (f !== this.vmFocus) { this.vmFocus = f; vm.notifyFocus(f >= 0 && r.F[f] ? layerName(r.F[f]) : null); }
      vm.step(dt, new Inputs({ 1: new Set(held) }, {}, this.mouse));
    } catch (e) {
      if (!e.wui) throw e;
      this.say('error: ' + e.message);
    }
    if (vm.closed && !r.closing && r.mode !== 'hidden') r.close();
  }
  click(l) { this.vm.notifyClick(layerName(l)); }

  /** cambios de la logica a una capa, listos para Stage2D (o null) */
  look(l) {
    const o = this.ov[layerName(l)];
    if (!o) return null;
    const n = (k, d) => (k in o && o[k] !== null ? raw(o[k]) : d);
    const d = this.r.p.mode === '3d' ? { x: W / 2, y: H / 2 } : { x: +(l.base?.x ?? W / 2), y: +(l.base?.y ?? H / 2) };
    return {
      visible: o.visible !== false,
      dx: n('x', d.x) - d.x, dy: n('y', d.y) - d.y,
      sx: n('scale', 1) * n('sx', 1), sy: n('scale', 1) * n('sy', 1), rot: n('rot', 0),
      alpha: Math.max(0, Math.min(1, n('alpha', 1))),
      tint: o.tint || null, brightness: n('brightness', 1),
      glow: n('glow', 0), glowColor: o.glow_color || '#ffffff', glowAlpha: n('glow_alpha', 1),
      blur: n('blur', 0),
      shadow: n('shadow', 0), shadowColor: o.shadow_color || '#000000', shadowAlpha: n('shadow_alpha', 0.6),
      shadowX: n('shadow_x', 4), shadowY: n('shadow_y', 4),
      outline: n('outline', 0), outlineColor: o.outline_color || '#ffffff',
      fill: Math.max(0, Math.min(1, n('fill', 1))), fillDir: o.fill_dir || 'right',
      text: typeof o.text === 'string' && o.text !== '' ? o.text : null,
    };
  }
}

const hexA = (hex, a) => {
  const h = (hex || '#ffffff').replace('#', '');
  const v = [0, 2, 4].map(i => parseInt(h.slice(i, i + 2), 16) || 0);
  return `rgba(${v[0]},${v[1]},${v[2]},${Math.max(0, Math.min(1, a)).toFixed(3)})`;
};

/** CSS de los efectos (z = zoom de la vista): {transform, filter, clipPath} */
export function lookCss(v, z) {
  const f = [];
  if (v.blur > 0) f.push(`blur(${(v.blur * z).toFixed(2)}px)`);
  if (v.brightness !== 1) f.push(`brightness(${v.brightness})`);
  if (v.outline > 0) {
    const w = v.outline * z, c = v.outlineColor;
    for (const [x, y] of [[w, 0], [-w, 0], [0, w], [0, -w]]) f.push(`drop-shadow(${x.toFixed(2)}px ${y.toFixed(2)}px 0 ${c})`);
  }
  if (v.glow > 0) {
    f.push(`drop-shadow(0 0 ${(v.glow * z * 0.5).toFixed(2)}px ${hexA(v.glowColor, v.glowAlpha)})`);
    f.push(`drop-shadow(0 0 ${(v.glow * z).toFixed(2)}px ${hexA(v.glowColor, v.glowAlpha * 0.7)})`);
  }
  if (v.shadow > 0) f.push(`drop-shadow(${(v.shadowX * z).toFixed(2)}px ${(v.shadowY * z).toFixed(2)}px ${(v.shadow * z * 0.5).toFixed(2)}px ${hexA(v.shadowColor, v.shadowAlpha)})`);
  let clipPath;
  if (v.fill < 1) {
    const k = ((1 - v.fill) * 100).toFixed(2) + '%';
    clipPath = { right: `inset(0 ${k} 0 0)`, left: `inset(0 0 0 ${k})`, up: `inset(${k} 0 0 0)`, down: `inset(0 0 ${k} 0)` }[v.fillDir] || `inset(0 ${k} 0 0)`;
  }
  return {
    transform: `translate(${(v.dx * z).toFixed(2)}px, ${(v.dy * z).toFixed(2)}px) rotate(${v.rot}deg) scale(${v.sx}, ${v.sy})`,
    filter: f.length ? f.join(' ') : undefined, clipPath,
  };
}
