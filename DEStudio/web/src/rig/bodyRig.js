// Puntos 3D del cuerpo (MediaPipe Pose, metros) -> esqueleto de WoR.
// Marco de MediaPipe: x a la derecha de la imagen, y hacia abajo, z hacia el fondo (menos = mas cerca de la camara),
// origen entre las caderas. Con la persona mirando a la camara su izquierda sale a la derecha de la imagen, asi que
// en el marco del personaje (+X su izquierda, +Y arriba, +Z hacia delante) un punto es (x, -y, -z).
// Se orienta cada hueso hacia el punto que le toca (biceps -> codo, antebrazo -> muneca, muslo -> rodilla...),
// de arriba abajo; la columna reparte el giro entre pelvis y pecho, y los pies se apoyan en el suelo.
// Visibilidad (4o valor de cada punto, si lo hay): MediaPipe "inventa" los puntos que no ve (piernas fuera de cuadro,
// brazo tapado por el cuerpo) y moverse con ellos se ve raro. Cada brazo y pierna pesa segun lo visible que es: lo que
// no se ve vuelve suave a la pose de reposo; sin caderas visibles la pelvis solo sigue el giro de los hombros, y los
// pies solo se apoyan en el suelo si se ven.
// Los clips de WoR no animan rodillas ni tobillos: llevan huesos objetivo (93/94 pies, 126/127 manos) y el motor
// cierra brazos y piernas con IK. Al exportar se colocan esos objetivos donde quedan las manos y los pies.
import * as THREE from 'three';
import { LM, LM_SWAP } from '../capture/channels.js';
import { CHAINS, rotateTo } from '../viewport/ik.js';
import { applyHands } from './handRig.js';
import { applyMic, prepareMic } from './micRig.js';

export const BODY_KEYED = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 18, 25, 26, 93, 94, 126, 127];
export const BODY_BONES = Array.from({ length: 29 }, (_, i) => i);   // los que se reponen antes de aplicar
const IK_T = [126, 127, 93, 94];

const rq = new THREE.Quaternion(), pq = new THREE.Quaternion(), wq = new THREE.Quaternion(), q1 = new THREE.Quaternion(),
  m4 = new THREE.Matrix4(), a = new THREE.Vector3(), b = new THREE.Vector3(), cc = new THREE.Vector3(), d = new THREE.Vector3(),
  e = new THREE.Vector3(), f = new THREE.Vector3(), from = new THREE.Vector3(), to = new THREE.Vector3(), q0 = new THREE.Quaternion();
const ramp = (v, a, b) => { const x = Math.min(1, Math.max(0, (v - a) / (b - a))); return x * x * (3 - 2 * x); };
const UP = new THREE.Vector3(0, 1, 0);

export function prepareBody(c) {
  c.root.updateMatrixWorld(true);
  c.root.getWorldQuaternion(rq);
  const inv = rq.clone().invert();
  c.bodyChar = c.bones.map(bn => bn ? inv.clone().multiply(bn.getWorldQuaternion(new THREE.Quaternion())) : null);
  const foot = i => c.bones[i] ? c.root.worldToLocal(c.bones[i].getWorldPosition(new THREE.Vector3())).y : 0;
  c.bodyFloor = Math.min(foot(17), foot(20));
  prepareMic(c);
}

// orientacion a partir de un eje lateral (x) y otro (y o z), en el marco del personaje
function basisXY(x, y, out) {
  a.copy(x).normalize(); b.copy(y).addScaledVector(a, -y.dot(a)).normalize(); cc.crossVectors(a, b);
  return out.setFromRotationMatrix(m4.makeBasis(a, b, cc));
}
function basisXZ(x, z, out) {
  a.copy(x).normalize(); cc.copy(z).addScaledVector(a, -z.dot(a)).normalize(); b.crossVectors(cc, a);
  return out.setFromRotationMatrix(m4.makeBasis(a, b, cc));
}

function setCharQuat(c, i, qChar) {
  const bn = c.bones[i]; if (!bn?.parent || !c.bodyChar[i]) return;
  wq.copy(rq).multiply(qChar).multiply(c.bodyChar[i]);           // mundo deseado
  bn.parent.getWorldQuaternion(pq);
  bn.quaternion.copy(pq.invert().multiply(wq));
  bn.updateMatrixWorld(true);
}
const wpos = (c, i, out) => c.bones[i].getWorldPosition(out);

