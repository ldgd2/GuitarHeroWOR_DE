// Modelo del editor de interfaces (version 4): un proyecto tiene varias PANTALLAS; cada pantalla tiene sus capas, sus
// clips (Entrada, Bucle, Salida) y, segun el tipo, su camara 2.5D o sus camaras 3D. Las capas pueden ser botones
// (foco con el mando o el raton) con una accion: ir a otra pantalla, volver, cerrar, ejecutar un script del juego o
// cambiar una variable del juego.
//
// El exportador (DEStudio/editor/ui/modes.py) lee este mismo modelo: si cambias algo aqui, cambialo alli.
// El juego lo dibuja re-wor (src/de/ui3d.cpp) en los tres tipos; el navegador convierte cada capa en imagen.
import { evalTracks, removeKey as rmKey, setKey, trackSet, uid } from '../studio/keys.js';

export { History, hex, unhex, fmt, keyAt as keyAtClip } from '../studio/keys.js';

export const W = 1280, H = 720;
export const MODES = {
  '2d': { label: '2D', desc: 'Pantallas planas: imagenes del juego o tuyas (PNG, SVG), textos con cualquier fuente, formas vectoriales y estilos de capa.' },
  '25d': { label: '2.5D', desc: 'Capas a distintas profundidades y una camara que se mueve, hace zoom y gira: paralaje y profundidad.' },
  '3d': { label: '3D', desc: 'Escena 3D con planos en el espacio, varias camaras con claves y cortes entre ellas, perspectiva real.' },
};
export const CLIPS = [
  ['base', 'Base', 'Como queda quieta: la pantalla sin animar'],
  ['intro', 'Entrada', 'Al abrirse la pantalla'],
  ['loop', 'Bucle', 'Se repite mientras esta abierta, despues de la entrada'],
  ['outro', 'Salida', 'Al cerrarse o al pasar a otra pantalla'],
];
export const TYPES = { image: 'Imagen', text: 'Texto', shape: 'Forma', group: 'Grupo', camera: 'Camara' };
export const TYPE_ICON = { image: 'tex', text: 'text', shape: 'rect', group: 'folder', camera: 'cam' };
export const SHAPES = { rect: 'Rectangulo', ellipse: 'Elipse', polygon: 'Poligono', star: 'Estrella', line: 'Linea', path: 'Trazado' };
export const SHAPE_ICON = { rect: 'rect', ellipse: 'ellipse', polygon: 'polygon', star: 'star', line: 'line', path: 'pen' };
export const CAM25 = '__cam';            // id de la camara 2.5D en las pistas de los clips
export const PERSP = 800;                // distancia de la camara 2.5D al plano de profundidad 0 (modes.py: PERSP)
export const CAM3D_Z = 772;              // camara 3D por defecto: con 50 grados ve 1280 x 720 en z = 0

// ------------------------------------------------------------------ logica (misma semantica en runtime.js, modes.py y
// re-wor src/de/ui3d.cpp; si cambias algo aqui, cambialo alli)
//
// Fuentes de datos (cadena "src"), lo que pueden mostrar, mover o condicionar las capas:
//   nombre            variable propia de la interfaz (proj.vars; en el juego, la global re_ui_<interfaz>_v_<nombre>)
//   $global           variable global del juego (entera, real o texto)
//   $a.b / (expr)     expresion QB: un script de la interfaz la copia cada frame a una global propia
//   @Funcion args     llamada a una funcion del juego que devuelve un parametro: "@GetPlayerInfo 1 score" lee
//                     <score>; con "=> nombre" se elige el parametro devuelto
export const ACTIONS = [
  ['none', 'Nada (lo hace su script)'],
  ['goto', 'Ir a pantalla'], ['back', 'Volver a la anterior'], ['close', 'Cerrar la interfaz'],
  ['set', 'Poner un valor'], ['add', 'Sumar a un valor'], ['toggle', 'Alternar 0 / 1'],
  ['state', 'Abrir pantalla del juego'], ['script', 'Ejecutar script del juego'], ['sound', 'Sonido de menu'],
  ['open', 'Abrir otra interfaz'],
];
export const CONTROLS = [
  ['button', 'Boton', 'Al elegirlo hace sus acciones'],
  ['toggle', 'Interruptor', 'Al elegirlo cambia su variable entre 0 y 1'],
  ['slider', 'Deslizador', 'Izquierda / derecha cambian su variable entre minimo y maximo (o clic en su sitio)'],
  ['cycle', 'Selector', 'Izquierda / derecha recorren opciones y dan la vuelta'],
];
export const EVENTS = { actions: 'Al elegir', onChange: 'Al cambiar su valor', onFocus: 'Al recibir el foco' };
export const BIND_PROPS = [['x', 'Mover X (px)'], ['y', 'Mover Y (px)'], ['rot', 'Girar (grados)'], ['scale', 'Escala'],
  ['alpha', 'Opacidad'], ['fill', 'Relleno (barra)']];
