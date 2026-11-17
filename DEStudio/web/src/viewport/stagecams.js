// Camaras de escenario (cortes autocut y planos de escenario de la pista de momento), como las mueve el juego.
// Datos: /api/venue/cameras (editor/stage/cameras.py). Numero de corte -> nombre de camara -> struct del escenario.
//   CameraCuts_HandCam   plano anclado al musico, en mano, siguiendo su hueso LookAtBone
//   CameraCuts_TwoCam    Cam1 -> Cam2 durante el corte (zoom in / zoom out / travelling)
//   CameraCuts_ThreePos, CameraCuts_SpecialStepMove: igual, de Cam1 a Cam2
//   CameraCuts_Orbit     gira orbitangle grados alrededor del musico
// pos/Quat relativos al nodo del musico (LockTo) o al mundo (World); la camara mira hacia su +Z.
import * as THREE from 'three';
import { api, S } from '../core/state.js';
import { boneWorld } from './band.js';
import { nodePos, nodeQuat } from './venue.js';

const ROLE_OF = { guitarist: 'guitarist', vocalist: 'vocalist', singer: 'vocalist', bassist: 'bassist', drummer: 'drummer' };
let data = null, zone = '';

export async function loadStageCams(z) {
  zone = z; data = null;
  try { const d = await api('/api/venue/cameras?zone=' + encodeURIComponent(z)); if (zone === z) data = d; }
  catch { data = { cameras: [], cut_names: [] }; }
}

const lc = s => (s || '').toLowerCase();
// variante elegida de forma estable para cada corte (el juego elige al azar entre las de un mismo tipo)
function choose(list, seed) { return list[Math.abs(Math.floor(seed / 97)) % list.length]; }

export function stageCamFor(num, cutStart) {
  if (!data) return null;
  const name = data.cut_names[num] || '';
  if (!name) return null;
  const exact = data.cameras.filter(c => lc(c.name) === name);
  const typed = data.cameras.filter(c => lc(c.type) === name);
  const pool = typed.length ? typed : exact;
  return pool.length ? { cam: choose(pool, cutStart), name } : { cam: null, name };
}
export const cutName = num => data?.cut_names[num] || '';
// camaras del escenario disponibles en este escenario: [[numero de corte, nombre]]
export const stageCamChoices = () => (data?.cut_names || []).map((n, i) => [i, n]).filter(([i, n]) => n && stageCamFor(i, 0)?.cam);

const q = new THREE.Quaternion(), FLIP = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 1, 0), Math.PI);
const quatOf = a => { const [x, y, z] = a || [0, 0, 0]; return new THREE.Quaternion(x, y, z, Math.sqrt(Math.max(0, 1 - x * x - y * y - z * z))); };

function anchor(lockto) {
  const role = ROLE_OF[lc(lockto)];
  return role ? { p: nodePos(role), q: nodeQuat(role), role } : { p: new THREE.Vector3(), q: new THREE.Quaternion(), role: null };
}
// pose de un struct {lockto, pos, quat, fov} -> {p, q (three, mira a -Z), fov}
function pose(c) {
  const a = anchor(c.lockto);
  const p = new THREE.Vector3(...(c.pos || [0, 0, 0])).applyQuaternion(a.q).add(a.p);
  const r = a.q.clone().multiply(quatOf(c.quat)).multiply(FLIP);
  return { p, q: r, fov: c.fov || 50, role: a.role };
}

// camara en mano: suma de senos (lenta y suave, como un operador)
function handheld(t, amt = 1) {
  const n = (f, ph) => Math.sin(t * f + ph) * 0.6 + Math.sin(t * f * 2.3 + ph * 1.7) * 0.4;
  return {
    off: new THREE.Vector3(n(0.9, 1), n(0.7, 2), n(0.5, 3)).multiplyScalar(0.012 * amt),
    rot: new THREE.Euler(n(1.1, 4) * 0.006 * amt, n(0.8, 5) * 0.008 * amt, n(0.6, 6) * 0.004 * amt),
  };
}

const track = new Map();   // seguimiento del hueso objetivo por corte
const ease = u => u * u * (3 - 2 * u);

// Coloca `cam` (PerspectiveCamera) en el instante t (ms) de un corte [inicio, duracion]. Devuelve texto o null.
export function applyStageCam(cam, sel, cut, t, aspect) {
  const c = sel.cam; if (!c) return null;
  const u = Math.min(1, Math.max(0, (t - cut[0]) / Math.max(1, cut[1])));
  const ts = t / 1000;
  let P;
  if (c.cam1 && c.cam2) {
    const A = pose(c.cam1), B = pose(c.cam2), k = ease(u);
    P = { p: A.p.lerp(B.p, k), q: A.q.slerp(B.q, k), fov: A.fov + (B.fov - A.fov) * k, role: A.role };
  } else P = pose(c);
  if (c.script === 'CameraCuts_Orbit' && P.role) {
    const a = anchor(c.lockto), ang = THREE.MathUtils.degToRad((c.orbitangle || 0) * ease(u));
    const r = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 1, 0), ang);
    P.p.sub(a.p).applyQuaternion(r).add(a.p); P.q.premultiply(r);
  }
  // HandCam: sigue el movimiento del hueso objetivo (mantiene el encuadre del autor) y tiembla como en mano
  if (c.script === 'CameraCuts_HandCam' || c.usehandcam) {
    const role = ROLE_OF[lc(c.lookat)];
    const b = role && c.lookatbone ? boneWorld(role, c.lookatbone) : null;
    if (b) {
      const key = cut[0] + ':' + cut[2];
      if (!track.has(key)) { track.clear(); track.set(key, b.clone()); }
      const d0 = track.get(key).clone().sub(P.p).normalize(), d1 = b.clone().sub(P.p).normalize();
      P.q.premultiply(new THREE.Quaternion().slerp(new THREE.Quaternion().setFromUnitVectors(d0, d1), 0.85));
    }
    const h = handheld(ts, c.script === 'CameraCuts_HandCam' ? 1 : 0.6);
    P.p.add(h.off.applyQuaternion(P.q)); P.q.multiply(q.setFromEuler(h.rot));
  }
  cam.position.copy(P.p); cam.quaternion.copy(P.q);
  // FOV horizontal (como en las camaras .ska) -> vertical de three.js
  cam.fov = THREE.MathUtils.radToDeg(2 * Math.atan(Math.tan(THREE.MathUtils.degToRad(P.fov) / 2) / aspect));
  const kind = { CameraCuts_HandCam: 'en mano', CameraCuts_TwoCam: 'movimiento', CameraCuts_Orbit: 'orbita',
    CameraCuts_ThreePos: 'movimiento', CameraCuts_SpecialStepMove: 'movimiento', static: 'fija' }[c.script] || c.script;
  return `${c.name || sel.name} · ${kind} · FOV ${P.fov.toFixed(0)}°`;
}
