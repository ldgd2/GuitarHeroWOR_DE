// Editor de animacion de camara (keyframes), AISLADO del resto del editor:
//   - trabaja sobre una COPIA de la toma (KF.doc); el proyecto no se toca hasta "Aplicar". "Cancelar" lo deja como estaba.
//   - tiene su propio deshacer/rehacer, su propio portapapeles y sus propios atajos de teclado (solo mientras esta abierto).
//   - el monitor muestra la animacion de la copia; la vista 3D muestra la trayectoria y las claves.
//   - la camara del movil, si graba con el editor abierto, graba en la copia.
// La interfaz esta en KeyframeEditor.jsx; el formato de las claves y la interpolacion en camera/shots.js (keyPose).
import * as THREE from 'three';
import { $, changed, emit, pushUndo, S } from '../core/state.js';
import { keyPose, worldPose } from '../camera/shots.js';
import { editCam, editorOnly, onFrame, progCam, mon, scene, view } from '../viewport/scene.js';
import { orbit } from '../viewport/controls.js';
import { seek, setPlaying } from '../ui/playback.js';
import { VCAM, VCAM_HOOKS } from '../vcam/vcam.js';

const D2R = THREE.MathUtils.degToRad, R2D = THREE.MathUtils.radToDeg;
export const FRAME_MS = 1000 / 30;

export const KF = {
  on: false, shot: null, doc: null, orig: '',
  sel: new Set(), undo: [], redo: [], clip: null,
  v0: 0, v1: 1000,                 // ventana visible de la linea de tiempo (ms locales de la toma)
  mode: 'tracks',                  // 'tracks' | 'curves'
  chans: ['px', 'py', 'pz'],       // canales del editor de curvas
  snap: true, loop: true, pilot: false,
};

// ---- bus propio ----
const subs = new Set();
export const kfSub = fn => { subs.add(fn); return () => subs.delete(fn); };
export function kfEmit() { rebuildPath(); subs.forEach(f => f()); }

// ---- tiempo ----
export const kfTime = () => (KF.doc ? Math.min(KF.doc.dur, Math.max(0, S.t - KF.doc.t)) : 0);
export const kfSeek = rt => { if (KF.doc) seek(KF.doc.t + Math.min(KF.doc.dur, Math.max(0, rt))); };
export function snapT(rt, free = false) {
  const d = KF.doc;
  rt = Math.min(d.dur, Math.max(0, rt));
  if (free) return Math.round(rt);
  if (KF.snap && S.proj?.fretbars?.length) {   // a los pulsos de la cancion, si estan cerca (8 px)
    const px = 8 * (KF.v1 - KF.v0) / Math.max(1, kfWidth());
    let best = null;
    for (const b of S.proj.fretbars) {
      const r = b - d.t; if (r < -px) continue; if (r > d.dur + px) break;
      if (Math.abs(r - rt) <= px && (best == null || Math.abs(r - rt) < Math.abs(best - rt))) best = r;
    }
    if (best != null) return Math.min(d.dur, Math.max(0, Math.round(best)));
  }
  return Math.round(Math.round(rt / FRAME_MS) * FRAME_MS);   // a fotogramas (30 por segundo)
}
let _w = 800;
export const kfWidth = () => _w;
export const setKfWidth = w => { _w = w; };

