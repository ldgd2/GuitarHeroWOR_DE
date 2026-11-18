// La interfaz funcionando, para la vista previa del editor: misma logica que re-wor (src/de/ui3d.cpp): pantallas
// con entrada, bucle y salida, foco con flechas o raton, efecto de foco animado, acciones de los botones, atras,
// historial y condiciones con variables del juego (aqui simuladas). Si cambias algo aqui, cambialo alli.
import { actionOf, condOk, condVar, focusables, navTable } from './model.js';
import { listsOf, listTarget, listPlace, listMembers, listNav } from './lists.js';

const animated = (doc, clip) => {
  const c = doc.clips?.[clip];
  return !!c && (Object.values(c.tracks || {}).some(v => v && Object.keys(v).length) || (c.cuts || []).length > 0);
};
export const KEYS = { up: 1, down: 2, left: 3, right: 4, choose: 5, back: 6 };

export class Runtime {
  constructor(proj, log = () => {}) {
    this.p = proj; this.log = log; this.vars = { ...(proj.designVars || {}) };
    this.mode = 'hidden'; this.history = []; this.ff = []; this.F = []; this.nav = []; this.screen = 0; this.t = 0;
  }
  get doc() { return this.p.screens[this.screen]; }
  dur(clip) { return animated(this.doc, clip) ? Math.max(0, this.doc.clips[clip].duration || 0) : 0; }
  open() { this.history = []; this.enter(Math.max(0, this.p.screens.findIndex(s => s.id === this.p.start))); }
  enter(i) {
    this.screen = Math.max(0, Math.min(this.p.screens.length - 1, i));
    this.mode = 'intro'; this.t = 0; this.pending = -2; this.closing = false;
    this.F = focusables(this.doc); this.nav = listNav(this.doc, navTable(this.doc));
    this.L = listsOf(this.doc); this.lm = listMembers(this.L); this.loff = this.L.map(L => L.start);
    this.focus = this.F.length ? Math.max(0, this.F.findIndex(l => l.id === this.doc.focusStart)) : -1;
    this.ff = this.F.map(() => 0);
  }
  outro(pending) { this.pending = pending; this.mode = 'outro'; this.t = 0; }
  close() { if (this.closing) return; this.closing = true; this.outro(-1); this.log('Cierra la interfaz (re_ui_' + this.p.name + '_hide)'); }
  goto(i) { if (i < 0 || i === this.screen) return; this.history.push(this.screen); this.outro(i); }
  back() { if (!this.history.length) return this.close(); this.outro(this.history.pop()); }

  step(dt) {
    if (this.mode === 'hidden') return;
    this.t += dt;
    for (let g = 0; g < 4; g++) {
      if (this.mode === 'intro' && this.t >= this.dur('intro')) { this.t -= this.dur('intro'); this.mode = 'loop'; continue; }
      if (this.mode === 'loop') { const d = this.dur('loop'); this.t = d > 0 ? this.t % d : 0; break; }
      if (this.mode === 'outro' && this.t >= this.dur('outro')) {
        if (this.pending >= 0) { const h = this.history; this.enter(this.pending); this.history = h; continue; }
        this.mode = 'hidden'; this.history = []; break;
      }
      break;
    }
    // listas: se desplazan hacia el elemento con el foco
    const kl = Math.min(1, dt * 12);
    this.L?.forEach((L, li) => {
      const idx = L.items.findIndex(it => it.focus === this.focus && it.focus >= 0);
      const off = this.loff[li], to = idx >= 0 ? listTarget(L, off, idx) : off;
      this.loff[li] = Math.abs(to - off) < 1e-3 ? to : off + (to - off) * kl;
    });
    const k = Math.min(1, dt * 14);
    this.ff = this.ff.map((v, i) => v + ((i === this.focus && this.mode !== 'outro' ? 1 : 0) - v) * k);
  }
  /** lo que hay que dibujar ahora: {doc, clip, t} o null */
  frame() {
    if (this.mode === 'hidden') return null;
    const clip = this.mode;
    return { doc: this.doc, clip: animated(this.doc, clip) ? clip : 'base', t: Math.min(this.t, this.dur(clip)) };
  }

