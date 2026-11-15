// Blendshapes de la captura -> rig facial de los personajes de WoR.
// El rig (igual en los 17 modelos de la banda; nombres de las QBKeys del juego) son 30 huesos hijos de b006 Bone_Head:
// mandibula, ojos, parpados, cejas, mejillas, labios, nariz y lengua. Las animaciones faciales del juego los
// mueven asi (medido en 41 animaciones de 3 canciones, percentil 95): labio inferior ~35 mm (va con la mandibula,
// pero cuelga de la cabeza), comisuras ~17 mm, cejas ~12 mm, mejillas ~12 mm, mandibula ~23 grados, parpados ~38
// grados, ojos ~23 grados. Las magnitudes de abajo estan en ese rango.
// Todo se calcula en el marco de la cabeza EN LA POSE DE ENLACE (+X izquierda del personaje, +Y arriba, +Z delante)
// y se pasa al espacio local de cada hueso: el resultado no depende de la animacion del cuerpo, y es exactamente lo
// que se exporta al .ska facial.
import * as THREE from 'three';

export const FB = {
  neck: 5, head: 6, jaw: 63, eyeL: 64, eyeR: 65, lidUpL: 66, lidUpR: 67, mouthL: 68, mouthR: 69, lipLoMid: 70,
  lipUpMid: 71, browL: 72, browR: 73, browMid: 74, browMidL: 75, browMidR: 76, cheekL: 77, cheekR: 78, cheekUpL: 79,
  cheekUpR: 80, lidLoL: 81, lidLoR: 82, lipLoCornerL: 83, lipLoCornerR: 84, lipLoL: 85, lipLoR: 86,
  lipUpCornerL: 87, lipUpCornerR: 88, lipUpL: 89, lipUpR: 90, nostrils: 91, tongue: 92,
};
export const FACE_BONES = Array.from({ length: 30 }, (_, i) => 63 + i);

// huesos que arrastra la mandibula (peso 1 = entero)
const JAW_FOLLOW = [[FB.lipLoMid, 1], [FB.lipLoL, 1], [FB.lipLoR, 1], [FB.lipLoCornerL, 0.85], [FB.lipLoCornerR, 0.85],
  [FB.mouthL, 0.5], [FB.mouthR, 0.5], [FB.tongue, 0.8]];

