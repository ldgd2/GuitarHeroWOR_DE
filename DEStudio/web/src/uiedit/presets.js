// Ayudas que montan cosas comunes de un menu de un golpe (Pantalla > ...):
//  - indicaciones de botones (gema del color del traste + texto: "verde continuar, rojo volver")
//  - animaciones de entrada y salida automaticas para las capas de la pantalla
// La misma cuenta esta en el script de proyectos de ejemplo; si cambias algo aqui, cambialo alli.
import { W, H, newLayer } from './model.js';
import { listsOf } from './lists.js';

export const FRET_COLORS = {
  green: [46, 170, 70], red: [205, 32, 32], yellow: [240, 196, 30], blue: [40, 112, 214], orange: [242, 122, 28],
};
export const HINT_KINDS = [['green', 'Verde'], ['red', 'Rojo'], ['yellow', 'Amarillo'], ['blue', 'Azul'], ['orange', 'Naranja'], ['strum', 'Rasgueo']];
export const defaultHints = doc => [
  ['green', 'Continue'], ['red', 'Back'],
  ...(listsOf(doc).length ? [['strum', 'Move']] : []),
];

/** capas de una barra de indicaciones abajo a la izquierda: [grupo, ...hijos]. measure(texto, px) -> ancho */
export function hintLayers(mode, items, { font = 'Rocking', px = 30, color = [0, 0, 0], measure, left = 64, y = H - 42, rot = -4 } = {}) {
  const g = newLayer(mode, 'group', { name: 'indicaciones de botones', base: { x: 0, y: 0, w: 0, h: 0 } });
  const out = [g];
  let x = left;
  items.forEach(([kind, label], i) => {
    const strum = kind === 'strum';
    const c = strum ? [40, 36, 34] : FRET_COLORS[kind] || [255, 255, 255];
    const gw = strum ? 16 : 28, gh = strum ? 34 : 28;
    const tw = Math.round((measure ? measure(label, px) : label.length * px * 0.55) + px * 0.2);
    const gem = newLayer(mode, 'shape', {
      name: `indicacion ${kind}`, parent: g.id,
      shape: { kind: strum ? 'rect' : 'ellipse', radius: strum ? 8 : 0,
        fill: { type: 'solid', color: c, alpha: 1, color2: c, alpha2: 1, angle: 90 },
        stroke: { width: 3, color: [24, 12, 10], alpha: 1, dash: 0 } },
      base: { x: x + gw / 2, y: y + 1, w: gw, h: gh, ax: 0.5, ay: 0.5, rot, order: i * 2 + 1 },
    });
    const t = newLayer(mode, 'text', {
      name: `indicacion ${kind} texto`, parent: g.id,
      text: { value: label, font, px, color: [...color], align: 'left' },
      base: { x: x + gw + 10, y, w: tw, h: Math.round(px * 1.31), ax: 0, ay: 0.5, rot, order: i * 2 + 2 },
    });
    x += gw + 10 + tw + 38;
    out.push(gem, t);
  });
  return out;
}

// ------------------------------------------------------------------ entrada y salida automaticas
const BIG = 0.9;   // una capa que tapa casi toda la pantalla es fondo: no se anima (las pantallas comparten fondo)

/** pistas de entrada y salida para las capas de primer nivel de la pantalla. Devuelve {intro, outro} */
export function autoTransitions(doc, { intro = 0.7, outro = 0.35 } = {}) {
  const top = doc.layers.filter(l => !l.parent && l.visible !== false && l.type !== 'camera')
    .sort((a, b) => (a.base.order || 0) - (b.base.order || 0));
  const isBg = l => l.type !== 'group' && (l.base.w || 0) * (l.base.scale ?? 1) >= W * BIG && (l.base.h || 0) * (l.base.scale ?? 1) >= H * BIG;
  const anim = top.filter(l => !isBg(l));
  const tin = {}, tout = {};
  const key = (t, v, ease) => (ease ? { t: +t.toFixed(3), v, ease } : { t: +t.toFixed(3), v });
  let n = 0;
  anim.forEach((l, i) => {
    const b = l.base, x = b.x, y = b.y, a = b.alpha ?? 1;
    const title = l.type === 'text' && (l.text?.px || 0) >= 60;
    const hints = l.name === 'indicaciones de botones';
    // lo que esta a la derecha (la calavera y sus ojos) entra a la vez, como una pieza
    const fromRight = l.type !== 'group' && x + (b.w || 0) * (0.5 - (b.ax ?? 0.5)) > W * 0.6;
    const d = fromRight ? 0 : Math.min(0.3, 0.06 + n++ * 0.05);   // el resto, escalonado
    const t1 = Math.min(intro, d + 0.4);
    let dx = 0, dy = 0;
    if (title) dy = -50; else if (hints) dy = 70; else if (fromRight) dx = 160; else dx = -90;
    const ease = title ? 'backOut' : 'cubicOut';
    tin[l.id] = { alpha: [key(0, 0), key(d, 0), key(t1, a, 'cubicOut')] };
    if (dx) tin[l.id].x = [key(0, x + dx), key(d, x + dx), key(t1, x, ease)];
    if (dy) tin[l.id].y = [key(0, y + dy), key(d, y + dy), key(t1, y, ease)];
    const o0 = Math.min(outro * 0.4, i * 0.015), o1 = outro;
    tout[l.id] = { alpha: [key(0, a), key(o0, a), key(o1, 0, 'cubicIn')] };
    if (dx) tout[l.id].x = [key(0, x), key(o0, x), key(o1, x + dx * 0.5, 'cubicIn')];
    if (dy) tout[l.id].y = [key(0, y), key(o0, y), key(o1, y + dy * 0.5, 'cubicIn')];
  });
  return { intro: { duration: intro, tracks: tin }, outro: { duration: outro, tracks: tout } };
}
