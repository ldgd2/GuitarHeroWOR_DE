// Editor de animacion por keyframes, AISLADO del resto del editor. Dos tipos de documento:
//   camera  animacion de una toma de camara (claves de posicion, giro y FOV)
//   pose    retoque de pose de un musico (claves de giro por hueso; rig/poseLayer.js)
// Reglas comunes:
//   - trabaja sobre una COPIA (KF.doc); el proyecto no se toca hasta "Aplicar" (o al guardar/compilar: kfCommit).
//   - tiene su propio deshacer/rehacer, su propio portapapeles y sus propios atajos de teclado.
//   - la vista 3D muestra la trayectoria y las claves y se pueden manipular con el gizmo (keyframes/gizmo3d.js).
// La interfaz esta en KeyframeEditor.jsx; la interpolacion de camara en camera/shots.js (keyPose) y las
// transiciones en keyframes/easing.js.
import * as THREE from 'three';
import { $, changed, emit, pushUndo, S } from '../core/state.js';
import { keyPose, worldPose } from '../camera/shots.js';
import { editCam, editorOnly, onFrame, progCam, mon, scene, view } from '../viewport/scene.js';
import { orbit } from '../viewport/controls.js';
import { seek, setPlaying } from '../ui/playback.js';
import { VCAM, VCAM_HOOKS } from '../vcam/vcam.js';
import { layerAt, MOVABLE, POSE_EDIT } from '../rig/poseLayer.js';

const D2R = THREE.MathUtils.degToRad, R2D = THREE.MathUtils.radToDeg;
export const FRAME_MS = 1000 / 30;

export const KF = {
  on: false, kind: 'camera', target: null, doc: null, orig: '',
  sel: new Set(), undo: [], redo: [], clip: null,
  v0: 0, v1: 1000,                 // ventana visible de la linea de tiempo (ms locales)
  mode: 'tracks',                  // 'tracks' | 'curves'
  chans: ['px', 'py', 'pz'],       // canales del editor de curvas
  snap: true, loop: true, pilot: false,
  autokey: true,                   // mover el gizmo sin clave en el cursor crea una
  tool: 'move', space: 'world',    // gizmo: mover | rotar, en ejes del mundo | propios
  bone: null,                      // pose: hueso elegido
};
export const isPose = () => KF.kind === 'pose';