// ---- tabla de gestos: [canal, hueso, 't' (mm) | 'r' (grados), x, y, z, grupo de intensidad] ----
const T = [];
const add = (ch, bones, kind, x, y, z, g) => { for (const b of [].concat(bones)) T.push([ch, b, kind, x, y, z, g]); };
const LIP_UP = [FB.lipUpMid, FB.lipUpL, FB.lipUpR], LIP_LO = [FB.lipLoMid, FB.lipLoL, FB.lipLoR];
for (const [S, s, B] of [
  ['Left', 1, { mouth: FB.mouthL, upC: FB.lipUpCornerL, loC: FB.lipLoCornerL, up: FB.lipUpL, lo: FB.lipLoL, cheek: FB.cheekL,
    cheekUp: FB.cheekUpL, lidUp: FB.lidUpL, lidLo: FB.lidLoL, brow: FB.browL, browMid: FB.browMidL, eye: FB.eyeL }],
  ['Right', -1, { mouth: FB.mouthR, upC: FB.lipUpCornerR, loC: FB.lipLoCornerR, up: FB.lipUpR, lo: FB.lipLoR, cheek: FB.cheekR,
    cheekUp: FB.cheekUpR, lidUp: FB.lidUpR, lidLo: FB.lidLoR, brow: FB.browR, browMid: FB.browMidR, eye: FB.eyeR }],
]) {
  const corners = [B.mouth, B.upC, B.loC];
  add('mouthSmile' + S, corners, 't', s * 11, 9, -4, 'mouth');
  add('mouthSmile' + S, [B.up, B.lo], 't', s * 3, 2.5, -1, 'mouth');
  add('mouthSmile' + S, B.cheekUp, 't', s * 2, 7, 2, 'mouth');
  add('mouthSmile' + S, B.cheek, 't', s * 3, 5, 2, 'mouth');
  add('mouthSmile' + S, B.lidLo, 'r', -8, 0, 0, 'eyes');
  add('mouthFrown' + S, corners, 't', s * 2, -9, 0, 'mouth');
  add('mouthFrown' + S, B.lo, 't', 0, -2, 0, 'mouth');
  add('mouthDimple' + S, corners, 't', s * 5, 0, -5, 'mouth');
  add('mouthStretch' + S, corners, 't', s * 12, -4, -2, 'mouth');
  add('mouthStretch' + S, B.lo, 't', s * 3, -3, 0, 'mouth');
  add('mouthStretch' + S, B.up, 't', s * 2, 0, 0, 'mouth');
  add('mouthPress' + S, B.up, 't', 0, -2, -1, 'mouth');
  add('mouthPress' + S, B.lo, 't', 0, 3, -1, 'mouth');
  add('mouthPress' + S, [B.upC, B.loC], 't', 0, 0, -2, 'mouth');
  add('mouthLowerDown' + S, B.lo, 't', 0, -10, 1, 'mouth');
  add('mouthLowerDown' + S, B.loC, 't', 0, -6, 0, 'mouth');
  add('mouthLowerDown' + S, FB.lipLoMid, 't', 0, -5, 0, 'mouth');
  add('mouthUpperUp' + S, B.up, 't', 0, 8, 1, 'mouth');
  add('mouthUpperUp' + S, B.upC, 't', 0, 5, 0, 'mouth');
  add('mouthUpperUp' + S, FB.lipUpMid, 't', 0, 4, 0, 'mouth');
  add('cheekSquint' + S, B.cheekUp, 't', 0, 6, 1, 'mouth');
  add('cheekSquint' + S, B.lidLo, 'r', -10, 0, 0, 'eyes');
  add('noseSneer' + S, FB.nostrils, 't', 0, 3, 0, 'mouth');
  add('noseSneer' + S, B.cheekUp, 't', 0, 4, 0, 'mouth');
  add('noseSneer' + S, B.upC, 't', 0, 3, 0, 'mouth');
  add('noseSneer' + S, B.browMid, 't', -s * 2, -4, 0, 'brow');
  add('browDown' + S, [B.brow, B.browMid], 't', -s * 3, -8, 1, 'brow');
  add('browDown' + S, FB.browMid, 't', 0, -4, 0, 'brow');
  add('browOuterUp' + S, B.brow, 't', 0, 13, 0, 'brow');
  add('browOuterUp' + S, B.browMid, 't', 0, 5, 0, 'brow');
  add('eyeBlink' + S, B.lidUp, 'r', 42, 0, 0, 'lids');
  add('eyeBlink' + S, B.lidLo, 'r', -8, 0, 0, 'lids');
  add('eyeSquint' + S, B.lidUp, 'r', 10, 0, 0, 'lids');
  add('eyeSquint' + S, B.lidLo, 'r', -15, 0, 0, 'lids');
  add('eyeWide' + S, B.lidUp, 'r', -14, 0, 0, 'lids');
  add('eyeLookUp' + S, B.eye, 'r', -20, 0, 0, 'eyes');
  add('eyeLookUp' + S, B.lidUp, 'r', -8, 0, 0, 'eyes');
  add('eyeLookDown' + S, B.eye, 'r', 20, 0, 0, 'eyes');
  add('eyeLookDown' + S, B.lidUp, 'r', 12, 0, 0, 'eyes');
  add('eyeLookOut' + S, B.eye, 'r', 0, s * 25, 0, 'eyes');
  add('eyeLookIn' + S, B.eye, 'r', 0, -s * 25, 0, 'eyes');
}
add('browInnerUp', FB.browMid, 't', 0, 13, 0, 'brow');
add('browInnerUp', [FB.browMidL, FB.browMidR], 't', 0, 11, 0, 'brow');
add('browInnerUp', [FB.browL, FB.browR], 't', 0, 3, 0, 'brow');
add('cheekPuff', FB.cheekL, 't', 10, 0, 3, 'mouth');
add('cheekPuff', FB.cheekR, 't', -10, 0, 3, 'mouth');
add('cheekPuff', FB.cheekUpL, 't', 5, 0, 2, 'mouth');
add('cheekPuff', FB.cheekUpR, 't', -5, 0, 2, 'mouth');
add('mouthFunnel', LIP_UP, 't', 0, 3, 7, 'mouth');
add('mouthFunnel', LIP_LO, 't', 0, -3, 7, 'mouth');
add('mouthFunnel', [FB.mouthL, FB.lipUpCornerL, FB.lipLoCornerL], 't', -5, 0, 3, 'mouth');
add('mouthFunnel', [FB.mouthR, FB.lipUpCornerR, FB.lipLoCornerR], 't', 5, 0, 3, 'mouth');
add('mouthPucker', [FB.mouthL, FB.lipUpCornerL, FB.lipLoCornerL], 't', -10, 0, 4, 'mouth');
add('mouthPucker', [FB.mouthR, FB.lipUpCornerR, FB.lipLoCornerR], 't', 10, 0, 4, 'mouth');
add('mouthPucker', [...LIP_UP, ...LIP_LO], 't', 0, 0, 8, 'mouth');
const MOUTH_ALL = [FB.mouthL, FB.mouthR, ...LIP_UP, ...LIP_LO, FB.lipUpCornerL, FB.lipUpCornerR, FB.lipLoCornerL, FB.lipLoCornerR];
add('mouthLeft', MOUTH_ALL, 't', 9, 0, 0, 'mouth');
add('mouthRight', MOUTH_ALL, 't', -9, 0, 0, 'mouth');
add('mouthRollLower', LIP_LO, 't', 0, 3, -6, 'mouth');
add('mouthRollUpper', LIP_UP, 't', 0, -3, -6, 'mouth');
add('mouthShrugLower', LIP_LO, 't', 0, 5, 2, 'mouth');
add('mouthShrugUpper', LIP_UP, 't', 0, 4, 1, 'mouth');
add('mouthClose', [...LIP_LO, FB.lipLoCornerL, FB.lipLoCornerR], 't', 0, 10, 0, 'mouth');
add('jawForward', [FB.jaw, ...JAW_FOLLOW.map(f => f[0])], 't', 0, 0, 7, 'mouth');
export const FACE_TABLE = T;

