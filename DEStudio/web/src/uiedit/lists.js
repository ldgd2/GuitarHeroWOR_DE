// Listas con desplazamiento: un grupo con `list.on`. Sus hijos directos son los elementos (en el orden de su
// posicion en el eje). Solo se ven `view` elementos dentro de la caja; al mover el foco la lista se desplaza y,
// si es infinita, los que salen por arriba entran por abajo (y al reves). La misma cuenta esta en modes.py
// (exportar) y en re-wor (src/de/ui3d.cpp). Si cambias algo aqui, cambialo alli.
import { childrenOf, descendants, focusables, worldOf } from './model.js';

export const LIST_DEFAULT = { on: false, axis: 'y', view: 4, step: 0, loop: true, center: true, fade: true, clip: true, wheel: true };
export const listCfg = l => ({ ...LIST_DEFAULT, ...(l.list || {}) });

/** caja de una capa o grupo en el diseno (px del editor, sin giro) */
export function boxOf(doc, l) {
  const leaves = l.type === 'group'
    ? [...descendants(doc, l.id)].map(id => doc.layers.find(q => q.id === id)).filter(q => q && q.type !== 'group' && q.type !== 'camera' && q.visible !== false)
    : [l];
  let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
  for (const q of leaves) {
    const w = worldOf(doc, q, 'base', 0), k = q.base.scale ?? 1, bw = q.base.w * k, bh = q.base.h * k;
    const lx = w.x - (q.base.ax ?? 0.5) * bw, ly = w.y - (q.base.ay ?? 0.5) * bh;
    x0 = Math.min(x0, lx); y0 = Math.min(y0, ly); x1 = Math.max(x1, lx + bw); y1 = Math.max(y1, ly + bh);
  }
  if (!isFinite(x0)) { const w = worldOf(doc, l, 'base', 0); return { x0: w.x, y0: w.y, x1: w.x, y1: w.y }; }
  return { x0, y0, x1, y1 };
}

/** listas de la pantalla: [{ id, cfg, n, step, box, items: [{ id, ids:Set, focus }], start }] */
export function listsOf(doc) {
  const F = focusables(doc);
  const out = [];
  for (const g of doc.layers) {
    if (g.type !== 'group' || !g.list?.on || g.visible === false) continue;
    const cfg = listCfg(g), ax = cfg.axis === 'x' ? 0 : 1;
    const kids = childrenOf(doc, g.id).filter(q => q.visible !== false && q.type !== 'camera')
      .map(q => { const b = boxOf(doc, q); return { l: q, b, c: ax ? (b.y0 + b.y1) / 2 : (b.x0 + b.x1) / 2 }; })
      .sort((a, b) => a.c - b.c);
    if (!kids.length) continue;
    let step = +cfg.step || 0;
    if (step <= 0) {
      const d = kids.slice(1).map((k, i) => k.c - kids[i].c).filter(v => v > 0.5).sort((a, b) => a - b);
      step = d.length ? d[Math.floor(d.length / 2)] : (ax ? kids[0].b.y1 - kids[0].b.y0 : kids[0].b.x1 - kids[0].b.x0) || 40;
    }
    const view = Math.max(1, Math.min(kids.length, Math.round(+cfg.view || 1)));
    const items = kids.map(k => {
      const ids = descendants(doc, k.l.id);
      return { id: k.l.id, ids, focus: F.findIndex(f => ids.has(f.id)) };
    });
    // caja: `view` huecos desde el primero; de lado, lo que ocupen los elementos
    let c0 = Infinity, c1 = -Infinity;
    for (const k of kids) { c0 = Math.min(c0, ax ? k.b.x0 : k.b.y0); c1 = Math.max(c1, ax ? k.b.x1 : k.b.y1); }
    const a0 = kids[0].c - step / 2, a1 = kids[0].c + (view - 0.5) * step;
    const box = ax ? { x0: c0, x1: c1, y0: a0, y1: a1 } : { x0: a0, x1: a1, y0: c0, y1: c1 };
    const L = { id: g.id, name: g.name, cfg: { ...cfg, view }, ax, n: items.length, step, box, items };
    const si = items.findIndex(it => it.focus >= 0 && F[it.focus].id === doc.focusStart);
    L.start = si >= 0 ? listTarget(L, 0, si) : 0;
    out.push(L);
  }
  return out;
}