export const FILL_DIRS = [['right', 'Hacia la derecha'], ['left', 'Hacia la izquierda'], ['up', 'Hacia arriba'], ['down', 'Hacia abajo']];
export const SOUNDS = ['select', 'back', 'ScrollUp', 'ScrollDown', 'ScrollLeft', 'ScrollRight', 'toggle', 'ToggleOn', 'ToggleOff',
  'negativeselect', 'SelectAlt1', 'SelectAlt2', 'warningmessage'];
export const OPS = ['=', '!=', '>', '<', '>=', '<='];
// scripts del juego que se usan a menudo desde un menu (el Explorador del juego tiene todos)
export const SCRIPT_PRESETS = [
  ['ui_event event = menu_back', 'Volver al menu anterior del juego'],
  ['UI_SFX menustate = generic uitype = select', 'Sonido de elegir'],
  ['re_ctl_open', 'Abrir Options > Controls (re-wor)'],
];
export const newAction = (type = 'goto') => ({ type, screen: '', script: '', target: '', value: 1, state: '', sound: 'select', ui: '' });
/** accion al pulsar un boton: la primera de ui.actions (o ui.action, formato anterior); la variable sale de target ($nombre) */
export function actionOf(l) {
  const a = l?.ui?.action || l?.ui?.actions?.[0] || {};
  return { ...a, global: a.global || (a.target?.startsWith('$') ? a.target.slice(1) : '') };
}
/** variable de la condicion "mostrar segun una variable" (formato anterior: global; actual: src $nombre) */
export const condVar = c => c?.global || (c?.src?.startsWith('$') ? c.src.slice(1) : '');
/** se cumple la condicion "mostrar segun una variable" con estos valores (los que falten valen 0) */
export function condOk(c, vars) {
  const g = condVar(c);
  if (!g) return true;
  const v = +(vars?.[g] ?? 0) || 0, x = +c.value || 0;
  return { '=': v === x, '!=': v !== x, '>': v > x, '<': v < x, '>=': v >= x, '<=': v <= x }[c.op || '='] ?? true;
}
/** variables propias de la interfaz: empiezan por re_ui_ (el exportador las declara; las demas son del juego) */
export const ownVar = g => /^re_ui_[a-z0-9_]+$/i.test(g || '');
/** el panel guardado al pasar una capa a codigo -> ui (acepta el formato anterior con una sola accion) */
export function restorePanel(l) {
  const p = l.script?.panel;
  if (!p || !l.ui) return;
  if (Array.isArray(p.actions)) l.ui.actions = p.actions;
  else { const a = p.action || {}; l.ui.actions = a.type && a.type !== 'none' ? [{ ...newAction(a.type), ...a, target: a.global ? '$' + a.global : a.target || '' }] : []; }
  delete l.ui.action;
  if (p.hover) l.ui.hover = p.hover;
}

/** tipo de una fuente: var | global | expr | call | '' */
export function srcKind(src) {
  const t = (src || '').trim();
  if (!t) return '';
  if (t[0] === '@') return 'call';
  if (t[0] === '$') return /^\$[A-Za-z_][A-Za-z0-9_]*$/.test(t) ? 'global' : 'expr';
  if (t[0] === '(') return 'expr';
  return /^[a-z0-9_]+$/.test(t) ? 'var' : 'expr';
}
/** se puede escribir (variable propia o global del juego sin expresion) */
export const writable = src => ['var', 'global'].includes(srcKind(src));