// lm: [x, y, z, visibilidad] x 33 (o x, y, z). cfg: {swapLR, head, ground}
export function applyBodyRig(c, lm, cfg, neutral) {
  if (!c.bodyChar) prepareBody(c);
  const st = lm.length >= 33 * 4 ? 4 : 3;
  const swap = new Map(); if (cfg.swapLR) for (const [x, y] of LM_SWAP) { swap.set(x, y); swap.set(y, x); }
  const P = i => { const j = swap.get(i) ?? i; return new THREE.Vector3((cfg.swapLR ? -1 : 1) * lm[j * st], -lm[j * st + 1], -lm[j * st + 2]); };
  const V = i => (st === 4 ? lm[(swap.get(i) ?? i) * st + 3] ?? 1 : 1);
  const vis = (...ids) => ramp(Math.min(...ids.map(V)), 0.35, 0.75);   // 0 = no se ve, 1 = se ve bien
  c.root.updateMatrixWorld(true);
  c.root.getWorldQuaternion(rq);
  const toWorld = v => v.applyQuaternion(rq);
  const hipL = P(LM.hipL), hipR = P(LM.hipR), shL = P(LM.shL), shR = P(LM.shR);
  const hipM = hipL.clone().add(hipR).multiplyScalar(0.5), shM = shL.clone().add(shR).multiplyScalar(0.5);
  const up = shM.clone().sub(hipM);
  const Rc = basisXY(shL.clone().sub(shR), up, new THREE.Quaternion());
  // pelvis: con las caderas a la vista, la suya; si no, el giro de los hombros en horizontal (sin inclinarse)
  const wHip = vis(LM.hipL, LM.hipR);
  const Rp = basisXY(hipL.clone().sub(hipR), up, new THREE.Quaternion());
  if (wHip < 1) {
    const side = shL.clone().sub(shR).setY(0);
    const Ry = side.lengthSq() > 1e-6 ? basisXY(side, UP, new THREE.Quaternion()) : new THREE.Quaternion();
    Rp.copy(Ry.slerp(Rp, wHip));
  }
  setCharQuat(c, 1, Rp);
  setCharQuat(c, 2, q1.copy(Rp).slerp(Rc, 1 / 3));
  setCharQuat(c, 3, q1.copy(Rp).slerp(Rc, 2 / 3));
  setCharQuat(c, 4, Rc);
  // cuello y cabeza: orejas (lateral) y nariz (delante); la postura neutra se calibra
  if (cfg.head !== false) {
    const earL = P(LM.earL), earR = P(LM.earR), earM = earL.clone().add(earR).multiplyScalar(0.5);
    const Rh = basisXZ(earL.clone().sub(earR), P(LM.nose).sub(earM), new THREE.Quaternion());
    const off = neutral?.headOff ? new THREE.Quaternion().fromArray(neutral.headOff)
      : new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(1, 0, 0), -0.3);   // la nariz queda bajo las orejas
    Rh.multiply(off);
    setCharQuat(c, 5, q1.copy(Rc).slerp(Rh, 0.5));
    setCharQuat(c, 6, Rh);
  }
  // brazos y piernas: cada hueso hacia su punto
  // orienta el hueso hacia su punto; w < 1 lo deja a medio camino desde la pose que tenia (reposo)
  const aim = (bone, child, p0, p1, w = 1) => {
    const B = c.bones[bone], C = c.bones[child]; if (!B || !C || w <= 0.001) return;
    q0.copy(B.quaternion);
    wpos(c, bone, d); wpos(c, child, e);
    rotateTo(B, from.subVectors(e, d), toWorld(to.subVectors(p1, p0)));
    if (w < 0.999) { B.quaternion.slerpQuaternions(q0, B.quaternion, w); B.updateMatrixWorld(true); }
  };
  // la rodilla solo dobla hacia delante: si el punto la pone hacia atras, se lleva a la recta cadera-tobillo
  const kneeFix = (hp, knee, ank) => {
    const axis = ank.clone().sub(hp), len = axis.lengthSq(); if (len < 1e-6) return knee;
    const onLine = hp.clone().addScaledVector(axis, knee.clone().sub(hp).dot(axis) / len);
    const fwd = knee.clone().sub(onLine);
    const front = new THREE.Vector3(0, 0, 1).applyQuaternion(Rp);   // delante de la pelvis
    return fwd.dot(front) < 0 ? onLine : knee;
  };
  let legsSeen = 0;
  for (const [s, bi, fo, pa, mid, th, kn, an, toe, L] of [
    ['L', 8, 9, 10, 39, 15, 16, 17, 25, { sh: LM.shL, el: LM.elL, wr: LM.wrL, pi: LM.piL, ix: LM.inL, hip: LM.hipL, kn: LM.knL, an: LM.anL, ft: LM.ftL }],
    ['R', 12, 13, 14, 42, 18, 19, 20, 26, { sh: LM.shR, el: LM.elR, wr: LM.wrR, pi: LM.piR, ix: LM.inR, hip: LM.hipR, kn: LM.knR, an: LM.anR, ft: LM.ftR }],
  ]) {
    void s;
    const sh = P(L.sh), el = P(L.el), wr = P(L.wr), hand = P(L.pi).add(P(L.ix)).multiplyScalar(0.5);
    const wArm = vis(L.el), wFore = vis(L.el, L.wr), wHand = vis(L.wr, L.pi, L.ix) * 0.8;   // la mano tiembla mas: menos peso
    aim(bi, fo, sh, el, wArm); aim(fo, pa, el, wr, wFore); aim(pa, mid, wr, hand, wHand);
    const hp = P(L.hip), ank = P(L.an), ft = P(L.ft), knee = kneeFix(hp, P(L.kn), ank);
    const wThigh = vis(L.hip, L.kn), wShin = vis(L.kn, L.an), wFoot = vis(L.an, L.ft);
    aim(th, kn, hp, knee, wThigh); aim(kn, an, knee, ank, wShin); aim(an, toe, ank, ft, wFoot);
    legsSeen = Math.max(legsSeen, wShin);
  }
  // pies al suelo: la pelvis baja (o sube) lo que haga falta para que el pie mas bajo pise donde pisaba
  if (cfg.ground !== false && c.bones[1] && legsSeen > 0.5) {
    const y = i => c.root.worldToLocal(wpos(c, i, f)).y;
    const dy = c.bodyFloor - Math.min(y(17), y(20));
    const pel = c.bones[1];
    pel.getWorldPosition(f); const top = c.root.localToWorld(c.root.worldToLocal(f.clone()).add(new THREE.Vector3(0, dy, 0)));
    pel.position.copy(pel.parent.worldToLocal(top));
    pel.updateMatrixWorld(true);
  }
  // dedos (si llegan las manos) y, en el cantante, pie y micro
  const hands = applyHands(c, lm, cfg);
  if (cfg.role === 'vocalist') applyMic(c, lm, cfg, hands);
}