const D = THREE.MathUtils.degToRad;
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));

// ---- contencion: que se vea la expresion sin deformar la cara ----
// MediaPipe dispara a la vez varios canales para un solo gesto (al sonreir: sonrisa + estirar + hoyuelo + labio arriba
// y abajo) y sumarlos tal cual llevaba las comisuras a ~30 mm. Tres pasos:
//  1) curva por canal: zona muerta pequena (quita el temblor de la cara en reposo) y compresion de los valores altos
//     (lo pequeno se ve igual; lo grande se queda en ~70 %)
//  2) canales que se pisan: lo que ya hace la sonrisa o la mandibula no se suma otra vez
//  3) tope suave por hueso con las amplitudes medidas en las caras del juego (percentil 95)
const PAIRS = ['mouthSmile', 'mouthFrown', 'mouthDimple', 'mouthStretch', 'mouthPress', 'mouthLowerDown', 'mouthUpperUp', 'cheekSquint', 'noseSneer'];
export function shapeFace(v, cfg = {}) {
  const k = clamp(cfg.soft ?? 0.6, 0, 1), sym = clamp(cfg.sym ?? 0.4, 0, 1), dz = 0.04 + 0.06 * k, o = {};
  const curve = x => { x = clamp((x - dz) / (1 - dz), 0, 1); return x * (1 - 0.4 * k * x); };
  for (const key in v) o[key] = typeof v[key] === 'number' && !(key === 'yaw' || key === 'pitch' || key === 'roll') ? curve(v[key]) : v[key];
  // boca simetrica en parte (la deteccion de un lado suele bailar mas que la del otro y tuerce la boca)
  for (const P of PAIRS) {
    const l = o[P + 'Left'] || 0, r = o[P + 'Right'] || 0, m = (l + r) / 2;
    o[P + 'Left'] = l + (m - l) * sym; o[P + 'Right'] = r + (m - r) * sym;
  }
  const g = n => o[n] || 0;
  for (const S of ['Left', 'Right']) {
    const sm = g('mouthSmile' + S);
    o['mouthStretch' + S] = g('mouthStretch' + S) * (1 - 0.8 * sm);       // sonreir ya estira
    o['mouthDimple' + S] = g('mouthDimple' + S) * (1 - 0.7 * sm);
    o['mouthUpperUp' + S] = g('mouthUpperUp' + S) * (1 - 0.6 * sm);
    o['cheekSquint' + S] = g('cheekSquint' + S) * (1 - 0.5 * sm);
    o['mouthLowerDown' + S] = g('mouthLowerDown' + S) * (1 - 0.85 * clamp(g('jawOpen') * 1.5, 0, 1)) * (1 - 0.5 * sm);  // la mandibula ya baja el labio
    o['mouthFrown' + S] = g('mouthFrown' + S) * (1 - sm);                 // no a la vez que la sonrisa
  }
  o.mouthClose = g('mouthClose') * (1 - clamp(g('jawOpen') * 2, 0, 1));
  o.mouthPucker = g('mouthPucker') * (1 - 0.5 * g('mouthFunnel'));
  return o;
}
// tope por hueso (mm) y para los giros (grados)
const LIM_T = new Map([
  ...[FB.mouthL, FB.mouthR, FB.lipUpCornerL, FB.lipUpCornerR, FB.lipLoCornerL, FB.lipLoCornerR].map(b => [b, 13]),
  ...[FB.lipUpMid, FB.lipUpL, FB.lipUpR].map(b => [b, 7]), ...[FB.lipLoMid, FB.lipLoL, FB.lipLoR].map(b => [b, 9]),
  ...[FB.cheekL, FB.cheekR, FB.cheekUpL, FB.cheekUpR].map(b => [b, 8]),
  ...[FB.browL, FB.browR, FB.browMid, FB.browMidL, FB.browMidR].map(b => [b, 11]), [FB.nostrils, 3], [FB.jaw, 6], [FB.tongue, 6],
]);
const LIM_R = new Map([[FB.lidUpL, 40], [FB.lidUpR, 40], [FB.lidLoL, 14], [FB.lidLoR, 14], [FB.eyeL, 22], [FB.eyeR, 22]]);
const softLen = (a, L) => { const n = Math.hypot(a[0], a[1], a[2]); if (n < 1e-6) return; const f = L * Math.tanh(n / L) / n; a[0] *= f; a[1] *= f; a[2] *= f; };