/** "{src}" y "{src:formato}" en un texto -> [{lit} | {src, fmt}] */
export function parseTemplate(text) {
  const out = [];
  const re = /\{([^{}:]+)(?::([^{}]*))?\}/g;
  let last = 0, m;
  while ((m = re.exec(text || ''))) {
    if (m.index > last) out.push({ lit: text.slice(last, m.index) });
    out.push({ src: m[1].trim(), fmt: (m[2] || '').trim() });
    last = m.index + m[0].length;
  }
  if (last < (text || '').length) out.push({ lit: text.slice(last) });
  return out;
}
export const isDynamicText = l => l.type === 'text' && parseTemplate(l.text?.value).some(x => x.src);

/** valor -> texto (igual que ui3d.cpp Format): opt=A|B|C, time (m:ss), sep (miles), 0..6 decimales */
export function formatValue(v, fmt = '') {
  if (fmt.startsWith('opt=')) { const o = fmt.slice(4).split('|'); return o[Math.round(+v || 0)] ?? ''; }
  if (typeof v === 'string') return v;
  const n = +v || 0;
  if (fmt === 'time') { const t = Math.max(0, Math.floor(n)); return `${Math.floor(t / 60)}:${String(t % 60).padStart(2, '0')}`; }
  if (fmt === 'sep') return Math.round(n).toString().replace(/\B(?=(\d{3})+(?!\d))/g, ',');
  if (/^[0-6]$/.test(fmt)) return n.toFixed(+fmt);
  return Number.isInteger(n) ? String(n) : n.toFixed(2);
}
/** valor de un enlace: entrada [a, b] -> salida [c, d], limitado */
export function mapBind(v, b) {
  const a = +(b.in?.[0] ?? 0), bb = +(b.in?.[1] ?? 1), c = +(b.out?.[0] ?? 0), d = +(b.out?.[1] ?? 1);
  const f = bb === a ? 0 : Math.max(0, Math.min(1, ((+v || 0) - a) / (bb - a)));
  return c + (d - c) * f;
}
/** todas las fuentes que usa el proyecto (para el panel Datos, la vista previa y el exportador) */
export function sourcesOf(proj) {
  const out = new Set();
  const add = s => { if (s && srcKind(s)) out.add(s.trim()); };
  const acts = list => (list || []).forEach(a => { if (['set', 'add', 'toggle'].includes(a.type)) add(a.target); });
  for (const sc of proj.screens) {
    acts(sc.onEnter); acts(sc.onExit);
    for (const l of sc.layers) {
      if (l.visible === false) continue;
      add(l.showIf?.src);
      (l.binds || []).forEach(b => add(b.src));
      if (l.type === 'text') parseTemplate(l.text?.value).forEach(x => x.src && add(x.src));
      if (l.ui?.focus) { add(l.ui.var); acts(l.ui.actions); acts(l.ui.onChange); acts(l.ui.onFocus); }
    }
  }
  return [...out];
}

// efectos en vivo (animables; el juego los dibuja cada fotograma, no se hornean en la imagen)
export const LIVE_DEFAULTS = { e_glow: 0, e_glowc: [255, 200, 120], e_glowa: 1, e_blur: 0, e_chroma: 0, e_bright: 1, e_sat: 1, e_inv: 0, e_hue: 0, e_grain: 0, e_flicker: 0, e_shake: 0 };
// modos de fusion (los dibujan el editor y el juego)
export const BLEND_MODES = [['normal', 'Normal'], ['multiply', 'Multiplicar'], ['screen', 'Trama'], ['add', 'Sobreexponer lineal (sumar)'],
  ['darken', 'Oscurecer'], ['lighten', 'Aclarar']];
export const BLEND_CSS = { multiply: 'multiply', screen: 'screen', add: 'plus-lighter', darken: 'darken', lighten: 'lighten' };

