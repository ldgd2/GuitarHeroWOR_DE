// Manipulacion directa en la vista 3D mientras el editor de animacion esta abierto (como en Blender o Unity):
//   camara  clic en un cono = elegir clave (Mayus/Ctrl: anadir o quitar). El gizmo mueve (G) o gira (R) las claves
//           elegidas; con varias, gira alrededor de su centro. Sin clave elegida, el gizmo esta en la camara del
//           cursor y, con "clave automatica" (I), al arrastrarlo se crea una clave en el cursor.
//   pose    se ven las articulaciones del musico; clic en una = elegir hueso. El gizmo gira el hueso (y mueve raiz o
//           pelvis) y escribe el retoque en la clave del cursor (la crea si hace falta).
// L alterna ejes del mundo / propios. Mientras arrastras la vista no orbita.
import * as THREE from 'three';
import { TransformControls } from 'three/addons/controls/TransformControls.js';
import { editCam, editorOnly, onFrame, scene, view } from '../viewport/scene.js';
import { orbit } from '../viewport/controls.js';
import { charOf } from '../viewport/band.js';
import { MOVABLE, POSE_BONES } from '../rig/poseLayer.js';
import {
  ensureKeyAtCursor, isPose, KF, keyIndexAt, kfEmit, kfNotify, kfPush, kfSeek, kfSub, kfTime, pathGroup, poseAt, r4, rebuildPath,
  selKeys, snapT,
} from './kf.js';

const tc = new TransformControls(editCam, view);
tc.setSize(0.85);
scene.add(tc); editorOnly.add(tc);
const proxy = new THREE.Object3D(); scene.add(proxy);

// ---- articulaciones del musico (modo pose) ----
const joints = new THREE.Group(); scene.add(joints); editorOnly.add(joints);
const jGeo = new THREE.SphereGeometry(0.028, 12, 8);
const jMat = { idle: new THREE.MeshBasicMaterial({ color: 0x9aa3b2, depthTest: false, transparent: true, opacity: 0.85 }),
  keyed: new THREE.MeshBasicMaterial({ color: 0xe3a043, depthTest: false }), sel: new THREE.MeshBasicMaterial({ color: 0xffffff, depthTest: false }) };
const jMesh = new Map();
for (const [i] of POSE_BONES) { const m = new THREE.Mesh(jGeo, jMat.idle); m.renderOrder = 1001; m.userData.bone = i; joints.add(m); jMesh.set(i, m); }
joints.visible = false;

const keyedBones = () => { const s = new Set(); for (const k of KF.doc?.keys || []) for (const i of Object.keys(k.b || {})) s.add(+i); return s; };

// ---- donde va el gizmo ----
const _v = new THREE.Vector3(), _q = new THREE.Quaternion();
function camTargets() { return selKeys(); }
function placeProxy() {
  if (isPose()) {
    const c = charOf(KF.doc.role), b = KF.bone != null ? c?.bones?.[KF.bone] : null;
    if (!b) return false;
    b.getWorldPosition(proxy.position); b.getWorldQuaternion(proxy.quaternion);
    return true;
  }
  const ks = camTargets();
  if (ks.length === 1) { proxy.position.fromArray(ks[0].p); proxy.quaternion.fromArray(ks[0].q); }
  else if (ks.length > 1) {
    proxy.position.set(0, 0, 0); for (const k of ks) proxy.position.add(_v.fromArray(k.p)); proxy.position.divideScalar(ks.length);
    proxy.quaternion.fromArray(ks[0].q);
  } else { const W = poseAt(kfTime()); proxy.position.copy(W.p); proxy.quaternion.copy(W.q); }
  return true;
}
function sync() {
  joints.visible = KF.on && isPose();
  if (!KF.on) { if (tc.object) tc.detach(); return; }
  const rotOnly = isPose() && !MOVABLE.has(KF.bone);
  tc.setMode(KF.tool === 'rotate' || rotOnly ? 'rotate' : 'translate');
  tc.setSpace(KF.space);
  if (tc.dragging) return;
  if (placeProxy()) { if (tc.object !== proxy) tc.attach(proxy); } else if (tc.object) tc.detach();
}
kfSub(sync);

onFrame(() => {
  if (!KF.on) { if (tc.object) tc.detach(); joints.visible = false; return; }
  if (isPose()) {
    const c = charOf(KF.doc.role), keyed = keyedBones();
    for (const [i, m] of jMesh) {
      const b = c?.bones?.[i];
      m.visible = !!b;
      if (!b) continue;
      b.getWorldPosition(m.position);
      m.material = i === KF.bone ? jMat.sel : keyed.has(i) ? jMat.keyed : jMat.idle;
    }
  }
  if (!tc.dragging) sync();   // el cursor se mueve con la reproduccion: el gizmo le sigue
});

// ---- arrastre ----
const D = { on: false };
tc.addEventListener('dragging-changed', e => {
  orbit.enabled = !e.value;
  if (e.value) startDrag(); else endDrag();
});
tc.addEventListener('objectChange', () => { if (D.on) dragUpdate(); });