// En la pose de enlace: orientacion de la cabeza respecto al personaje y escala (constantes del modelo)
export function prepareFace(c) {
  const head = c.bones[FB.head]; if (!head || !c.root) return;
  c.root.updateMatrixWorld(true);
  const rq = c.root.getWorldQuaternion(new THREE.Quaternion()), hq = head.getWorldQuaternion(new THREE.Quaternion());
  const rs = c.root.getWorldScale(new THREE.Vector3()), hs = head.getWorldScale(new THREE.Vector3());
  c.faceH = rq.clone().invert().multiply(hq);           // cabeza en el marco del personaje
  c.faceHi = c.faceH.clone().invert();
  c.faceS = new THREE.Vector3(hs.x / rs.x, hs.y / rs.y, hs.z / rs.z);
}

const acc = new Map(), rot = new Map();
const v3 = new THREE.Vector3(), qa = new THREE.Quaternion(), qb = new THREE.Quaternion(), eu = new THREE.Euler(),
  piv = new THREE.Vector3(), qj = new THREE.Quaternion(), qk = new THREE.Quaternion(), ID = new THREE.Quaternion();

// char -> local de la cabeza
const toHeadVec = (c, x, y, z) => v3.set(x, y, z).applyQuaternion(c.faceHi).divide(c.faceS);
const toHeadQuat = (c, qChar, out) => out.copy(c.faceHi).multiply(qChar).multiply(qb.copy(c.faceH));