// propiedades animables: [clave, etiqueta, paso, min, max]
const P = {
  x: ['x', 'X', 1], y: ['y', 'Y', 1], z: ['z', 'Z', 1], depth: ['depth', 'Profundidad', 1],
  w: ['w', 'Ancho', 1, 0], h: ['h', 'Alto', 1, 0], scale: ['scale', 'Escala', 0.01, 0], rot: ['rot', 'Giro', 1],
  rx: ['rx', 'Giro X', 1], ry: ['ry', 'Giro Y', 1], rz: ['rz', 'Giro Z', 1],
  alpha: ['alpha', 'Opacidad', 0.01, 0, 1], color: ['color', 'Tinte'], fov: ['fov', 'Campo de vision', 1, 5, 170],
  zoom: ['zoom', 'Zoom', 0.01, 0.05],
  e_glow: ['e_glow', 'Resplandor', 1, 0, 200], e_glowc: ['e_glowc', 'Color del resplandor'], e_glowa: ['e_glowa', 'Fuerza del resplandor', 0.05, 0, 1],
  e_blur: ['e_blur', 'Desenfoque', 0.5, 0, 100], e_chroma: ['e_chroma', 'Aberracion cromatica', 0.5, 0, 60],
  e_bright: ['e_bright', 'Brillo', 0.05, 0, 2], e_sat: ['e_sat', 'Saturacion', 0.05, 0, 1], e_inv: ['e_inv', 'Invertir', 0.05, 0, 1],
  e_hue: ['e_hue', 'Tono', 1, -180, 180], e_grain: ['e_grain', 'Grano', 0.05, 0, 1], e_flicker: ['e_flicker', 'Parpadeo', 0.05, 0, 1],
  e_shake: ['e_shake', 'Temblor (px)', 0.5, 0, 60],
};
export const LIVE_PROPS = Object.keys(LIVE_DEFAULTS).map(k => P[k]);
export const COLOR_PROPS = new Set(['color', 'e_glowc']);
export function propsFor(mode, type) {
  const pick = ks => ks.map(k => P[k]);
  if (type === CAM25) return pick(['x', 'y', 'zoom', 'rot']);
  if (mode === '3d') {
    if (type === 'camera') return pick(['x', 'y', 'z', 'rx', 'ry', 'rz', 'fov']);
    if (type === 'group') return pick(['x', 'y', 'z', 'rx', 'ry', 'rz', 'scale', 'alpha']);
    return pick(['x', 'y', 'z', 'rx', 'ry', 'rz', 'scale', 'w', 'h', 'alpha', 'color']);
  }
  if (type === 'group') return pick(['x', 'y', ...(mode === '25d' ? ['depth'] : []), 'alpha']);
  return pick(['x', 'y', ...(mode === '25d' ? ['depth'] : []), 'w', 'h', 'scale', 'rot', 'alpha', 'color']);
}
export const typesFor = mode => Object.keys(TYPES).filter(t => t !== 'camera' || mode === '3d');

const BASE2D = { x: W / 2, y: H / 2, w: 200, h: 120, scale: 1, rot: 0, alpha: 1, color: [255, 255, 255], order: 10, ax: 0.5, ay: 0.5, depth: 0, ...LIVE_DEFAULTS };
const BASE3D = { x: 0, y: 0, z: 0, rx: 0, ry: 0, rz: 0, w: 400, h: 225, scale: 1, alpha: 1, color: [255, 255, 255], ax: 0.5, ay: 0.5, order: 10, fov: 50, ...LIVE_DEFAULTS };

export const DEFAULT_TEXT = { value: 'Texto', font: 'Inter Variable', weight: 700, italic: false, px: 48, color: [255, 255, 255], align: 'center',
  spacing: 0, line: 1.15, upper: false, box: 'auto' };
export const DEFAULT_SHAPE = { kind: 'rect', radius: 0, sides: 5, inner: 0.5, points: [], closed: true,
  fill: { type: 'solid', color: [227, 160, 67], alpha: 1, color2: [120, 60, 20], alpha2: 1, angle: 90 },
  stroke: { width: 0, color: [255, 255, 255], alpha: 1, dash: 0 } };
export const DEFAULT_FX = {
  shadow: { on: false, dx: 0, dy: 6, blur: 12, color: [0, 0, 0], alpha: 0.6 },
  glow: { on: false, size: 14, color: [255, 200, 120], alpha: 0.8 },
  stroke: { on: false, width: 3, color: [0, 0, 0], alpha: 1 },
  blur: 0,
};
export const DEFAULT_UI = { focus: false, order: 0, control: 'button', var: '', min: 0, max: 10, step: 1,
  actions: [], onChange: [], onFocus: [],
  sound: true, hover: { scale: 1.08, color: [255, 220, 150], alpha: 1, dx: 0, dy: 0 } };

const clipsNew = mode => {
  const c = { intro: { duration: 0.8, tracks: {} }, loop: { duration: 2, tracks: {} }, outro: { duration: 0.5, tracks: {} } };
  if (mode === '3d') for (const k in c) c[k].cuts = [];
  return c;
};

