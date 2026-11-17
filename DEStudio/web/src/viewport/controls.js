// Camara de edicion: orbita + vuelo con WASD/QE, encuadres rapidos y centrar con doble clic.
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { ROLES } from '../core/roles.js';
import { VIEW } from '../core/view.js';
import { editCam, onFrame, progCam, scene, view } from './scene.js';
import { nodePos, nodeQuat, stageCenter } from './venue.js';

export const orbit = new OrbitControls(editCam, view);
orbit.enableDamping = true; orbit.dampingFactor = 0.12; orbit.screenSpacePanning = true;
orbit.target.set(0, 1, 4); orbit.update();

const keys = new Set();
let hover = false;
view.addEventListener('pointerenter', () => { hover = true; });
view.addEventListener('pointerleave', () => { hover = false; keys.clear(); });

// mueve camara y objetivo a la vez durante dur segundos
let tween = null;
export function moveTo(pos, target, dur = 0.45) {
  tween = { p0: editCam.position.clone(), t0: orbit.target.clone(), p1: pos.clone(), t1: target.clone(), k: 0, dur };
}

export function shotRole(role) {
  const n = nodePos(role), q = nodeQuat(role);
  const tgt = n.clone().add(new THREE.Vector3(0, 1.25, 0));
  const off = new THREE.Vector3(role === 'drummer' ? 0 : 0.6, 0.45, 3.4).applyQuaternion(q);
  moveTo(tgt.clone().add(off), tgt);
}
export function shotFront() { const c = stageCenter(); moveTo(c.clone().add(new THREE.Vector3(0, 3.5, 14)), c.clone().add(new THREE.Vector3(0, 1.2, 0))); }
export function shotTop() { const c = stageCenter(); moveTo(c.clone().add(new THREE.Vector3(0, 22, 0.01)), c); }
export function copyProgram() {
  const dir = new THREE.Vector3(0, 0, -1).applyQuaternion(progCam.quaternion);
  moveTo(progCam.position.clone(), progCam.position.clone().add(dir.multiplyScalar(4)), 0.3);
}

const ray = new THREE.Raycaster();
view.addEventListener('dblclick', e => {
  const r = view.getBoundingClientRect();
  ray.setFromCamera(new THREE.Vector2(((e.clientX - r.left) / r.width) * 2 - 1, -((e.clientY - r.top) / r.height) * 2 + 1), editCam);
  const hit = ray.intersectObjects(scene.children, true).find(h => h.object.visible && h.object.isMesh);
  if (!hit) return;
  const d = editCam.position.clone().sub(orbit.target);
  moveTo(hit.point.clone().add(d.setLength(Math.min(d.length(), 6))), hit.point);
});

addEventListener('keydown', e => {
  if (!hover || ['INPUT', 'SELECT', 'TEXTAREA'].includes(document.activeElement?.tagName) || e.ctrlKey) return;
  const k = e.key.toLowerCase();
  if ('wasdqe'.includes(k) && k.length === 1) { keys.add(k); e.preventDefault(); }
  if (e.shiftKey) keys.add('shift');
  const n = '1234'.indexOf(e.key);
  if (n >= 0) shotRole(ROLES[n]);
  else if (e.key === '0') shotFront();
  else if (e.key === '9') shotTop();
  else if (k === 'f') shotFront();
  else if (k === 'c') copyProgram();
});
addEventListener('keyup', e => { keys.delete(e.key.toLowerCase()); if (!e.shiftKey) keys.delete('shift'); });
addEventListener('blur', () => keys.clear());

const fwd = new THREE.Vector3(), right = new THREE.Vector3(), mv = new THREE.Vector3();
onFrame(dt => {
  if (tween) {
    tween.k = Math.min(1, tween.k + dt / tween.dur);
    const s = tween.k * tween.k * (3 - 2 * tween.k);
    editCam.position.lerpVectors(tween.p0, tween.p1, s); orbit.target.lerpVectors(tween.t0, tween.t1, s);
    if (tween.k >= 1) tween = null;
  }
  if (keys.size) {
    editCam.getWorldDirection(fwd); right.crossVectors(fwd, editCam.up).normalize();
    mv.set(0, 0, 0);
    if (keys.has('w')) mv.add(fwd); if (keys.has('s')) mv.sub(fwd);
    if (keys.has('d')) mv.add(right); if (keys.has('a')) mv.sub(right);
    if (keys.has('e')) mv.y += 1; if (keys.has('q')) mv.y -= 1;
    if (mv.lengthSq()) {
      mv.normalize().multiplyScalar(VIEW.flySpeed * (keys.has('shift') ? 4 : 1) * dt);
      editCam.position.add(mv); orbit.target.add(mv); tween = null;
    }
  }
  orbit.update();
});
if (new URLSearchParams(location.search).has('debug')) window.__orbit = orbit;   // pruebas automaticas