/** desplazamiento (indice del elemento en el primer hueco) que deja visible el elemento `idx` */
export function listTarget(L, off, idx) {
  const { view, loop, center } = L.cfg, n = L.n, c = Math.floor((view - 1) / 2);
  if (!loop) {
    const mx = Math.max(0, n - view);
    if (center) return Math.max(0, Math.min(mx, idx - c));
    if (idx < off) return idx;
    if (idx > off + view - 1) return Math.min(mx, idx - view + 1);
    return Math.max(0, Math.min(mx, off));
  }
  let rel = idx - off;
  rel -= n * Math.round((rel - (view - 1) / 2) / n);   // el representante mas cercano a la ventana
  if (center) return off + rel - c;
  if (rel < 0) return off + rel;
  if (rel > view - 1) return off + rel - (view - 1);
  return off;
}

/** hueco donde se ve el elemento `idx` (puede ser negativo o >= view: fuera de la caja) */
export function listSlot(L, off, idx) {
  let s = idx - off;
  if (L.cfg.loop) { const lo = (L.cfg.view - 1) / 2 - L.n / 2; s -= L.n * Math.floor((s - lo) / L.n); }
  return s;
}

/** desplazamiento [dx, dy] y opacidad de un elemento en ese hueco */
export function listPlace(L, off, idx) {
  const s = listSlot(L, off, idx), d = (s - idx) * L.step;
  const out = Math.max(0, s < 0 ? -s : s - (L.cfg.view - 1));
  const a = L.cfg.fade ? Math.max(0, Math.min(1, 1 - out)) : (out >= 1 ? 0 : 1);
  return { dx: L.ax ? 0 : d, dy: L.ax ? d : 0, alpha: a, inside: out < 0.5 };
}

/** id de capa -> [lista, elemento] */
export function listMembers(lists) {
  const m = new Map();
  lists.forEach((L, li) => L.items.forEach((it, i) => it.ids.forEach(id => { if (!m.has(id)) m.set(id, [li, i]); })));
  return m;
}

/** vecinos dentro de las listas: en el eje de la lista, anterior/siguiente (con vuelta si es infinita) */
export function listNav(doc, nav) {
  const lists = listsOf(doc);
  for (const L of lists) {
    const withF = L.items.map((it, i) => [i, it.focus]).filter(([, f]) => f >= 0);
    const m = withF.length;
    const inL = new Set(withF.map(([, f]) => f));
    for (const [, f] of withF) {   // de lado no se salta a otro elemento de la misma lista (podria estar oculto)
      const nb = nav[f];
      if (!nb) continue;
      if (L.ax) { if (inL.has(nb.left)) nb.left = -1; if (inL.has(nb.right)) nb.right = -1; }
      else { if (inL.has(nb.up)) nb.up = -1; if (inL.has(nb.down)) nb.down = -1; }
    }
    if (m < 2) continue;
    withF.forEach(([, f], j) => {
      const prev = j > 0 ? withF[j - 1][1] : L.cfg.loop ? withF[m - 1][1] : -1;
      const next = j < m - 1 ? withF[j + 1][1] : L.cfg.loop ? withF[0][1] : -1;
      const nb = nav[f];
      if (!nb) return;
      if (L.ax) { if (prev >= 0) nb.up = prev; if (next >= 0) nb.down = next; }
      else { if (prev >= 0) nb.left = prev; if (next >= 0) nb.right = next; }
    });
  }
  return nav;
}