export function newScreen(mode, name = 'Pantalla') {
  const s = { id: uid('s'), name, layers: [], clips: clipsNew(mode), onBack: 'auto', focusStart: null, onEnter: [], onExit: [] };
  if (mode === '25d') s.cam25 = { x: W / 2, y: H / 2, zoom: 1, rot: 0 };
  if (mode === '3d') {
    const cam = newLayer('3d', 'camera', { name: 'Camara 1' });
    cam.base.z = CAM3D_Z;
    s.layers.push(cam);
    s.activeCam = cam.id;
  }
  return s;
}

export function newProject(mode = '2d', name = 'mi_interfaz') {
  const s = newScreen(mode, 'Principal');
  return { version: 4, id: uid('p'), name, mode, screens: [s], start: s.id, fonts: [], vars: [], settings: { quality: 2, capture: true } };
}

export function newLayer(mode, type, extra = {}) {
  const base = mode === '3d' ? { ...BASE3D, color: [255, 255, 255] } : { ...BASE2D, color: [255, 255, 255] };
  const l = { id: uid(type[0]), name: TYPES[type], type, parent: null, visible: true, locked: false, base,
    fx: JSON.parse(JSON.stringify(DEFAULT_FX)), ui: JSON.parse(JSON.stringify(DEFAULT_UI)), showIf: null, showOnFocus: null, binds: [] };
  if (type === 'text') l.text = { ...DEFAULT_TEXT, color: [...DEFAULT_TEXT.color] };
  if (type === 'shape') l.shape = JSON.parse(JSON.stringify(DEFAULT_SHAPE));
  if (type === 'image') l.image = { pak: '', key: '', name: '' };
  if (type === 'group' && mode !== '3d') { base.w = 0; base.h = 0; }
  const out = Object.assign(l, extra, { base: { ...base, ...(extra.base || {}) } });
  if (extra.shape) out.shape = { ...JSON.parse(JSON.stringify(DEFAULT_SHAPE)), ...extra.shape };
  if (extra.text) out.text = { ...DEFAULT_TEXT, ...extra.text };
  return out;
}

// ------------------------------------------------------------------ lectura de disco / versiones anteriores
const TEXT_PX_OLD = { text: 50, title: 62 };
function upgradeLayer(mode, l) {
  const t = l.type === 'rect' ? 'shape' : l.type;
  const d = newLayer(mode, TYPES[t] ? t : 'shape');
  const q = { ...d, ...l, type: TYPES[t] ? t : 'shape', id: l.id || d.id, name: l.name || d.name, base: { ...d.base, ...(l.base || {}) },
    fx: { ...d.fx, ...(l.fx || {}) }, ui: { ...d.ui, ...(l.ui || {}), hover: { ...d.ui.hover, ...(l.ui?.hover || {}) } }, binds: l.binds || [] };
  if (l.ui?.action && !l.ui.actions) {   // v3: una sola accion
    const a = l.ui.action;
    q.ui.actions = a.type && a.type !== 'none' ? [{ ...newAction(a.type), ...a, target: a.global ? '$' + a.global : '' }] : [];
  }
  delete q.ui.action;
  for (const k of ['actions', 'onChange', 'onFocus']) q.ui[k] = (q.ui[k] || []).map(a => ({ ...newAction(a.type), ...a }));
  if (l.showIf?.global && !l.showIf.src) q.showIf = { src: '$' + l.showIf.global, op: l.showIf.op || '=', value: l.showIf.value ?? 1 };
  if (mode !== '3d' && l.base && l.base.order === undefined && l.base.z !== undefined) q.base.order = l.base.z;   // v2: z = orden en 2D
  if (l.type === 'rect') {   // v2: rectangulo de color plano = forma con relleno de su color, sin tinte
    q.shape = JSON.parse(JSON.stringify(DEFAULT_SHAPE));
    q.shape.fill.color = [...(l.base?.color || [40, 33, 27])];
    q.base.color = [255, 255, 255];
  }
  if (q.type === 'shape') q.shape = { ...JSON.parse(JSON.stringify(DEFAULT_SHAPE)), ...(q.shape || {}) };
  if (q.type === 'text') {
    const o = l.text || {};
    q.text = { ...DEFAULT_TEXT, ...o };
    if (o.size !== undefined && o.px === undefined) {   // v2: fuentes del juego con escala
      q.text.px = Math.round(o.size * (TEXT_PX_OLD[o.font] || 50));
      q.text.font = o.font === 'title' ? 'Georgia' : 'Inter Variable';
      delete q.text.size;
    }
  }
  if (q.type === 'image') q.image = { pak: '', key: '', name: '', ...(l.image || {}) };
  return q;
}