function startDrag() {
  D.on = true; D.p0 = proxy.position.clone(); D.q0 = proxy.quaternion.clone();
  kfPush();
  if (isPose()) {
    const c = charOf(KF.doc.role), b = c?.bones?.[KF.bone];
    if (!b || !c.pre) { D.on = false; return; }
    const rt = snapT(kfTime(), true);
    if (!KF.autokey && keyIndexAt(rt) < 0) { D.on = false; D.blocked = true; return; }
    D.key = ensureKeyAtCursor(); D.bone = KF.bone;
    D.base = c.pre.q[KF.bone].clone(); D.basePos = c.pre.p[KF.bone].clone();
    D.parentQ = b.parent.getWorldQuaternion(new THREE.Quaternion()); D.parent = b.parent;
    KF.sel = new Set([KF.doc.keys.indexOf(D.key)]);
    return;
  }
  let ks = camTargets();
  if (!ks.length) {
    if (!KF.autokey) { D.on = false; D.blocked = true; return; }
    const k = ensureKeyAtCursor(); KF.sel = new Set([KF.doc.keys.indexOf(k)]); ks = [k];
  }
  D.keys = ks.map(k => ({ k, p: new THREE.Vector3().fromArray(k.p), q: new THREE.Quaternion().fromArray(k.q) }));
}

const dq = new THREE.Quaternion(), inv = new THREE.Quaternion(), ld = new THREE.Quaternion();
function dragUpdate() {
  if (isPose()) {
    const k = D.key, i = D.bone;
    if (tc.mode === 'rotate') {
      // mundo deseado -> local deseado -> giro extra sobre la pose de la animacion
      ld.copy(D.parentQ).invert().multiply(proxy.quaternion);
      const off = D.base.clone().invert().multiply(ld).normalize();
      (k.b ||= {})[i] = r4(off.toArray());
    } else if (MOVABLE.has(i)) {
      const lp = D.parent.worldToLocal(proxy.position.clone()).sub(D.basePos);
      (k.bp ||= {})[i] = r4(lp.toArray());
      if (!k.b?.[i]) (k.b ||= {})[i] = [0, 0, 0, 1];
    }
    kfNotify();
    return;
  }
  if (tc.mode === 'translate') {
    _v.copy(proxy.position).sub(D.p0);
    for (const e of D.keys) e.k.p = r4(e.p.clone().add(_v).toArray());
  } else {
    dq.copy(proxy.quaternion).multiply(inv.copy(D.q0).invert());   // giro en el mundo desde el inicio
    for (const e of D.keys) {
      e.k.q = r4(dq.clone().multiply(e.q).normalize().toArray());
      if (D.keys.length > 1) e.k.p = r4(e.p.clone().sub(D.p0).applyQuaternion(dq).add(D.p0).toArray());
    }
  }
  rebuildPath(); kfNotify();
}
function endDrag() {
  if (D.blocked) { KF.undo.pop(); D.blocked = false; }
  D.on = false;
  kfEmit();
}

// ---- elegir con clic (sin arrastrar) ----
const ray = new THREE.Raycaster();
let down = null;
view.addEventListener('pointerdown', e => { down = [e.clientX, e.clientY]; });
view.addEventListener('pointerup', e => {
  if (!KF.on || !down || e.button !== 0 || Math.hypot(e.clientX - down[0], e.clientY - down[1]) > 4 || tc.dragging || tc.axis) return;
  const r = view.getBoundingClientRect();
  ray.setFromCamera(new THREE.Vector2(((e.clientX - r.left) / r.width) * 2 - 1, -((e.clientY - r.top) / r.height) * 2 + 1), editCam);
  const add = e.shiftKey || e.ctrlKey;
  if (isPose()) {
    // la articulacion mas cercana al rayo (en pantalla son pequenas: se busca por distancia al rayo)
    let best = null;
    for (const [i, m] of jMesh) {
      if (!m.visible) continue;
      const d = ray.ray.distanceToPoint(m.position), lim = 0.012 * m.position.distanceTo(editCam.position) + 0.02;
      if (d < lim && (!best || d < best.d)) best = { i, d };
    }
    if (best) { KF.bone = best.i; if (!KF.chans.some(c => c[0] === 'r' || c[0] === 't')) KF.chans = ['rx', 'ry', 'rz']; kfEmit(); }
    return;
  }
  const hit = ray.intersectObjects(pathGroup.children.filter(o => o.userData.keyIndex !== undefined), false)[0];
  if (hit) {
    const i = hit.object.userData.keyIndex;
    if (add) { KF.sel.has(i) ? KF.sel.delete(i) : KF.sel.add(i); } else KF.sel = new Set([i]);
    kfSeek(KF.doc.keys[i].t);
  } else if (!add) KF.sel = new Set();
  kfEmit();
});