// ---- bus propio ----
const subs = new Set();
export const kfSub = fn => { subs.add(fn); return () => subs.delete(fn); };
export function kfEmit() { if (isPose()) POSE_EDIT.doc = KF.doc; rebuildPath(); subs.forEach(f => f()); }
export const kfNotify = () => subs.forEach(f => f());   // sin rehacer la trayectoria (arrastres)

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
function begin(kind, target, doc) {
  if (KF.on) kfClose(false);
  KF.kind = kind; KF.target = target; KF.doc = doc;
  KF.orig = JSON.stringify(doc);
  KF.sel = new Set(); KF.undo = []; KF.redo = []; KF.pilot = false; KF.bone = null;
  KF.v0 = 0; KF.v1 = doc.dur; KF.on = true;
  KF.chans = kind === 'pose' ? ['rx', 'ry', 'rz'] : ['px', 'py', 'pz'];
  KF.tool = kind === 'pose' ? 'rotate' : 'move';
  document.body.classList.add('kfMode');
  if (S.t < doc.t || S.t > doc.t + doc.dur) seek(doc.t);
  emit('kf'); kfEmit();
  dispatchEvent(new Event('resize'));
}
export function kfOpen(s) {
  begin('camera', s, { id: s.id, t: s.t, dur: s.dur, role: s.role, preset: 'keys', keys: seedKeys(s), interp: s.interp || 'smooth', fps: s.fps || 30 });
  VCAM_HOOKS.target = () => KF.doc;
  VCAM_HOOKS.before = () => kfPush();
  VCAM_HOOKS.after = () => { KF.sel = new Set(); kfEmit(); };
}
export function kfOpenPose(L) {
  const doc = { id: L.id, t: L.t, dur: L.dur, role: L.role, name: L.name, fade: L.fade ?? 200, interp: L.interp || 'sineInOut',
    keys: JSON.parse(JSON.stringify(L.keys || [])) };
  POSE_EDIT.id = L.id; POSE_EDIT.doc = doc;
  begin('pose', L, doc);
}
export const kfDirty = () => KF.on && JSON.stringify(KF.doc) !== KF.orig;
// copia lo editado al proyecto sin cerrar (guardar y compilar lo llaman: lo que ves es lo que se exporta)
// La toma o el retoque se busca por id en el proyecto ACTUAL: deshacer/rehacer del proyecto lo sustituye por una
// copia y escribir en la referencia antigua perderia la animacion (no llegaria a guardarse ni a exportarse).
function liveTarget() {
  const P = S.proj, id = KF.target.id;
  if (!P) return KF.target;
  const list = KF.kind === 'camera' ? (P.shots ||= []) : (P.poseLayers ||= []);
  let s = list.find(x => x.id === id);
  if (!s) { s = JSON.parse(JSON.stringify(KF.target)); list.push(s); }   // borrada mientras se editaba: se recupera
  KF.target = s;
  return s;
}
function writeBack() {
  const s = liveTarget(), d = JSON.parse(JSON.stringify(KF.doc));
  if (KF.kind === 'camera') { s.preset = 'keys'; s.keys = d.keys; s.interp = d.interp; s.fps = d.fps; }
  else { s.keys = d.keys; s.interp = d.interp; s.fade = d.fade; s.name = d.name; }
}
export function kfCommit() {
  if (!KF.on || !kfDirty()) return false;
  pushUndo(); writeBack(); KF.orig = JSON.stringify(KF.doc);
  S.dirty = true; S.ver++;
  return true;
}
export function kfClose(apply) {
  if (!KF.on) return;
  if (apply && kfDirty()) { pushUndo(); writeBack(); }   // un solo paso en el deshacer del proyecto
  KF.on = false; KF.pilot = false; KF.target = null; KF.doc = null; KF.bone = null;
  POSE_EDIT.id = null; POSE_EDIT.doc = null;
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
export function sortKeys() {
  const K = KF.doc.keys, selObjs = [...KF.sel].map(i => K[i]).filter(Boolean);
  K.sort((a, b) => a.t - b.t);
  for (let i = 1; i < K.length; i++) if (K[i].t <= K[i - 1].t) K[i].t = K[i - 1].t + 1;
  KF.sel = new Set(selObjs.map(k => K.indexOf(k)).filter(i => i >= 0));
}
export const selKeys = () => [...KF.sel].sort((a, b) => a - b).map(i => KF.doc.keys[i]).filter(Boolean);

// ---- poses de camara ----
export const poseAt = rt => keyPose(KF.doc, KF.doc.t + rt);
export function viewPose() {   // pose de la vista 3D (FOV horizontal como lo usa la toma)
  const asp = view.clientWidth / Math.max(1, view.clientHeight);
  const h = R2D(2 * Math.atan(Math.tan(D2R(editCam.fov) / 2) * asp));
  return { p: editCam.position.toArray(), q: editCam.quaternion.toArray(), fov: Math.min(150, Math.max(5, +h.toFixed(2))) };
}
export const r4 = a => a.map(v => +v.toFixed(5));

// clave de pose: lo que vale la capa en rt (todos sus huesos)
export function poseKeyAt(rt) {
  const k = { t: rt, b: {}, bp: {} };
  for (const [i, v] of layerAt(KF.doc, rt)) { k.b[i] = r4(v.q.toArray()); if (v.p) k.bp[i] = r4(v.p.toArray()); }
  if (!Object.keys(k.bp).length) delete k.bp;
  return k;
}
// clave en el cursor que existe (o -1)
export const keyIndexAt = rt => KF.doc.keys.findIndex(k => Math.abs(k.t - rt) < FRAME_MS / 2);
/** devuelve la clave en el cursor; si no hay, la crea con el valor actual (sin paso de deshacer propio) */
export function ensureKeyAtCursor() {
  const rt = snapT(kfTime(), true), K = KF.doc.keys;
  let i = keyIndexAt(rt);
  if (i < 0) {
    const k = isPose() ? poseKeyAt(rt) : (W => ({ t: rt, p: r4(W.p.toArray()), q: r4(W.q.toArray()), fov: +W.fov.toFixed(2) }))(poseAt(rt));
    K.push(k); sortKeys(); i = K.indexOf(k);
  }
  return K[i];
}
// clave en el cursor: desde la vista 3D (fromView, solo camara) o con el valor animado actual
export function addKey(fromView) {
  kfEdit(() => {
    const k = ensureKeyAtCursor();
    if (fromView && !isPose()) { const P = viewPose(); k.p = r4(P.p); k.q = r4(P.q); k.fov = P.fov; }
    sortKeys(); KF.sel = new Set([KF.doc.keys.indexOf(k)]);
  });
}
export function delKeys() {
  if (!KF.sel.size || (!isPose() && KF.doc.keys.length - KF.sel.size < 1)) return;
  kfEdit(d => { d.keys = d.keys.filter((_, i) => !KF.sel.has(i)); KF.sel = new Set(); });
}
export function copyKeys() {
  const ks = selKeys();
  if (!ks.length) return;
  const t0 = ks[0].t; KF.clip = { kind: KF.kind, keys: ks.map(k => ({ ...JSON.parse(JSON.stringify(k)), t: k.t - t0 })) };
}
export function pasteKeys(at = kfTime()) {
  if (!KF.clip?.keys?.length || KF.clip.kind !== KF.kind) return;
  kfEdit(d => {
    const added = KF.clip.keys.map(k => ({ ...JSON.parse(JSON.stringify(k)), t: Math.min(d.dur, Math.round(at + k.t)) }));
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

// ---- operaciones sobre varias claves ----
// mover en el tiempo (ms), escalar el tiempo alrededor del cursor, invertir, repartir por igual
export function nudgeKeys(dms) {
  if (!KF.sel.size) return;
  kfEdit(d => { for (const k of selKeys()) k.t = Math.min(d.dur, Math.max(0, Math.round(k.t + dms))); });
}
export function scaleKeys(f, pivot = kfTime()) {
  if (KF.sel.size < 2 || !(f > 0)) return;
  kfEdit(d => { for (const k of selKeys()) k.t = Math.min(d.dur, Math.max(0, Math.round(pivot + (k.t - pivot) * f))); });
}
export function reverseKeys() {
  const ks = selKeys(); if (ks.length < 2) return;
  const a = ks[0].t, b = ks.at(-1).t;
  kfEdit(() => { for (const k of ks) k.t = Math.round(a + b - k.t); });
}
export function distributeKeys() {
  const ks = selKeys(); if (ks.length < 3) return;
  const a = ks[0].t, b = ks.at(-1).t;
  kfEdit(() => { ks.forEach((k, i) => { k.t = Math.round(a + (b - a) * i / (ks.length - 1)); }); });
}
export function setEase(ease, extra = {}) {
  kfEdit(() => { for (const k of selKeys()) { k.ease = ease; Object.assign(k, extra); if (ease !== 'bezier') delete k.bez; if (ease !== 'steps') delete k.steps; } });
}

// ---- canales (inspector y editor de curvas) ----
// camara: px py pz (m), pan tilt roll (grados, YXZ), fov. pose (hueso elegido): rx ry rz (grados, XYZ local), tx ty tz (cm)
const _e = new THREE.Euler(), _q = new THREE.Quaternion();
export const CHANNELS = {
  px: { label: 'Pos X', unit: 'm', color: '#e06c6c' }, py: { label: 'Pos Y', unit: 'm', color: '#5fbf86' }, pz: { label: 'Pos Z', unit: 'm', color: '#5a9fe0' },
  pan: { label: 'Panoramica', unit: '°', color: '#e3a043' }, tilt: { label: 'Inclinacion', unit: '°', color: '#b48ae0' }, roll: { label: 'Giro', unit: '°', color: '#d9739f' },
  fov: { label: 'FOV', unit: '°', color: '#d7dbe2' },
  rx: { label: 'Giro X', unit: '°', color: '#e06c6c' }, ry: { label: 'Giro Y', unit: '°', color: '#5fbf86' }, rz: { label: 'Giro Z', unit: '°', color: '#5a9fe0' },
  tx: { label: 'Mover X', unit: 'cm', color: '#e06c6c' }, ty: { label: 'Mover Y', unit: 'cm', color: '#5fbf86' }, tz: { label: 'Mover Z', unit: 'cm', color: '#5a9fe0' },
};
export const GROUPS = { pos: ['px', 'py', 'pz'], rot: ['pan', 'tilt', 'roll'], fov: ['fov'], brot: ['rx', 'ry', 'rz'], bpos: ['tx', 'ty', 'tz'] };
const POSE_CH = { rx: 'x', ry: 'y', rz: 'z', tx: 0, ty: 1, tz: 2 };
export function chanOf(k, c) {
  if (c in POSE_CH) {
    const i = KF.bone; if (i == null) return 0;
    if (c[0] === 't') return (k.bp?.[i]?.[POSE_CH[c]] || 0) * 100;
    _e.setFromQuaternion(_q.fromArray(k.b?.[i] || [0, 0, 0, 1]), 'XYZ');
    return R2D(_e[POSE_CH[c]]);
  }
  if (c === 'px') return k.p[0]; if (c === 'py') return k.p[1]; if (c === 'pz') return k.p[2];
  if (c === 'fov') return k.fov;
  _e.setFromQuaternion(_q.set(...k.q), 'YXZ');
  return R2D(c === 'pan' ? _e.y : c === 'tilt' ? _e.x : _e.z);
}
export function setChan(k, c, v) {
  if (c in POSE_CH) {
    const i = KF.bone; if (i == null) return;
    if (c[0] === 't') { if (!MOVABLE.has(i)) return; const p = ((k.bp ||= {})[i] ||= [0, 0, 0]); p[POSE_CH[c]] = +(v / 100).toFixed(5); return; }
    _e.setFromQuaternion(_q.fromArray(k.b?.[i] || [0, 0, 0, 1]), 'XYZ');
    _e[POSE_CH[c]] = D2R(v);
    (k.b ||= {})[i] = r4(_q.setFromEuler(_e).toArray());
    return;
  }
  if (c === 'px') k.p[0] = v; else if (c === 'py') k.p[1] = v; else if (c === 'pz') k.p[2] = v;
  else if (c === 'fov') k.fov = Math.min(150, Math.max(5, v));
  else {
    _e.setFromQuaternion(_q.set(...k.q), 'YXZ');
    if (c === 'pan') _e.y = D2R(v); else if (c === 'tilt') _e.x = D2R(Math.max(-89.9, Math.min(89.9, v))); else _e.z = D2R(v);
    k.q = r4(_q.setFromEuler(_e).toArray());
  }
}
// valor animado de un canal en rt (curvas y miniaturas)
export function chanAt(rt, c) {
  if (c in POSE_CH) {
    const v = KF.bone == null ? null : layerAt(KF.doc, rt).get(KF.bone);
    return chanOf({ b: v ? { [KF.bone]: v.q.toArray() } : {}, bp: v?.p ? { [KF.bone]: v.p.toArray() } : {} }, c);
  }
  const W = poseAt(rt);
  return chanOf({ p: W.p.toArray(), q: W.q.toArray(), fov: W.fov }, c);
}

// ---- vista 3D: llevar la vista a la clave / pilotar la clave con la vista ----
export function viewToKey(k) {
  const q = new THREE.Quaternion(...k.q), p = new THREE.Vector3(...k.p);
  editCam.position.copy(p);
  orbit.target.copy(p.clone().add(new THREE.Vector3(0, 0, -3).applyQuaternion(q)));
  orbit.update();
}
let pilotLast = '';
export function setPilot(on) {
  if (isPose()) return;
  const one = KF.sel.size === 1 ? KF.doc.keys[[...KF.sel][0]] : null;
  if (on && !one) return;
  KF.pilot = on;
  if (on) { kfPush(); viewToKey(one); pilotLast = ''; }
  kfEmit();
}

// ---- trayectoria y claves en la vista 3D (solo en la vista de edicion, nunca en el monitor) ----
export const pathGroup = new THREE.Group(); scene.add(pathGroup); editorOnly.add(pathGroup);
const lineMat = new THREE.LineBasicMaterial({ color: 0xe3a043, transparent: true, opacity: 0.85, depthTest: false });
const tickMat = new THREE.PointsMaterial({ color: 0xe3a043, size: 4, sizeAttenuation: false, depthTest: false, transparent: true, opacity: 0.6 });
const coneGeo = new THREE.ConeGeometry(0.12, 0.32, 4); coneGeo.rotateX(-Math.PI / 2); coneGeo.translate(0, 0, -0.16);
const keyMat = new THREE.MeshBasicMaterial({ color: 0xe3a043, depthTest: false }), selMat = new THREE.MeshBasicMaterial({ color: 0xffffff, depthTest: false });
export function rebuildPath() {
  for (const c of [...pathGroup.children]) { pathGroup.remove(c); c.geometry !== coneGeo && c.geometry?.dispose?.(); }
  if (!KF.on || !KF.doc || isPose()) return;
  const d = KF.doc, n = Math.max(2, Math.min(600, Math.round(d.dur / FRAME_MS))), pts = [];
  for (let i = 0; i <= n; i++) pts.push(poseAt(d.dur * i / n).p);
  const line = new THREE.Line(new THREE.BufferGeometry().setFromPoints(pts), lineMat); line.renderOrder = 999;
  pathGroup.add(line);
  // un punto por segundo: se ve donde la camara va deprisa (puntos separados) o despacio (juntos)
  const ticks = [];
  for (let t = 0; t <= d.dur; t += 250) ticks.push(poseAt(t).p);
  const tp = new THREE.Points(new THREE.BufferGeometry().setFromPoints(ticks), tickMat); tp.renderOrder = 999;
  pathGroup.add(tp);
  if (d.keys.length > 400) return;   // grabaciones densas del movil: solo la linea
  d.keys.forEach((k, i) => {
    const m = new THREE.Mesh(coneGeo, KF.sel.has(i) ? selMat : keyMat);
    m.position.set(...k.p); m.quaternion.set(...k.q); m.renderOrder = 1000; m.userData.keyIndex = i;
    pathGroup.add(m);
  });
}

// ---- cada fotograma: monitor con la copia, bucle, pilotaje ----
onFrame(() => {
  if (!KF.on || !KF.doc) return;
  const d = KF.doc;
  if (S.playing && S.t >= d.t + d.dur) { if (KF.loop) seek(d.t); else setPlaying(false); }
  if (isPose()) return;
  if (KF.pilot) {   // la clave elegida sigue a la vista 3D (el giro sobre el eje se conserva)
    const k = d.keys[[...KF.sel][0]];
    if (!k) KF.pilot = false;
    else {
      const P = viewPose(), sig = P.p.map(v => v.toFixed(4)).join() + P.q.map(v => v.toFixed(4)).join();
      if (sig !== pilotLast) {
        pilotLast = sig;
        const roll = chanOf(k, 'roll');
        k.p = r4(P.p); k.q = r4(P.q); setChan(k, 'roll', roll);
        rebuildPath(); kfNotify();
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
  const k = e.key, c = e.ctrlKey || e.metaKey, lk = k.toLowerCase();
  const done = () => { e.preventDefault(); e.stopImmediatePropagation(); };
  if (k === ' ') { setPlaying(!S.playing); done(); }
  else if (c && lk === 'z') { e.shiftKey ? kfRedo() : kfUndo(); done(); }
  else if (c && lk === 'y') { kfRedo(); done(); }
  else if (c && lk === 'c') { copyKeys(); done(); }
  else if (c && lk === 'v') { pasteKeys(); done(); }
  else if (c && lk === 'd') { copyKeys(); pasteKeys(kfTime() + FRAME_MS * 10); done(); }
  else if (c && lk === 'a') { selectAll(); done(); }
  else if (k === 'Delete' || k === 'Backspace') { delKeys(); done(); }
  else if (!c && lk === 'k') { addKey(!e.shiftKey); done(); }
  else if (!c && lk === 'p') { setPilot(!KF.pilot); done(); }
  else if (!c && lk === 'g') { KF.tool = 'move'; kfEmit(); done(); }
  else if (!c && lk === 'r') { KF.tool = 'rotate'; kfEmit(); done(); }
  else if (!c && lk === 'l') { KF.space = KF.space === 'world' ? 'local' : 'world'; kfEmit(); done(); }
  else if (!c && lk === 'i') { KF.autokey = !KF.autokey; kfEmit(); done(); }
  else if (k === 'ArrowLeft' || k === 'ArrowRight') {
    const dir = k === 'ArrowLeft' ? -1 : 1;
    if (e.altKey) nudgeKeys(dir * FRAME_MS * (e.shiftKey ? 10 : 1));   // Alt: mover las claves elegidas
    else if (e.shiftKey) goKey(dir); else kfSeek(kfTime() + dir * FRAME_MS);
    done();
  } else if (k === 'Home') { kfSeek(0); done(); }
  else if (k === 'End') { kfSeek(KF.doc.dur); done(); }
  else if (k === 'Escape') { if (KF.pilot) setPilot(false); else if (KF.sel.size) { KF.sel = new Set(); kfEmit(); } else emit('kfAskClose'); done(); }
  else if (k === 'Enter' && c) { kfClose(true); done(); }
}, true);

// ---- simplificar (grabaciones del movil: cientos de claves -> las imprescindibles) ----
// Ramer-Douglas-Peucker sobre la pose: una clave se quita si la interpolacion lineal entre sus vecinas conservadas
// la reproduce dentro de la tolerancia (posicion en metros, giro y FOV en grados).
export function simplifyKeys(tolPos = 0.01, tolDeg = 0.4) {
  const K = KF.doc.keys;
  if (K.length < 3 || isPose()) return 0;
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
