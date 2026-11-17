// La interfaz funcionando, para la vista previa del editor: misma logica que re-wor (src/de/ui3d.cpp): pantallas
// con entrada, bucle y salida, foco con flechas o raton, efecto de foco animado, acciones de los botones, atras,
// historial y condiciones con variables del juego (aqui simuladas). Si cambias algo aqui, cambialo alli.
import { focusables, navTable } from './model.js';

const animated = (doc, clip) => {
  const c = doc.clips?.[clip];
  return !!c && (Object.values(c.tracks || {}).some(v => v && Object.keys(v).length) || (c.cuts || []).length > 0);
};
export const KEYS = { up: 1, down: 2, left: 3, right: 4, choose: 5, back: 6 };

export class Runtime {
  constructor(proj, log = () => {}) {
    this.p = proj; this.log = log; this.vars = {};
    this.mode = 'hidden'; this.history = []; this.ff = []; this.F = []; this.nav = []; this.screen = 0; this.t = 0;
  }
  get doc() { return this.p.screens[this.screen]; }
  dur(clip) { return animated(this.doc, clip) ? Math.max(0, this.doc.clips[clip].duration || 0) : 0; }
  open() { this.history = []; this.enter(Math.max(0, this.p.screens.findIndex(s => s.id === this.p.start))); }
  enter(i) {
    this.screen = Math.max(0, Math.min(this.p.screens.length - 1, i));
    this.mode = 'intro'; this.t = 0; this.pending = -2; this.closing = false;
    this.F = focusables(this.doc); this.nav = navTable(this.doc);
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
      if (this.focus >= 0) this.run(this.F[this.focus].ui.action || {});
    } else if (key === 6) {
      const ob = this.doc.onBack || 'auto';
      if (ob === 'none') return;
      if (ob === 'close') this.close(); else this.back();
    }
  }
  hover(id) { const i = this.F.findIndex(l => l.id === id); if (i >= 0 && (this.mode === 'intro' || this.mode === 'loop')) this.focus = i; }
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
    let alpha = 1, tint = null, fx = null;
    if (l.showIf?.global) {
      const v = this.vars[l.showIf.global] ?? 0, x = +l.showIf.value || 0;
      const ok = { '=': v === x, '!=': v !== x, '>': v > x, '<': v < x, '>=': v >= x, '<=': v <= x }[l.showIf.op || '='];
      if (!ok) return { hide: true };
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
    return { alpha, tint, fx };
  }
}
