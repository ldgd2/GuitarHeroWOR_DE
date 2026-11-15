// Retoques de pose de los musicos: una capa de claves por hueso que se suma ENCIMA de lo que ya suena (loop, clip,
// captura del movil). Sirve para arreglar o exagerar una animacion del juego sin rehacerla: girar la cabeza, abrir
// un brazo, inclinar el torso, bajar la pelvis...
//   S.proj.poseLayers = [{id, role, t, dur, track, name, fade, interp, keys: [{t, b: {hueso: [x,y,z,w]},
//                         bp: {hueso: [x,y,z]}, ease?, bez?, steps?}]}]
//   b  = giro EXTRA en el espacio local del hueso (final = animacion * b)
//   bp = desplazamiento extra en el espacio del padre (solo raiz y pelvis)
// Entre claves se interpola con la curva de tiempo de la clave (keyframes/easing.js); un hueso que una clave no
// tiene vale "sin cambio" en esa clave. En los bordes la capa entra y sale con un fundido (fade, ms).
// Al compilar se hornea la pose final (animacion + capa) a clips del juego (rig/poseExport.js).
import * as THREE from 'three';
import { S } from '../core/state.js';
import { easeValue } from '../keyframes/easing.js';
import { poseTrack, trackActive } from '../timeline/layout.js';

// Huesos que se pueden tocar (los del cuerpo del juego; la cara va con la captura de rostro)
export const POSE_BONES = [
  [0, 'Raiz', 'torso'], [1, 'Pelvis', 'torso'], [2, 'Abdomen bajo', 'torso'], [3, 'Abdomen alto', 'torso'], [4, 'Pecho', 'torso'],
  [5, 'Cuello', 'head'], [6, 'Cabeza', 'head'],
  [7, 'Clavicula izq.', 'armL'], [8, 'Brazo izq.', 'armL'], [9, 'Antebrazo izq.', 'armL'], [10, 'Mano izq.', 'armL'],
  [33, 'Indice izq.', 'armL'], [39, 'Corazon izq.', 'armL'], [57, 'Pulgar izq.', 'armL'],
  [11, 'Clavicula der.', 'armR'], [12, 'Brazo der.', 'armR'], [13, 'Antebrazo der.', 'armR'], [14, 'Mano der.', 'armR'],
  [36, 'Indice der.', 'armR'], [42, 'Corazon der.', 'armR'], [60, 'Pulgar der.', 'armR'],
  [15, 'Muslo izq.', 'legs'], [16, 'Rodilla izq.', 'legs'], [17, 'Tobillo izq.', 'legs'], [25, 'Pie izq.', 'legs'],
  [18, 'Muslo der.', 'legs'], [19, 'Rodilla der.', 'legs'], [20, 'Tobillo der.', 'legs'], [26, 'Pie der.', 'legs'],
];
export const POSE_GROUPS = [['torso', 'Torso'], ['head', 'Cuello y cabeza'], ['armL', 'Brazo izquierdo'], ['armR', 'Brazo derecho'], ['legs', 'Piernas']];
export const boneName = i => POSE_BONES.find(b => b[0] === i)?.[1] || `Hueso ${i}`;
export const boneGroup = i => POSE_BONES.find(b => b[0] === i)?.[2] || 'torso';
export const MOVABLE = new Set([0, 1]);          // huesos que tambien se pueden desplazar
export const ARM_L = new Set([7, 8, 9, 10, 33, 34, 35, 39, 40, 41, 57, 58, 59]), ARM_R = new Set([11, 12, 13, 14, 36, 37, 38, 42, 43, 44, 60, 61, 62]);

export const poseLayers = () => (S.proj ? (S.proj.poseLayers ||= []) : []);
export const layerOf = sel => poseLayers().find(l => l.id === sel?.id);
// el editor de animacion pone aqui su copia mientras esta abierto (asi el visor muestra lo que se edita)
export const POSE_EDIT = { id: null, doc: null };

