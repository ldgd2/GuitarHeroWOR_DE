// Transiciones entre claves (como se va de una clave a la siguiente). Comun a camaras y poses de personaje.
//   ease de la clave = curva de TIEMPO (cuanto del camino se ha recorrido en cada instante)
//   path de la clave = forma del CAMINO en el espacio ('curve' Catmull-Rom por las claves vecinas, 'line' recto)
// Familias de Robert Penner (las de After Effects, Unity, Blender, CSS...): seno, cuadratica, cubica, cuartica,
// quintica, exponencial, circular, retroceso (back), elastica y rebote; cada una con entrada, salida, entrada y
// salida, y salida y entrada. Mas: lineal, mantener (corte seco), escalones, Bezier propia (4 valores como CSS
// cubic-bezier) y las antiguas 'smooth', 'easeIn', 'easeOut', 'easeInOut' (se conservan para los proyectos guardados).
const { PI, sin, cos, sqrt, pow, abs, min, max } = Math;

const c1 = 1.70158, c2 = c1 * 1.525, c3 = c1 + 1, c4 = (2 * PI) / 3, c5 = (2 * PI) / 4.5;
function bounceOut(x) {
  const n1 = 7.5625, d1 = 2.75;
  if (x < 1 / d1) return n1 * x * x;
  if (x < 2 / d1) return n1 * (x -= 1.5 / d1) * x + 0.75;
  if (x < 2.5 / d1) return n1 * (x -= 2.25 / d1) * x + 0.9375;
  return n1 * (x -= 2.625 / d1) * x + 0.984375;
}
// curvas de entrada (in); salida, entrada-salida y salida-entrada se derivan
const IN = {
  sine: x => 1 - cos((x * PI) / 2),
  quad: x => x * x,
  cubic: x => x * x * x,
  quart: x => x ** 4,
  quint: x => x ** 5,
  expo: x => (x === 0 ? 0 : pow(2, 10 * x - 10)),
  circ: x => 1 - sqrt(1 - x * x),
  back: x => c3 * x * x * x - c1 * x * x,
  elastic: x => (x === 0 ? 0 : x === 1 ? 1 : -pow(2, 10 * x - 10) * sin((x * 10 - 10.75) * c4)),
  bounce: x => 1 - bounceOut(1 - x),
};
// entrada-salida con la forma propia de back/elastic (las genericas quedan blandas)
const INOUT_SPECIAL = {
  back: x => (x < 0.5 ? (pow(2 * x, 2) * ((c2 + 1) * 2 * x - c2)) / 2 : (pow(2 * x - 2, 2) * ((c2 + 1) * (x * 2 - 2) + c2) + 2) / 2),
  elastic: x => (x === 0 ? 0 : x === 1 ? 1 : x < 0.5 ? -(pow(2, 20 * x - 10) * sin((20 * x - 11.125) * c5)) / 2
    : (pow(2, -20 * x + 10) * sin((20 * x - 11.125) * c5)) / 2 + 1),
};
const outOf = f => x => 1 - f(1 - x);
const inOutOf = f => x => (x < 0.5 ? f(2 * x) / 2 : 1 - f(2 - 2 * x) / 2);
const outInOf = f => { const o = outOf(f); return x => (x < 0.5 ? o(2 * x) / 2 : 0.5 + f(2 * x - 1) / 2); };

export const FAMILIES = [
  ['sine', 'Seno'], ['quad', 'Cuadratica'], ['cubic', 'Cubica'], ['quart', 'Cuartica'], ['quint', 'Quintica'],
  ['expo', 'Exponencial'], ['circ', 'Circular'], ['back', 'Retroceso'], ['elastic', 'Elastica'], ['bounce', 'Rebote'],
];
export const VARIANTS = [['In', 'entrada'], ['Out', 'salida'], ['InOut', 'entrada y salida'], ['OutIn', 'salida y entrada']];

const FN = {
  linear: x => x,
  smooth: x => x,                                  // antigua "curva suave": tiempo lineal sobre camino curvo
  hold: () => 0,
  easeIn: IN.cubic, easeOut: outOf(IN.cubic), easeInOut: inOutOf(IN.cubic),   // nombres antiguos
};
for (const [fam] of FAMILIES) {
  const f = IN[fam];
  FN[fam + 'In'] = f; FN[fam + 'Out'] = outOf(f); FN[fam + 'InOut'] = INOUT_SPECIAL[fam] || inOutOf(f); FN[fam + 'OutIn'] = outInOf(f);
}

// Bezier de tiempo (como cubic-bezier de CSS): (0,0) (x1,y1) (x2,y2) (1,1); x1 y x2 entre 0 y 1
export const DEFAULT_BEZ = [0.42, 0, 0.58, 1];
export function bezier(b, x) {
  const [x1, y1, x2, y2] = b || DEFAULT_BEZ;
  const cx = 3 * x1, bx = 3 * (x2 - x1) - cx, ax = 1 - cx - bx, cy = 3 * y1, by = 3 * (y2 - y1) - cy, ay = 1 - cy - by;
  const X = t => ((ax * t + bx) * t + cx) * t, dX = t => (3 * ax * t + 2 * bx) * t + cx;
  let t = x;
  for (let i = 0; i < 8; i++) { const e = X(t) - x; if (abs(e) < 1e-6) break; const d = dX(t); if (abs(d) < 1e-6) break; t -= e / d; }
  if (abs(X(t) - x) > 1e-4) {   // biseccion si Newton no converge
    let lo = 0, hi = 1; t = x;
    for (let i = 0; i < 30; i++) { if (X(t) < x) lo = t; else hi = t; t = (lo + hi) / 2; }
  }
  return ((ay * t + by) * t + cy) * t;
}

/** f(x) de la transicion de la clave k (0..1 -> 0..1, puede pasarse de 0 o 1 en back/elastic) */
export function easeValue(name, k, x) {
  x = min(1, max(0, x));
  if (name === 'bezier') return bezier(k?.bez, x);
  if (name === 'steps') { const n = max(1, k?.steps || 4); return min(1, Math.floor(x * n) / n); }
  return (FN[name] || FN.linear)(x);
}
export const knownEase = n => n === 'bezier' || n === 'steps' || n in FN;

// Lista para menus: [clave, etiqueta, grupo]
export const EASE_BASIC = [['auto', 'Como la toma', 'Basicas'], ['smooth', 'Curva suave (automatica)', 'Basicas'], ['linear', 'Lineal', 'Basicas'],
  ['hold', 'Mantener (corte seco)', 'Basicas'], ['steps', 'Escalones', 'Basicas'], ['bezier', 'Bezier propia', 'Basicas'],
  ['easeIn', 'Acelerar (cubica)', 'Basicas'], ['easeOut', 'Frenar (cubica)', 'Basicas'], ['easeInOut', 'Acelerar y frenar (cubica)', 'Basicas']];
export const EASES = [...EASE_BASIC];
for (const [fam, fl] of FAMILIES) for (const [v, vl] of VARIANTS) EASES.push([fam + v, `${fl}, ${vl}`, fl]);
export const easeLabel = n => EASES.find(e => e[0] === n)?.[1] || n;

/** puntos SVG (viewBox 0 0 100 100, con margen para back/elastic) de una transicion, para miniaturas */
export function easePath(name, k, n = 40) {
  const pts = [];
  for (let i = 0; i <= n; i++) {
    const x = i / n, y = name === 'hold' ? (i === n ? 1 : 0) : easeValue(name, k, x);
    pts.push(`${(10 + x * 80).toFixed(1)},${(85 - y * 70).toFixed(1)}`);
  }
  return pts.join(' ');
}