// ---- abrir / cerrar ----
function seedKeys(s) {
  if (s.preset === 'keys' && s.keys?.length) return JSON.parse(JSON.stringify(s.keys));
  return [0, 0.5, 1].map(f => {   // la toma tenia un movimiento predefinido: se muestrea en 3 claves
    const t = Math.round(s.dur * f), W = worldPose(s, s.t + t);
    return { t, p: W.p.toArray(), q: W.q.toArray(), fov: W.fov };
  });
}
export function kfOpen(s) {
  if (KF.on) kfClose(false);
  KF.shot = s;
  KF.doc = { id: s.id, t: s.t, dur: s.dur, role: s.role, preset: 'keys', keys: seedKeys(s), interp: s.interp || 'smooth', fps: s.fps || 30 };
  KF.orig = JSON.stringify(KF.doc);
  KF.sel = new Set(); KF.undo = []; KF.redo = []; KF.pilot = false;
  KF.v0 = 0; KF.v1 = s.dur;
  KF.on = true;
  document.body.classList.add('kfMode');
  VCAM_HOOKS.target = () => KF.doc;
  VCAM_HOOKS.before = () => kfPush();
  VCAM_HOOKS.after = () => { KF.sel = new Set(); kfEmit(); };
  if (S.t < s.t || S.t > s.t + s.dur) seek(s.t);
  emit('kf'); kfEmit();
  dispatchEvent(new Event('resize'));
}
export const kfDirty = () => KF.on && JSON.stringify(KF.doc) !== KF.orig;
export function kfClose(apply) {
  if (!KF.on) return;
  if (apply && kfDirty()) {
    pushUndo();   // un solo paso en el deshacer del proyecto
    const s = KF.shot, d = KF.doc;
    s.preset = 'keys'; s.keys = d.keys; s.interp = d.interp; s.fps = d.fps;
  }
  KF.on = false; KF.pilot = false; KF.shot = null; KF.doc = null;
  VCAM_HOOKS.target = VCAM_HOOKS.before = VCAM_HOOKS.after = null;
  document.body.classList.remove('kfMode');
  setPlaying(false);
  rebuildPath();
  if (apply) changed(); else emit('inspector');
  emit('kf');
  dispatchEvent(new Event('resize'));
}

// ---- deshacer propio ----
export function kfPush() {
  KF.undo.push(JSON.stringify(KF.doc)); if (KF.undo.length > 200) KF.undo.shift();
  KF.redo = [];
}
export function kfEdit(fn, push = true) { if (push) kfPush(); fn(KF.doc); sortKeys(); kfEmit(); }
export function kfUndo() { if (!KF.undo.length) return; KF.redo.push(JSON.stringify(KF.doc)); KF.doc = JSON.parse(KF.undo.pop()); KF.sel = new Set(); kfEmit(); }
export function kfRedo() { if (!KF.redo.length) return; KF.undo.push(JSON.stringify(KF.doc)); KF.doc = JSON.parse(KF.redo.pop()); KF.sel = new Set(); kfEmit(); }

// mantiene las claves ordenadas sin perder la seleccion (por identidad) ni dejar dos en el mismo ms
function sortKeys() {
  const K = KF.doc.keys, selObjs = [...KF.sel].map(i => K[i]).filter(Boolean);
  K.sort((a, b) => a.t - b.t);
  for (let i = 1; i < K.length; i++) if (K[i].t <= K[i - 1].t) K[i].t = K[i - 1].t + 1;
  KF.sel = new Set(selObjs.map(k => K.indexOf(k)).filter(i => i >= 0));
}