  input(key) {
    if (this.mode !== 'intro' && this.mode !== 'loop') return;
    if (key >= 1 && key <= 4) {
      if (!this.F.length) return;
      if (this.focus < 0) { this.focus = 0; return; }
      const nb = this.nav[this.focus], to = [nb.up, nb.down, nb.left, nb.right][key - 1];
      if (to >= 0) this.focus = to;
    } else if (key === 5) {
      if (this.focus >= 0) this.run(actionOf(this.F[this.focus]));
    } else if (key === 6) {
      const ob = this.doc.onBack || 'auto';
      if (ob === 'none') return;
      if (ob === 'close') this.close(); else this.back();
    }
  }
  hover(id) {
    const i = this.F.findIndex(l => l.id === id);
    if (i < 0 || (this.mode !== 'intro' && this.mode !== 'loop')) return;
    const m = this.lm?.get(id);   // un elemento fuera de la caja de su lista no se puede senalar
    if (m && !listPlace(this.L[m[0]], this.loff[m[0]], m[1]).inside) return;
    this.focus = i;
  }
  /** rueda del raton en (x, y) px del editor: mueve el foco por la lista que hay debajo. true si la uso */
  wheel(x, y, dir) {
    const li = this.L?.findIndex(L => L.cfg.wheel && x >= L.box.x0 && x <= L.box.x1 && y >= L.box.y0 && y <= L.box.y1);
    if (li == null || li < 0) return false;
    const L = this.L[li];
    if (!L.items.some(it => it.focus === this.focus)) {
      const first = L.items.find(it => it.focus >= 0);
      if (first) this.focus = first.focus;
      return true;
    }
    this.input(L.ax ? (dir > 0 ? 2 : 1) : (dir > 0 ? 4 : 3));
    return true;
  }
  click(id) { const i = this.F.findIndex(l => l.id === id); if (i >= 0) { this.focus = i; this.input(5); } }

  run(a) {
    switch (a.type) {
      case 'goto': { const i = this.p.screens.findIndex(s => s.id === a.screen); if (i < 0) this.log('Pantalla de destino no encontrada'); else this.goto(i); break; }
      case 'back': this.back(); break;
      case 'close': this.close(); break;
      case 'set': this.vars[a.global] = +a.value || 0; this.log(`Variable del juego ${a.global} = ${+a.value || 0}`); break;
      case 'script': this.log(`Script del juego: ${a.script || '(vacio)'}`); break;
      default: break;
    }
  }

  /** efecto sobre cada capa: foco (escala, tinte, opacidad, desplazamiento), visible con el foco, condicion */
  overrides(it) {
    const l = it.l;
    let alpha = 1, tint = null, fx = null, clip = null;
    const m = this.lm?.get(l.id);
    let ldx = 0, ldy = 0;
    if (m) {
      const L = this.L[m[0]], pl = listPlace(L, this.loff[m[0]], m[1]);
      ldx = pl.dx; ldy = pl.dy; alpha *= pl.alpha;
      if (L.cfg.clip) clip = L.box;
    }
    if (condVar(l.showIf)) {
      if (!condOk(l.showIf, this.vars)) return { hide: true };
    }
    if (l.showOnFocus) { const i = this.F.findIndex(q => q.id === l.showOnFocus); alpha *= i >= 0 ? this.ff[i] : 0; }
    const i = this.F.findIndex(q => q.id === l.id);
    if (i >= 0) {
      const f = this.ff[i], h = l.ui.hover || {};
      fx = { s: 1 + ((h.scale ?? 1) - 1) * f, dx: (h.dx || 0) * f, dy: (h.dy || 0) * f };
      const c = it.s.color || [255, 255, 255], hc = h.color || [255, 255, 255];
      tint = c.map((v, k) => v * (1 + (hc[k] / 255 - 1) * f));
      alpha *= 1 + ((h.alpha ?? 1) - 1) * f;
    }
    if (ldx || ldy) fx = { s: fx?.s ?? 1, dx: (fx?.dx || 0) + ldx, dy: (fx?.dy || 0) + ldy };
    return { alpha, tint, fx, clip };
  }
}