// v: valores por nombre (FACE_KEYS), cfg: intensidades. Mueve los 30 huesos de la cara desde su pose actual.
export function applyFaceRig(c, v, cfg) {
  if (!c.faceH) prepareFace(c);
  if (!c.faceH) return;
  const B = c.bones, g = { mouth: cfg.mouth ?? 1, brow: cfg.brow ?? 1, eyes: cfg.eyes ?? 1, lids: cfg.lids ?? 1 };
  const sw = cfg.swapLR, sv = shapeFace(v, cfg), soft = (cfg.soft ?? 0.6) > 0;
  const val = k => {
    if (sw) k = k.endsWith('Left') ? k.slice(0, -4) + 'Right' : k.endsWith('Right') ? k.slice(0, -5) + 'Left' : k;
    const x = sv[k]; return typeof x === 'number' ? x : 0;
  };
  acc.clear(); rot.clear();
  for (const [ch, b, kind, x, y, z, grp] of FACE_TABLE) {
    const w = val(ch) * g[grp]; if (!w) continue;
    const m = kind === 't' ? acc : rot;
    const a = m.get(b) || [0, 0, 0]; a[0] += x * w; a[1] += y * w; a[2] += z * w; m.set(b, a);
  }
  // mandibula: abrir (eje lateral) y de lado; el labio inferior, las comisuras y la lengua giran con ella
  const jb = B[FB.jaw];
  // apertura: curva suave hasta el maximo (sin pasarse aunque se suba "Mandibula")
  const jo = val('jawOpen') * (cfg.jaw ?? 1), open = (soft ? Math.tanh(jo * 1.3) / Math.tanh(1.3) : clamp(jo, 0, 1.15)) * (cfg.jawMax ?? 20), side = (val('jawLeft') - val('jawRight')) * 8;
  if (jb && (open > 0.05 || Math.abs(side) > 0.05)) {
    toHeadQuat(c, qa.setFromEuler(eu.set(D(open), D(side), 0, 'YXZ')), qj);
    piv.copy(jb.position);
    jb.quaternion.premultiply(qj);
    for (const [i, k] of JAW_FOLLOW) {
      const b = B[i]; if (!b) continue;
      qk.copy(ID).slerp(qj, k);
      b.position.sub(piv).applyQuaternion(qk).add(piv);
    }
  }
  if (soft) {
    for (const [b, a] of acc) if (LIM_T.has(b)) softLen(a, LIM_T.get(b));
    for (const [b, a] of rot) if (LIM_R.has(b)) { const L = LIM_R.get(b); for (let i = 0; i < 3; i++) a[i] = L * Math.tanh(a[i] / L); }
  }
  for (const [b, a] of acc) { const bone = B[b]; if (bone) bone.position.add(toHeadVec(c, a[0] / 1000, a[1] / 1000, a[2] / 1000)); }
  for (const [b, a] of rot) {
    const bone = B[b]; if (!bone) continue;
    toHeadQuat(c, qa.setFromEuler(eu.set(D(a[0]), D(a[1]), D(a[2]), 'YXZ')), qj);
    bone.quaternion.premultiply(qj);
  }
}

// Cabeza y cuello (40 / 60 %) en el marco del personaje
const pw = new THREE.Quaternion(), rq = new THREE.Quaternion(), dq = new THREE.Quaternion(), tq = new THREE.Quaternion();
export function rotateInChar(bone, root, qChar) {
  if (!bone?.parent) return;
  root.getWorldQuaternion(rq);
  bone.parent.getWorldQuaternion(pw);
  dq.copy(rq).multiply(qChar).multiply(tq.copy(rq).invert());
  dq.premultiply(tq.copy(pw).invert()).multiply(pw);
  bone.quaternion.premultiply(dq);
  bone.updateMatrixWorld(true);
}
export function applyHeadRig(c, v, cfg) {
  if (typeof v.yaw !== 'number') return;
  const k = cfg.head ?? 0.8, m = cfg.mirrorHead === false ? 1 : -1;
  const yaw = D(v.yaw) * k * m, pitch = D(v.pitch) * k * (cfg.invPitch ? -1 : 1), roll = D(v.roll) * k * m;
  c.root.updateMatrixWorld(true);
  rotateInChar(c.bones[FB.neck], c.root, qa.setFromEuler(eu.set(pitch * 0.4, yaw * 0.4, roll * 0.4, 'YXZ')));
  rotateInChar(c.bones[FB.head], c.root, qa.setFromEuler(eu.set(pitch * 0.6, yaw * 0.6, roll * 0.6, 'YXZ')));
}