// ---- poses ----
export const poseAt = rt => keyPose(KF.doc, KF.doc.t + rt);
export function viewPose() {   // pose de la vista 3D (FOV horizontal como lo usa la toma)
  const asp = view.clientWidth / Math.max(1, view.clientHeight);
  const h = R2D(2 * Math.atan(Math.tan(D2R(editCam.fov) / 2) * asp));
  return { p: editCam.position.toArray(), q: editCam.quaternion.toArray(), fov: Math.min(150, Math.max(5, +h.toFixed(2))) };
}
const r4 = a => a.map(v => +v.toFixed(5));
// clave en el cursor: desde la vista 3D (fromView) o con el valor animado actual
export function addKey(fromView) {
  const rt = snapT(kfTime(), true), K = KF.doc.keys;
  const P = fromView ? viewPose() : (W => ({ p: W.p.toArray(), q: W.q.toArray(), fov: +W.fov.toFixed(2) }))(poseAt(rt));
  const i = K.findIndex(k => Math.abs(k.t - rt) < FRAME_MS / 2);
  kfEdit(() => {
    const k = i >= 0 ? Object.assign(K[i], { p: r4(P.p), q: r4(P.q), fov: P.fov }) : { t: rt, p: r4(P.p), q: r4(P.q), fov: P.fov };
    if (i < 0) K.push(k);
    sortKeys(); KF.sel = new Set([K.indexOf(k)]);
  });
}
export function delKeys() {
  if (!KF.sel.size || KF.doc.keys.length - KF.sel.size < 1) return;
  kfEdit(d => { d.keys = d.keys.filter((_, i) => !KF.sel.has(i)); KF.sel = new Set(); });
}
export function copyKeys() {
  const ks = [...KF.sel].sort((a, b) => a - b).map(i => KF.doc.keys[i]);
  if (!ks.length) return;
  const t0 = ks[0].t; KF.clip = ks.map(k => ({ ...JSON.parse(JSON.stringify(k)), t: k.t - t0 }));
}
export function pasteKeys(at = kfTime()) {
  if (!KF.clip?.length) return;
  kfEdit(d => {
    const added = KF.clip.map(k => ({ ...JSON.parse(JSON.stringify(k)), t: Math.min(d.dur, Math.round(at + k.t)) }));
    for (const k of added) { d.keys = d.keys.filter(x => Math.abs(x.t - k.t) >= 1); d.keys.push(k); }
    sortKeys(); KF.sel = new Set(added.map(k => d.keys.indexOf(k)));
  });
}
export const selectAll = () => { KF.sel = new Set(KF.doc.keys.map((_, i) => i)); kfEmit(); };
export function goKey(dir) {
  const rt = kfTime(), K = KF.doc.keys;
  const i = dir > 0 ? K.findIndex(k => k.t > rt + 1) : K.map(k => k.t < rt - 1).lastIndexOf(true);
  if (i >= 0) { KF.sel = new Set([i]); kfSeek(K[i].t); kfEmit(); }
}

// ---- canales (para el inspector y el editor de curvas) ----
const _e = new THREE.Euler(), _q = new THREE.Quaternion();
export const CHANNELS = {
  px: { label: 'Pos X', unit: 'm', color: '#ff6b6b' }, py: { label: 'Pos Y', unit: 'm', color: '#5fd38d' }, pz: { label: 'Pos Z', unit: 'm', color: '#5aa9ff' },
  pan: { label: 'Panoramica', unit: '°', color: '#ffb44d' }, tilt: { label: 'Inclinacion', unit: '°', color: '#c77dff' }, roll: { label: 'Giro', unit: '°', color: '#ff7bd5' },
  fov: { label: 'FOV', unit: '°', color: '#e8eaf0' },
};
export function chanOf(k, c) {
  if (c === 'px') return k.p[0]; if (c === 'py') return k.p[1]; if (c === 'pz') return k.p[2];
  if (c === 'fov') return k.fov;
  _e.setFromQuaternion(_q.set(...k.q), 'YXZ');
  return R2D(c === 'pan' ? _e.y : c === 'tilt' ? _e.x : _e.z);
}
export function setChan(k, c, v) {
  if (c === 'px') k.p[0] = v; else if (c === 'py') k.p[1] = v; else if (c === 'pz') k.p[2] = v;
  else if (c === 'fov') k.fov = Math.min(150, Math.max(5, v));
  else {
    _e.setFromQuaternion(_q.set(...k.q), 'YXZ');
    if (c === 'pan') _e.y = D2R(v); else if (c === 'tilt') _e.x = D2R(Math.max(-89.9, Math.min(89.9, v))); else _e.z = D2R(v);
    k.q = r4(_q.setFromEuler(_e).toArray());
  }
}
export const poseChan = (W, c) => chanOf({ p: W.p.toArray(), q: W.q.toArray(), fov: W.fov }, c);