export function newLayer(role, t, dur = 4000) {
  const id = Math.max(0, ...poseLayers().map(l => l.id)) + 1;
  return { id, role, t: Math.round(t), dur, name: `Retoque ${id}`, fade: 200, interp: 'sineInOut', keys: [] };
}
export const keyEaseOf = (L, k) => (k.ease && k.ease !== 'auto' ? k.ease : L.interp || 'sineInOut');

// ---- muestreo: rt = ms desde el inicio de la capa -> Map(hueso -> {q, p}) ----
const ID = new THREE.Quaternion(), _a = new THREE.Quaternion(), _b = new THREE.Quaternion();
const qOf = (k, i, out) => (k.b?.[i] ? out.fromArray(k.b[i]) : out.copy(ID));
const pOf = (k, i) => k.bp?.[i] || [0, 0, 0];
export function layerAt(L, rt) {
  const K = L.keys, out = new Map();
  if (!K?.length) return out;
  let a = K[0], b = K[0], f = 0;
  if (rt >= K.at(-1).t) { a = b = K.at(-1); }
  else if (rt > K[0].t) {
    let i = 1; while (i < K.length - 1 && K[i].t < rt) i++;
    a = K[i - 1]; b = K[i];
    f = easeValue(keyEaseOf(L, a), a, (rt - a.t) / Math.max(1e-6, b.t - a.t));
  }
  const bones = new Set([...Object.keys(a.b || {}), ...Object.keys(b.b || {}), ...Object.keys(a.bp || {}), ...Object.keys(b.bp || {})].map(Number));
  for (const i of bones) {
    const q = qOf(a, i, _a).clone().slerp(qOf(b, i, _b), f);
    const pa = pOf(a, i), pb = pOf(b, i);
    const hasP = a.bp?.[i] || b.bp?.[i];
    out.set(i, { q, p: hasP ? new THREE.Vector3(pa[0] + (pb[0] - pa[0]) * f, pa[1] + (pb[1] - pa[1]) * f, pa[2] + (pb[2] - pa[2]) * f) : null });
  }
  return out;
}
// peso de la capa en rt (fundido de entrada y salida)
export function layerWeight(L, rt) {
  if (rt < 0 || rt > L.dur) return 0;
  const fd = Math.max(0, L.fade ?? 200);
  return fd ? Math.min(1, rt / fd, (L.dur - rt) / fd) : 1;
}

// capas de un musico activas en t (con la copia del editor si se esta editando)
export function activeLayers(role, t) {
  const out = [];
  for (const L of poseLayers()) {
    if (L.role !== role || !trackActive(poseTrack(L))) continue;
    const D = POSE_EDIT.id === L.id && POSE_EDIT.doc ? POSE_EDIT.doc : L;
    if (t >= D.t && t <= D.t + D.dur && D.keys?.length) out.push(D);
  }
  return out;
}

// ---- en el visor: despues de animar, IK y captura (band.js) ----
// c.pre guarda la pose ANTES de la capa: el gizmo la necesita para convertir lo que arrastras en el giro extra.
const _w = new THREE.Quaternion();
export function applyPoseLayers(role, c, t) {
  if (!c?.bones) return;
  const pre = (c.pre ||= { q: c.bones.map(() => new THREE.Quaternion()), p: c.bones.map(() => new THREE.Vector3()) });
  for (const [i] of POSE_BONES) { const b = c.bones[i]; if (b) { pre.q[i].copy(b.quaternion); pre.p[i].copy(b.position); } }
  if (!S.proj) return;
  for (const L of activeLayers(role, t)) {
    const rt = t - L.t, w = layerWeight(L, rt);
    if (w <= 0) continue;
    for (const [i, v] of layerAt(L, rt)) {
      const b = c.bones[i]; if (!b) continue;
      b.quaternion.multiply(w >= 1 ? v.q : _w.copy(ID).slerp(v.q, w));
      if (v.p) b.position.addScaledVector(v.p, w);
    }
  }
}

// huesos que toca una capa (para el horneado y para elegir el IK del clip)
export function layerBones(L) {
  const s = new Set();
  for (const k of L.keys || []) { for (const i of Object.keys(k.b || {})) s.add(+i); for (const i of Object.keys(k.bp || {})) s.add(+i); }
  return s;
}