/** completa un proyecto leido de disco (o de una version anterior) con los valores que falten */
export function normalize(p) {
  const mode = p?.mode || '2d';
  const d = newProject(mode, p?.name || 'mi_interfaz');
  let screens = p?.screens;
  if (!screens?.length) {   // v1/v2: una sola pantalla en la raiz del proyecto
    const s = newScreen(mode, 'Principal');
    s.layers = p?.layers || (mode === '3d' ? s.layers : []);
    s.clips = { ...s.clips, ...(p?.clips || {}) };
    if (p?.cam25) s.cam25 = p.cam25;
    if (p?.activeCam) s.activeCam = p.activeCam;
    screens = [s];
  }
  const q = { ...d, ...p, mode, version: 4, settings: { ...d.settings, ...(p?.settings || {}) }, fonts: p?.fonts || [], vars: p?.vars || [] };
  delete q.layers; delete q.clips; delete q.cam25; delete q.activeCam;
  q.screens = screens.map(s0 => {
    const s = { ...newScreen(mode, s0.name || 'Pantalla'), ...s0 };
    s.onEnter = (s0.onEnter || []).map(a => ({ ...newAction(a.type), ...a }));
    s.onExit = (s0.onExit || []).map(a => ({ ...newAction(a.type), ...a }));
    s.layers = (s0.layers || []).filter(l => mode === '3d' || l.type !== 'camera').map(l => upgradeLayer(mode, l));
    s.clips = { ...s.clips };
    for (const c of ['intro', 'loop', 'outro']) {
      s.clips[c] = { duration: 1, tracks: {}, ...(s0.clips?.[c] || {}) };
      if (mode === '3d') s.clips[c].cuts ??= [];
    }
    if (mode === '25d') s.cam25 = { x: W / 2, y: H / 2, zoom: 1, rot: 0, ...(s0.cam25 || {}) };
    if (mode === '3d' && !s.layers.some(l => l.id === s.activeCam)) s.activeCam = s.layers.find(l => l.type === 'camera')?.id || null;
    return s;
  });
  if (!q.screens.some(s => s.id === q.start)) q.start = q.screens[0].id;
  return q;
}

export const clean = n => (n || '').toLowerCase().replace(/[^a-z0-9_]+/g, '_').replace(/^_+|_+$/g, '').slice(0, 40);

// ------------------------------------------------------------------ estado en un instante (sobre una pantalla)
// "doc" = una pantalla (layers, clips, cam25, activeCam). Todas estas funciones trabajan sobre una pantalla.
export const clipOf = (doc, clip) => (clip === 'base' ? null : doc.clips[clip]);
export const stateAt = (doc, l, clip, t) => evalTracks(l.base, trackSet(clipOf(doc, clip), l.id), t);
export const cam25At = (doc, clip, t) => evalTracks(doc.cam25, trackSet(clipOf(doc, clip), CAM25), t);

/** cambia una propiedad: en Base el valor fijo; en un clip, la clave en el cursor */
export function setProp(doc, clip, l, prop, v, t) {
  const fixed = ['ax', 'ay', 'order'].includes(prop);
  if (clip === 'base' || fixed) { (l.id === CAM25 ? doc.cam25 : l.base)[prop] = v; return; }
  setKey(doc.clips[clip], l.id, prop, t, v);
}
export const removeKey = (doc, clip, id, prop, t) => rmKey(doc.clips[clip], id, prop, t);

/** camara 3D activa en t (cortes del clip; si no hay, la elegida en la pantalla) */
export function activeCamAt(doc, clip, t) {
  const cuts = clipOf(doc, clip)?.cuts || [];
  let id = doc.activeCam;
  for (const c of [...cuts].sort((a, b) => a.t - b.t)) if (c.t <= t + 1e-6) id = c.cam;
  return doc.layers.find(l => l.id === id && l.type === 'camera') || doc.layers.find(l => l.type === 'camera') || null;
}