// ---- vista 3D: llevar la vista a la clave / pilotar la clave con la vista ----
export function viewToKey(k) {
  const q = new THREE.Quaternion(...k.q), p = new THREE.Vector3(...k.p);
  editCam.position.copy(p);
  orbit.target.copy(p.clone().add(new THREE.Vector3(0, 0, -3).applyQuaternion(q)));
  orbit.update();
}
let pilotLast = '';
export function setPilot(on) {
  const one = KF.sel.size === 1 ? KF.doc.keys[[...KF.sel][0]] : null;
  if (on && !one) return;
  KF.pilot = on;
  if (on) { kfPush(); viewToKey(one); pilotLast = ''; }
  kfEmit();
}

// ---- trayectoria y claves en la vista 3D (solo en la vista de edicion, nunca en el monitor) ----
const pathGroup = new THREE.Group(); scene.add(pathGroup); editorOnly.add(pathGroup);
const lineMat = new THREE.LineBasicMaterial({ color: 0xffd34d, transparent: true, opacity: 0.85, depthTest: false });
const coneGeo = new THREE.ConeGeometry(0.12, 0.32, 4); coneGeo.rotateX(-Math.PI / 2); coneGeo.translate(0, 0, -0.16);
const keyMat = new THREE.MeshBasicMaterial({ color: 0xffd34d, depthTest: false }), selMat = new THREE.MeshBasicMaterial({ color: 0xff5ad1, depthTest: false });
function rebuildPath() {
  for (const c of [...pathGroup.children]) { pathGroup.remove(c); c.geometry?.dispose?.(); }
  if (!KF.on || !KF.doc) return;
  const d = KF.doc, n = Math.max(2, Math.min(600, Math.round(d.dur / FRAME_MS))), pts = [];
  for (let i = 0; i <= n; i++) pts.push(poseAt(d.dur * i / n).p);
  const line = new THREE.Line(new THREE.BufferGeometry().setFromPoints(pts), lineMat); line.renderOrder = 999;
  pathGroup.add(line);
  d.keys.forEach((k, i) => {
    const m = new THREE.Mesh(coneGeo, KF.sel.has(i) ? selMat : keyMat);
    m.position.set(...k.p); m.quaternion.set(...k.q); m.renderOrder = 1000;
    pathGroup.add(m);
  });
}

// ---- cada fotograma: monitor con la copia, bucle, pilotaje ----
onFrame(() => {
  if (!KF.on || !KF.doc) return;
  const d = KF.doc;
  if (S.playing && S.t >= d.t + d.dur) { if (KF.loop) seek(d.t); else setPlaying(false); }
  if (KF.pilot) {   // la clave elegida sigue a la vista 3D (el giro sobre el eje se conserva)
    const k = d.keys[[...KF.sel][0]];
    if (!k) KF.pilot = false;
    else {
      const P = viewPose(), sig = P.p.map(v => v.toFixed(4)).join() + P.q.map(v => v.toFixed(4)).join();
      if (sig !== pilotLast) {
        pilotLast = sig;
        const roll = chanOf(k, 'roll');
        k.p = r4(P.p); k.q = r4(P.q); setChan(k, 'roll', roll);
        rebuildPath(); subs.forEach(f => f());
      }
    }
  }
  if (VCAM.live || VCAM.rec) return;   // el movil manda en el monitor
  const W = poseAt(kfTime()), asp = mon.clientWidth / Math.max(1, mon.clientHeight);
  progCam.position.copy(W.p); progCam.quaternion.copy(W.q);
  progCam.fov = R2D(2 * Math.atan(Math.tan(D2R(W.fov) / 2) / asp));
  const info = $('monInfo'); if (info) info.innerHTML = `Animacion de la toma ${d.id} &middot; ${(kfTime() / 1000).toFixed(2)} s &middot; FOV ${W.fov.toFixed(0)}&deg;`;
});

