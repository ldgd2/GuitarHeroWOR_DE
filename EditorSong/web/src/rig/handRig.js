// Manos y dedos de la captura (MediaPipe Hand Landmarker) -> los 30 huesos de dedos del esqueleto de WoR.
// El esqueleto tiene 3 huesos por dedo (base, medio, punta) y MediaPipe da 4 puntos por dedo: cada hueso se orienta
// hacia el siguiente punto, igual que brazos y piernas. Los puntos de la mano estan en los mismos ejes que los del
// cuerpo, asi que pasan al personaje con la misma regla (x, -y, -z).
//   palma (10 / 14): apunta de la muneca a la base del corazon y gira hasta que el indice y el menique quedan como
//   en la mano real. Despues cada falange.
// Sin mano detectada (fuera de cuadro, tapada) los dedos se quedan como estaban (la animacion o el reposo); con el
// micro en la mano y sin datos de dedos, se cierran alrededor de el.
import * as THREE from 'three';
import { HAND_FINGERS, HAND_OFF } from '../capture/channels.js';
import { rotateTo } from '../viewport/ik.js';

// huesos por dedo, en el orden de HAND_FINGERS (indice, corazon, anular, menique, pulgar)
export const FINGER_BONES = {
  L: [[33, 34, 35], [39, 40, 41], [45, 46, 47], [51, 52, 53], [57, 58, 59]],
  R: [[36, 37, 38], [42, 43, 44], [48, 49, 50], [54, 55, 56], [60, 61, 62]],
};
export const HAND_BONES = [...FINGER_BONES.L.flat(), ...FINGER_BONES.R.flat()];
const PALM = { L: 10, R: 14 };
const MAX_BEND = [1.75, 1.9, 1.6];          // radianes por falange respecto a la anterior (base, medio, punta)

const ramp = (v, a, b) => { const x = Math.min(1, Math.max(0, (v - a) / (b - a))); return x * x * (3 - 2 * x); };
const rq = new THREE.Quaternion(), q0 = new THREE.Quaternion(), wq = new THREE.Quaternion(), pq = new THREE.Quaternion(),
  a = new THREE.Vector3(), b = new THREE.Vector3(), from = new THREE.Vector3(), to = new THREE.Vector3(), X = new THREE.Vector3(1, 0, 0),
  ax = new THREE.Vector3(), cur = new THREE.Vector3(), tgt = new THREE.Vector3();
const dirOf = (bone, out) => out.copy(X).applyQuaternion(bone.getWorldQuaternion(wq));   // los dedos crecen por +X local

// presencia de la mano 'L' / 'R' (del cantante) en un fotograma
export function handPresence(lm, side) { const o = HAND_OFF[side]; return lm.length > o + 63 ? lm[o] : 0; }

// lm: fotograma completo (BODY_FULL). cfg.swapLR como en el cuerpo. Devuelve que manos se han aplicado.
export function applyHands(c, lm, cfg) {
  const done = { L: false, R: false };
  if (cfg.fingers === false || lm.length < HAND_OFF.R + 64) return done;
  c.root.getWorldQuaternion(rq);
  const sx = cfg.swapLR ? -1 : 1;
  for (const side of ['L', 'R']) {
    const src = cfg.swapLR ? (side === 'L' ? 'R' : 'L') : side, o = HAND_OFF[src];
    const w = ramp(lm[o], 0.3, 0.7); if (w <= 0.001) continue;
    const P = i => new THREE.Vector3(sx * lm[o + 1 + i * 3], -lm[o + 2 + i * 3], -lm[o + 3 + i * 3]).applyQuaternion(rq);
    const pts = Array.from({ length: 21 }, (_, i) => P(i));
    const bones = FINGER_BONES[side], palm = c.bones[PALM[side]];
    if (!palm || !c.bones[bones[1][0]] || !c.bones[bones[0][0]] || !c.bones[bones[3][0]]) continue;
    // palma: hacia el corazon y con el indice y el menique en su sitio
    q0.copy(palm.quaternion);
    palm.getWorldPosition(a); c.bones[bones[1][0]].getWorldPosition(b);
    rotateTo(palm, from.subVectors(b, a), to.subVectors(pts[9], pts[0]));
    to.normalize();
    c.bones[bones[0][0]].getWorldPosition(a); c.bones[bones[3][0]].getWorldPosition(b);
    cur.subVectors(a, b).addScaledVector(to, -a.clone().sub(b).dot(to));
    tgt.subVectors(pts[5], pts[17]); tgt.addScaledVector(to, -tgt.dot(to));
    if (cur.lengthSq() > 1e-10 && tgt.lengthSq() > 1e-10) {
      cur.normalize(); tgt.normalize();
      const ang = Math.atan2(ax.crossVectors(cur, tgt).dot(to), cur.dot(tgt));
      palm.getWorldQuaternion(wq).premultiply(pq.setFromAxisAngle(to, ang));
      palm.parent.getWorldQuaternion(pq); palm.quaternion.copy(pq.invert().multiply(wq));
    }
    if (w < 0.999) palm.quaternion.slerpQuaternions(q0, palm.quaternion, w);
    palm.updateMatrixWorld(true);
    // falanges: cada hueso hacia el siguiente punto, sin doblarse mas de lo que dobla un dedo
    HAND_FINGERS.forEach((L, f) => {
      const ch = bones[f];
      for (let j = 0; j < 3; j++) {
        const B = c.bones[ch[j]]; if (!B) break;
        q0.copy(B.quaternion);
        to.subVectors(pts[L[j + 1]], pts[L[j]]); if (to.lengthSq() < 1e-10) continue;
        to.normalize();
        // tope respecto al hueso padre (falange anterior o palma): sin dedos rotos por un punto mal detectado
        dirOf(B.parent, a);
        const ang = a.angleTo(to);
        if (ang > MAX_BEND[j]) to.copy(a).lerp(to, MAX_BEND[j] / ang).normalize();
        rotateTo(B, dirOf(B, from), to);
        if (w < 0.999) { B.quaternion.slerpQuaternions(q0, B.quaternion, w); B.updateMatrixWorld(true); }
      }
    });
    done[side] = true;
  }
  return done;
}

// dedos cerrados alrededor de un punto (el micro) cuando no hay datos de la mano: cada falange gira hacia el
const GRIP = [[0.95, 1.15, 0.7], [1.0, 1.2, 0.75], [1.05, 1.2, 0.75], [1.1, 1.15, 0.7], [0.45, 0.55, 0.4]];
export function gripAround(c, side, center, w = 1) {
  FINGER_BONES[side].forEach((ch, f) => {
    for (let j = 0; j < 3; j++) {
      const B = c.bones[ch[j]]; if (!B) return;
      q0.copy(B.quaternion);
      B.getWorldPosition(a); dirOf(B, from);
      to.subVectors(center, a).addScaledVector(from, -center.clone().sub(a).dot(from));   // hacia el micro, perpendicular
      if (to.lengthSq() < 1e-10) continue;
      to.normalize();
      const k = GRIP[f][j];
      rotateTo(B, from.clone(), from.clone().multiplyScalar(Math.cos(k)).addScaledVector(to, Math.sin(k)));
      if (w < 0.999) { B.quaternion.slerpQuaternions(q0, B.quaternion, w); B.updateMatrixWorld(true); }
    }
  });
}
