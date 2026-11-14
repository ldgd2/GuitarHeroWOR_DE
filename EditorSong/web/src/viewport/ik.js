// IK de dos huesos, como hace el juego al reproducir las animaciones de la banda.
// Los .ska de WoR NO animan antebrazos, manos, rodillas ni tobillos: traen huesos objetivo y el motor cierra
// brazo y pierna sobre ellos (medido en anim_loops y en los clips de las canciones):
//   93 / 94   -> pie izquierdo / derecho (en la pose de enlace coinciden exactamente con el tobillo)
//   126 / 127 -> mano izquierda / derecha (en guitarra quedan sobre el mastil y las cuerdas; en voz, en el micro)
// El codo y la rodilla se doblan hacia donde los deja la animacion (biceps y muslo si estan animados).
import * as THREE from 'three';

export const CHAINS = [
  { a: 8, b: 9, c: 10, t: 126 },    // brazo izquierdo
  { a: 12, b: 13, c: 14, t: 127 },  // brazo derecho
  { a: 15, b: 16, c: 17, t: 93 },   // pierna izquierda
  { a: 18, b: 19, c: 20, t: 94 },   // pierna derecha
];

const v1 = new THREE.Vector3(), v2 = new THREE.Vector3(), pa = new THREE.Vector3(), pb = new THREE.Vector3(),
  pc = new THREE.Vector3(), pt = new THREE.Vector3(), u = new THREE.Vector3(), w = new THREE.Vector3(),
  nb = new THREE.Vector3(), nc = new THREE.Vector3(), q = new THREE.Quaternion(), wq = new THREE.Quaternion(),
  pq = new THREE.Quaternion();

function setWorldQuat(bone, worldQ) {
  bone.parent.getWorldQuaternion(pq);
  bone.quaternion.copy(pq.invert().multiply(worldQ));
  bone.updateMatrixWorld(true);
}
export function rotateTo(bone, from, to) {
  if (from.lengthSq() < 1e-12 || to.lengthSq() < 1e-12) return;
  q.setFromUnitVectors(from.normalize(), to.normalize());
  bone.getWorldQuaternion(wq);
  setWorldQuat(bone, wq.premultiply(q));
}

// En la pose de enlace: rotacion de la mano/pie relativa a su objetivo (constante)
export function prepareIK(c) {
  c.root.updateMatrixWorld(true);
  c.ikOff = CHAINS.map(ch => {
    const T = c.bones[ch.t], E = c.bones[ch.c];
    if (!T || !E) return null;
    return T.getWorldQuaternion(new THREE.Quaternion()).invert().multiply(E.getWorldQuaternion(new THREE.Quaternion()));
  });
}

// tr: {target(b), keyed(b), hasRot(b)} sobre las pistas activas. Una cadena se cierra si su objetivo esta animado
// y su hueso medio no (o si se fuerza con only = indices de CHAINS).
export function solveIK(c, tr, enabled = true, only = null) {
  if (!enabled || !c.ikOff) return 0;
  c.root.updateMatrixWorld(true);
  c.ikErr = [0, 0, 0, 0];
  let n = 0;
  CHAINS.forEach((ch, k) => {
    if (only ? !only.includes(k) : (!tr.target(ch.t) || tr.keyed(ch.b))) return;
    const A = c.bones[ch.a], B = c.bones[ch.b], C = c.bones[ch.c], T = c.bones[ch.t];
    if (!A || !B || !C || !T) return;
    A.getWorldPosition(pa); B.getWorldPosition(pb); C.getWorldPosition(pc); T.getWorldPosition(pt);
    const l1 = pa.distanceTo(pb), l2 = pb.distanceTo(pc);
    u.subVectors(pt, pa);
    const dist = THREE.MathUtils.clamp(u.length(), Math.abs(l1 - l2) + 1e-4, (l1 + l2) * 0.9999);
    u.normalize();
    // plano de flexion: hacia donde apunta ahora el codo/rodilla
    w.subVectors(pb, pa); w.addScaledVector(u, -w.dot(u));
    if (w.lengthSq() < 1e-8) { w.set(0, 0, 1).applyQuaternion(c.root.quaternion); w.addScaledVector(u, -w.dot(u)); }
    w.normalize();
    const cosA = THREE.MathUtils.clamp((l1 * l1 + dist * dist - l2 * l2) / (2 * l1 * dist), -1, 1);
    nb.copy(pa).addScaledVector(u, cosA * l1).addScaledVector(w, Math.sqrt(1 - cosA * cosA) * l1);
    nc.copy(pa).addScaledVector(u, dist);
    rotateTo(A, v1.subVectors(pb, pa), v2.subVectors(nb, pa));
    C.getWorldPosition(pc);
    rotateTo(B, v1.subVectors(pc, nb), v2.subVectors(nc, nb));
    if (tr.hasRot(ch.t) && c.ikOff[k]) setWorldQuat(C, T.getWorldQuaternion(wq).multiply(c.ikOff[k]));
    C.getWorldPosition(pc); c.ikErr[k] = pc.distanceTo(pt);
    n++;
  });
  return n;
}

// Cierra el brazo k (0 izquierdo, 1 derecho) para que la palma quede en pos con orientacion quat (mundo).
export function solveArmTo(c, k, pos, quat) {
  const ch = CHAINS[k], A = c.bones[ch.a], B = c.bones[ch.b], C = c.bones[ch.c];
  if (!A || !B || !C) return false;
  c.root.updateMatrixWorld(true);
  A.getWorldPosition(pa); B.getWorldPosition(pb); C.getWorldPosition(pc); pt.copy(pos);
  const l1 = pa.distanceTo(pb), l2 = pb.distanceTo(pc);
  u.subVectors(pt, pa);
  const dist = THREE.MathUtils.clamp(u.length(), Math.abs(l1 - l2) + 1e-4, (l1 + l2) * 0.9999);
  u.normalize();
  w.subVectors(pb, pa); w.addScaledVector(u, -w.dot(u));
  if (w.lengthSq() < 1e-8) { w.set(0, -1, 0); w.addScaledVector(u, -w.dot(u)); }
  w.normalize();
  const cosA = THREE.MathUtils.clamp((l1 * l1 + dist * dist - l2 * l2) / (2 * l1 * dist), -1, 1);
  nb.copy(pa).addScaledVector(u, cosA * l1).addScaledVector(w, Math.sqrt(1 - cosA * cosA) * l1);
  nc.copy(pa).addScaledVector(u, dist);
  rotateTo(A, v1.subVectors(pb, pa), v2.subVectors(nb, pa));
  C.getWorldPosition(pc);
  rotateTo(B, v1.subVectors(pc, nb), v2.subVectors(nc, nb));
  if (quat) setWorldQuat(C, quat);
  return true;
}
