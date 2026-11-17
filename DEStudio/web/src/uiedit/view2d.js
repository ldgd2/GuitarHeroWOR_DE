// Geometria de las pantallas 2D y 2.5D en el lienzo de 1280 x 720: orden de pintado, esquinas, caja, punto dentro.
// La misma cuenta que el exportador (editor/ui/modes.py: _Screen.screen, paint_order, SceneBuilder._corners).
import { H, W, cam25At, childrenOf, descendants, project25, worldOf } from './model.js';
import { gameImageUrl } from '../studio/AssetBrowser.jsx';
import { fxPad, textSize } from '../studio/raster.js';

export const assetUrl = (proj, file) => `/api/ui/custom?pid=${encodeURIComponent(proj.id)}&file=${encodeURIComponent(file)}`;
export function layerUrl(proj, l) {
  const im = l?.image || {};
  if (l?.type !== 'image') return '';
  if (im.custom) return assetUrl(proj, im.file);
  return im.key ? gameImageUrl(im.pak, im.key) : '';
}
export const VISUAL = ['image', 'text', 'shape'];

/**
 * Capas de una pantalla en orden de pintado (grupos en su sitio; 2.5D: lo lejano detras), con su estado en pantalla:
 * {l, s, x, y, k (escala total), rot (giro en pantalla), alpha, depth, pk (escala de la proyeccion), camRot}
 * Los grupos tambien salen (group: true) para poder elegirlos, pero no se dibujan.
 */
export function layout(doc, mode, clip, t, { hidden = false } = {}) {
  const is25 = mode === '25d';
  const cam = is25 ? cam25At(doc, clip, t) : null;
  const out = [];
  const walk = parent => {
    const kids = childrenOf(doc, parent).sort((a, b) => (a.base.order || 0) - (b.base.order || 0));
    for (const l of kids) {
      if (l.visible === false && !hidden) continue;
      if (l.type === 'camera') continue;
      const w = worldOf(doc, l, clip, t);
      let x = w.x, y = w.y, pk = 1;
      if (is25) {
        const pr = project25(cam, w.x, w.y, w.depth);
        if (!pr) { if (l.type === 'group') walk(l.id); continue; }
        x = pr.x; y = pr.y; pk = pr.k;
      }
      const it = { l, s: w.s, x, y, k: (w.s.scale ?? 1) * pk, rot: (w.s.rot || 0) - (is25 ? cam.rot : 0), alpha: w.alpha,
        depth: w.depth, pk, camRot: is25 ? cam.rot : 0, group: l.type === 'group' };
      out.push(it);
      if (l.type === 'group') walk(l.id);
    }
  };
  walk(null);
  if (is25) {
    const idx = new Map(out.map((it, i) => [it, i]));
    out.sort((a, b) => (b.depth - a.depth) || (idx.get(a) - idx.get(b)));
  }
  return out;
}

/** esquinas en pantalla (1280 x 720) de un elemento, con un margen opcional (pad, en unidades de la capa) */
export function corners(it, pad = 0) {
  const { s } = it;
  const w = s.w || 0, h = s.h || 0, ax = s.ax ?? 0.5, ay = s.ay ?? 0.5;
  const r = it.rot * Math.PI / 180, c = Math.cos(r), sn = Math.sin(r), k = it.k;
  return [[-ax * w - pad, -ay * h - pad], [(1 - ax) * w + pad, -ay * h - pad], [(1 - ax) * w + pad, (1 - ay) * h + pad], [-ax * w - pad, (1 - ay) * h + pad]]
    .map(([lx, ly]) => [it.x + (lx * c - ly * sn) * k, it.y + (lx * sn + ly * c) * k]);
}
export function bboxOf(pts) {
  const xs = pts.map(p => p[0]), ys = pts.map(p => p[1]);
  return { x0: Math.min(...xs), y0: Math.min(...ys), x1: Math.max(...xs), y1: Math.max(...ys) };
}
/** caja de un elemento (un grupo: la de todo lo que tiene dentro) */
export function itemBox(items, it, doc) {
  if (!it.group) return bboxOf(corners(it));
  const ids = descendants(doc, it.l.id);
  const pts = items.filter(o => !o.group && ids.has(o.l.id)).flatMap(o => corners(o));
  return pts.length ? bboxOf(pts) : { x0: it.x, y0: it.y, x1: it.x, y1: it.y };
}
export const unionBox = boxes => boxes.length ? {
  x0: Math.min(...boxes.map(b => b.x0)), y0: Math.min(...boxes.map(b => b.y0)),
  x1: Math.max(...boxes.map(b => b.x1)), y1: Math.max(...boxes.map(b => b.y1)) } : null;

/** pantalla -> coordenadas locales de la capa (respecto a su ancla, sin escalar) */
export function toLocal(it, X, Y) {
  const r = it.rot * Math.PI / 180, c = Math.cos(r), s = Math.sin(r);
  const dx = (X - it.x) / (it.k || 1e-6), dy = (Y - it.y) / (it.k || 1e-6);
  return [dx * c + dy * s, -dx * s + dy * c];
}
export function hits(it, X, Y, slop = 0) {
  if (it.group) return false;
  const [lx, ly] = toLocal(it, X, Y);
  const { w = 0, h = 0, ax = 0.5, ay = 0.5 } = it.s;
  const e = slop / (it.k || 1);
  return lx >= -ax * w - e && lx <= (1 - ax) * w + e && ly >= -ay * h - e && ly <= (1 - ay) * h + e;
}

/** delta en pantalla -> delta en el mundo de la capa (2.5D: deshace la proyeccion y el giro de la camara) */
export function screenToWorldDelta(it, dx, dy) {
  if (!it.pk || it.pk === 1 && !it.camRot) return [dx, dy];
  const r = it.camRot * Math.PI / 180, c = Math.cos(r), s = Math.sin(r);
  return [(dx * c - dy * s) / it.pk, (dx * s + dy * c) / it.pk];
}

/** ajusta ancho y alto de un texto a su contenido (caja automatica) o el alto (caja fija) */
export function fitText(l) {
  if (l.type !== 'text') return;
  if (l.text.box === 'fixed') l.base.h = textSize(l.text, l.base.w)[1];
  else [l.base.w, l.base.h] = textSize(l.text);
}

export const hasFx = l => { const f = l.fx || {}; return !!(f.shadow?.on || f.glow?.on || f.stroke?.on || f.blur); };
export { fxPad, W, H };
