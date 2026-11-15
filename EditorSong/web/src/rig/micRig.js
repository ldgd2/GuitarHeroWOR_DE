// Micro y pie del cantante en la captura de cuerpo.
// En WoR el micro no es un objeto suelto: cuelga de una cadena de huesos del cantante
//   122 pie (en el suelo) -> 123 parte alta del pie (122 + altura) -> 124 pinza -> 125 MICRO -> 126/127 manos
// y las animaciones del juego lo mueven: en el pie, 125 queda en lo alto del pie; en la mano, 125 va pegado a la
// palma con un agarre fijo y el pie se queda donde estaba (medido en las animaciones del juego: el agarre es el
// mismo en todos los tramos con el micro en la mano).
// La camara no reconoce un micro como objeto, pero no hace falta: se deduce de las manos (una mano cerrada junto
// a la boca lo esta sujetando; abierta lo ha soltado) y el pie se pone delante de la boca del personaje, a su
// altura. Entre "en el pie" y "en la mano" se pasa suave (el valor de agarre va de 0 a 1).
import * as THREE from 'three';
import { MIC_OFF } from '../capture/channels.js';
import { gripAround } from './handRig.js';

export const MIC_BONES = [122, 123, 124, 125];
// agarre (mano derecha, espacio local de la palma 14) y orientaciones del pie (marco del personaje), medidos
const GRIP_P = new THREE.Vector3(-0.043, 0.0613, 0.0039), GRIP_Q = new THREE.Quaternion(-0.3549, 0.1665, -0.8242, 0.4087).normalize();
const GRIP_PL = new THREE.Vector3(-0.043, 0.0613, -0.0039), GRIP_QL = new THREE.Quaternion(0.3549, -0.1665, -0.8242, 0.4087).normalize();  // espejo
const STAND_Q = {
  122: new THREE.Quaternion(0.4522, 0.4437, 0.5382, 0.5558).normalize(),
  123: new THREE.Quaternion(0.4522, 0.4437, 0.5382, 0.5558).normalize(),
  124: new THREE.Quaternion(0.5069, 0.3801, 0.607, 0.4798).normalize(),
  125: new THREE.Quaternion(0.0897, 0.6272, 0.0899, 0.7684).normalize(),
};
// el micro en el pie: un poco por encima y delante de la cabeza (medido: +0.13 m arriba, +0.13 m delante del hueso de la cabeza)
const MOUTH_OFF = new THREE.Vector3(0, 0.12, 0.15);

const rq = new THREE.Quaternion(), pq = new THREE.Quaternion(), wq = new THREE.Quaternion(), hq = new THREE.Quaternion(),
  v = new THREE.Vector3(), sp = new THREE.Vector3(), hp = new THREE.Vector3(), sq = new THREE.Quaternion();

function setWorld(bone, pos, quat) {
  bone.parent.updateMatrixWorld(true);
  bone.parent.getWorldQuaternion(pq);
  if (quat) bone.quaternion.copy(pq.invert().multiply(quat));
  if (pos) bone.position.copy(bone.parent.worldToLocal(pos.clone()));
  bone.updateMatrixWorld(true);
}

// en la pose de enlace (lo llama prepareBody): donde esta la cabeza respecto al personaje
export function prepareMic(c) {
  if (!c.bones[6]) return;
  c.micHead = c.root.worldToLocal(c.bones[6].getWorldPosition(new THREE.Vector3()));
}

// cuanto agarra cada mano en este fotograma (cfg.mic: 'auto' usa lo detectado; 'L' / 'R' / 'stand' lo fuerzan)
export function micGrip(lm, cfg) {
  const m = cfg.mic || 'auto';
  if (m === 'R') return { L: 0, R: 1 };
  if (m === 'L') return { L: 1, R: 0 };
  if (m === 'stand') return { L: 0, R: 0 };
  if (lm.length < MIC_OFF + 2) return { L: 0, R: 0 };
  const L = lm[MIC_OFF], R = lm[MIC_OFF + 1];
  return cfg.swapLR ? { L: R, R: L } : { L, R };
}

// coloca pie y micro. hands: manos con datos de dedos (si una agarra sin datos, se cierran sus dedos alrededor)
export function applyMic(c, lm, cfg, hands = {}) {
  const B = c.bones; if (!B[125] || !B[122] || !B[123] || !B[124] || !c.micHead || cfg.mic === 'none') return;
  c.root.updateMatrixWorld(true);
  c.root.getWorldQuaternion(rq);
  const floor = c.bodyFloor ?? 0, M = c.micHead.clone().add(MOUTH_OFF);
  // pie: vertical, en el suelo bajo el micro, con la altura justa para que el micro quede a la altura de la boca
  const W = q => wq.copy(rq).multiply(q);
  setWorld(B[122], c.root.localToWorld(v.set(M.x, floor, M.z)), W(STAND_Q[122]));
  let len = M.y - floor - 0.096;
  for (let it = 0; it < 2; it++) {
    B[123].position.set(len, 0, 0);
    setWorld(B[123], null, W(STAND_Q[123]));
    B[124].position.copy(c.bindP[124]); setWorld(B[124], null, W(STAND_Q[124]));
    B[125].position.copy(c.bindP[125]); setWorld(B[125], null, W(STAND_Q[125]));
    len += M.y - c.root.worldToLocal(B[125].getWorldPosition(v)).y;
  }
  B[125].getWorldPosition(sp); B[125].getWorldQuaternion(sq);
  // mano: el agarre del juego sobre la palma que lo sujeta
  const g = micGrip(lm, cfg), side = g.R >= g.L ? 'R' : 'L', w = Math.max(g.L, g.R);
  if (w <= 0.001) return;
  const palm = B[side === 'R' ? 14 : 10]; if (!palm) return;
  palm.updateMatrixWorld(true);
  hp.copy(side === 'R' ? GRIP_P : GRIP_PL).applyMatrix4(palm.matrixWorld);
  palm.getWorldQuaternion(hq).multiply(side === 'R' ? GRIP_Q : GRIP_QL);
  setWorld(B[125], sp.lerp(hp, w), sq.slerp(hq, w));
  if (!hands[side]) gripAround(c, side, B[125].getWorldPosition(v), Math.min(1, w * 1.5));
}