// ------------------------------------------------------------------ jerarquia
export const childrenOf = (doc, id) => doc.layers.filter(l => (l.parent || null) === (id || null));
export function ancestors(doc, l) {
  const out = [];
  for (let cur = l; cur.parent;) { const par = doc.layers.find(q => q.id === cur.parent); if (!par || out.includes(par)) break; out.push(par); cur = par; }
  return out;
}
export function descendants(doc, id) {
  const out = new Set([id]);
  for (let grow = true; grow;) { grow = false; for (const l of doc.layers) if (l.parent && out.has(l.parent) && !out.has(l.id)) { out.add(l.id); grow = true; } }
  return out;
}

/** 2D y 2.5D: posicion en el mundo (suma de los padres) y profundidad acumulada */
export function worldOf(doc, l, clip, t) {
  const s = stateAt(doc, l, clip, t);
  let x = s.x, y = s.y, depth = s.depth || 0, alpha = s.alpha;
  for (const a of ancestors(doc, l)) { const q = stateAt(doc, a, clip, t); x += q.x; y += q.y; depth += q.depth || 0; alpha *= q.alpha; }
  return { s, x, y, depth, alpha };
}

/** 2.5D: mundo -> pantalla con la camara (x, y, zoom, giro). null = detras de la camara */
export function project25(cam, x, y, depth) {
  const d = PERSP + depth;
  if (d <= 1) return null;
  const k = cam.zoom * PERSP / d;
  const dx = (x - cam.x) * k, dy = (y - cam.y) * k;
  const r = -cam.rot * Math.PI / 180, c = Math.cos(r), s = Math.sin(r);
  return { x: W / 2 + dx * c - dy * s, y: H / 2 + dx * s + dy * c, k };
}

// ------------------------------------------------------------------ botones y navegacion (igual que modes.py)
export const focusables = doc => doc.layers.filter(l => l.ui?.focus && l.visible !== false && l.type !== 'camera')
  .sort((a, b) => (a.ui.order || 0) - (b.ui.order || 0) || (a.base.y - b.base.y) || (a.base.x - b.base.x));

/** boton al que pertenece una capa: ella misma o el grupo-boton mas cercano que la contiene */
export function buttonOf(doc, l) {
  if (l.ui?.focus) return l;
  return ancestors(doc, l).find(a => a.ui?.focus && a.visible !== false) || null;
}
/** centro de un boton en el mundo 2D (grupos: caja de sus capas visuales). Igual que modes.py button_center */
export function buttonCenter(doc, l) {
  if (l.type !== 'group') {
    const w = worldOf(doc, l, 'base', 0), k = l.base.scale ?? 1;
    return [w.x + (0.5 - l.base.ax) * l.base.w * k, w.y + (0.5 - l.base.ay) * l.base.h * k];
  }
  const kids = [...descendants(doc, l.id)].map(id => doc.layers.find(q => q.id === id))
    .filter(q => q && q.type !== 'group' && q.type !== 'camera' && q.visible !== false);
  if (!kids.length) { const w = worldOf(doc, l, 'base', 0); return [w.x, w.y]; }
  let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
  for (const q of kids) {
    const [cx, cy] = buttonCenter(doc, q), hw = q.base.w * (q.base.scale ?? 1) / 2, hh = q.base.h * (q.base.scale ?? 1) / 2;
    x0 = Math.min(x0, cx - hw); y0 = Math.min(y0, cy - hh); x1 = Math.max(x1, cx + hw); y1 = Math.max(y1, cy + hh);
  }
  return [(x0 + x1) / 2, (y0 + y1) / 2];
}

/** vecino de cada boton en cada direccion (indices en focusables); arriba/abajo dan la vuelta como un menu */
export function navTable(doc) {
  const F = focusables(doc);
  const pos = F.map(l => buttonCenter(doc, l));
  const pick = (i, dx, dy) => {
    let best = -1, bs = Infinity;
    pos.forEach((p, j) => {
      if (j === i) return;
      const vx = p[0] - pos[i][0], vy = p[1] - pos[i][1];
      const along = vx * dx + vy * dy, across = Math.abs(vx * dy - vy * dx);
      if (along <= 1) return;
      const sc = along + across * 2;
      if (sc < bs) { bs = sc; best = j; }
    });
    return best;
  };
  return F.map((l, i) => {
    let up = pick(i, 0, -1), down = pick(i, 0, 1);
    if (up < 0 && F.length > 1) up = (i - 1 + F.length) % F.length;
    if (down < 0 && F.length > 1) down = (i + 1) % F.length;
    return { id: l.id, up, down, left: pick(i, -1, 0), right: pick(i, 1, 0) };
  });
}