// Objetivos de IK sobre las manos y los pies que han quedado (para el .ska de clip)
export function placeIkTargets(c) {
  if (!c.ikOff) return;
  CHAINS.forEach((ch, k) => {
    const T = c.bones[ch.t], E = c.bones[ch.c]; if (!T?.parent || !E || !c.ikOff[k]) return;
    E.getWorldPosition(d); E.getWorldQuaternion(wq);
    wq.multiply(q1.copy(c.ikOff[k]).invert());                      // T = E * ikOff^-1
    T.parent.getWorldQuaternion(pq);
    T.quaternion.copy(pq.invert().multiply(wq));
    T.position.copy(T.parent.worldToLocal(d));
    T.updateMatrixWorld(true);
  });
}
export { IK_T };

// calibracion de pie y mirando al frente: la cabeza neutra pasa a ser la del pecho
export function bodyNeutral(lm) {
  const st = lm.length >= 33 * 4 ? 4 : 3, P = i => new THREE.Vector3(lm[i * st], -lm[i * st + 1], -lm[i * st + 2]);
  const up = P(LM.shL).add(P(LM.shR)).multiplyScalar(0.5).sub(P(LM.hipL).add(P(LM.hipR)).multiplyScalar(0.5));
  const Rc = basisXY(P(LM.shL).sub(P(LM.shR)), up, new THREE.Quaternion());
  const earM = P(LM.earL).add(P(LM.earR)).multiplyScalar(0.5);
  const Rh = basisXZ(P(LM.earL).sub(P(LM.earR)), P(LM.nose).sub(earM), new THREE.Quaternion());
  return { headOff: Rh.invert().multiply(Rc).toArray() };
}