// ---- atajos (solo con el editor abierto; tienen prioridad sobre los del resto del editor) ----
addEventListener('keydown', e => {
  if (!KF.on || ['INPUT', 'SELECT', 'TEXTAREA'].includes(document.activeElement?.tagName) || S.modal) return;
  const k = e.key, c = e.ctrlKey || e.metaKey;
  const done = () => { e.preventDefault(); e.stopImmediatePropagation(); };
  if (k === ' ') { setPlaying(!S.playing); done(); }
  else if (c && k.toLowerCase() === 'z') { e.shiftKey ? kfRedo() : kfUndo(); done(); }
  else if (c && k.toLowerCase() === 'y') { kfRedo(); done(); }
  else if (c && k.toLowerCase() === 'c') { copyKeys(); done(); }
  else if (c && k.toLowerCase() === 'v') { pasteKeys(); done(); }
  else if (c && k.toLowerCase() === 'd') { copyKeys(); pasteKeys(kfTime() + FRAME_MS * 10); done(); }
  else if (c && k.toLowerCase() === 'a') { selectAll(); done(); }
  else if (k === 'Delete' || k === 'Backspace') { delKeys(); done(); }
  else if (!c && k.toLowerCase() === 'k') { addKey(!e.shiftKey); done(); }
  else if (!c && k.toLowerCase() === 'p') { setPilot(!KF.pilot); done(); }
  else if (k === 'ArrowLeft' || k === 'ArrowRight') {
    const dir = k === 'ArrowLeft' ? -1 : 1;
    if (e.shiftKey) goKey(dir); else kfSeek(kfTime() + dir * FRAME_MS);
    done();
  } else if (k === 'Home') { kfSeek(0); done(); }
  else if (k === 'End') { kfSeek(KF.doc.dur); done(); }
  else if (k === 'Escape') { if (KF.pilot) setPilot(false); else emit('kfAskClose'); done(); }
  else if (k === 'Enter' && c) { kfClose(true); done(); }
}, true);

// ---- simplificar (grabaciones del movil: cientos de claves -> las imprescindibles) ----
// Ramer-Douglas-Peucker sobre la pose: una clave se quita si la interpolacion lineal entre sus vecinas conservadas
// la reproduce dentro de la tolerancia (posicion en metros, giro y FOV en grados).
export function simplifyKeys(tolPos = 0.01, tolDeg = 0.4) {
  const K = KF.doc.keys;
  if (K.length < 3) return 0;
  const qa = new THREE.Quaternion(), qb = new THREE.Quaternion(), qi = new THREE.Quaternion(), qk = new THREE.Quaternion();
  const err = (a, b, k) => {
    const f = (k.t - a.t) / Math.max(1, b.t - a.t);
    let dp = 0; for (let j = 0; j < 3; j++) { const v = a.p[j] + (b.p[j] - a.p[j]) * f - k.p[j]; dp += v * v; }
    qi.copy(qa.set(...a.q)).slerp(qb.set(...b.q), f);
    const ang = R2D(qi.angleTo(qk.set(...k.q))), df = Math.abs(a.fov + (b.fov - a.fov) * f - k.fov);
    return Math.max(Math.sqrt(dp) / tolPos, ang / tolDeg, df / tolDeg);
  };
  const keep = new Uint8Array(K.length); keep[0] = keep[K.length - 1] = 1;
  const stack = [[0, K.length - 1]];
  while (stack.length) {
    const [i, j] = stack.pop(); let worst = 1, at = -1;
    for (let k = i + 1; k < j; k++) { const e = err(K[i], K[j], K[k]); if (e > worst) { worst = e; at = k; } }
    if (at >= 0) { keep[at] = 1; stack.push([i, at], [at, j]); }
  }
  const before = K.length;
  kfEdit(d => {
    const lin = d.interp === 'linear';
    d.keys = K.filter((_, i) => keep[i]);
    if (!lin) for (const k of d.keys) k.ease ||= 'linear';   // la tolerancia se calcula con lineal: se respeta
    KF.sel = new Set();
  });
  return before - KF.doc.keys.length;
}
