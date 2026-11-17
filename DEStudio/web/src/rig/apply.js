// Vista previa de la captura en el visor (band.js llama a esto despues de posar cada musico) y horneado de la
// captura a claves por hueso para el .ska. Las dos cosas usan las mismas funciones del rig: lo que ves es lo que
// se exporta.
import { bodyAt, bodyNow, capCfg, faceAt, faceNow } from '../capture/capture.js';
import { applyBodyRig, BODY_BONES, BODY_KEYED, IK_T, placeIkTargets } from './bodyRig.js';
import { applyFaceRig, applyHeadRig, FACE_BONES, FB } from './faceRig.js';
import { HAND_BONES } from './handRig.js';
import { MIC_BONES } from './micRig.js';
import { HAND_OFF } from '../capture/channels.js';
import { S } from '../core/state.js';

const resetBones = (c, list) => { for (const i of list) { const b = c.bones[i]; if (b) { b.quaternion.copy(c.bindQ[i]); b.position.copy(c.bindP[i]); } } };

export function applyCapture(role, c) {
  if (!S.proj || !c?.bones) return;
  const bc = capCfg('body');
  if (bc.role === role) {
    const lm = bodyNow();
    if (lm) {
      resetBones(c, BODY_BONES);
      applyBodyRig(c, lm, bc, S.proj.capture?.body?.neutral);
    }
  }
  const fc = capCfg('face');
  if (fc.role === role) {
    const v = faceNow();
    if (v) {
      applyHeadRig(c, v, fc);
      applyFaceRig(c, v, fc);
    }
  }
}

// ---- horneado: n fotogramas a 30/s desde t0 (ms) -> {hueso: {q: [...], p: [...]}} en espacio local ----
function read(c, bones, out, withPos) {
  for (const i of bones) {
    const b = c.bones[i]; if (!b) continue;
    const o = (out[i] ||= { q: [], p: withPos.has(i) ? [] : null });
    o.q.push(+b.quaternion.x.toFixed(6), +b.quaternion.y.toFixed(6), +b.quaternion.z.toFixed(6), +b.quaternion.w.toFixed(6));
    if (o.p) { const bp = c.bindP[i]; o.p.push(+(b.position.x - bp.x).toFixed(6), +(b.position.y - bp.y).toFixed(6), +(b.position.z - bp.z).toFixed(6)); }
  }
}
const ALL = c => c.bones.map((b, i) => i).filter(i => c.bones[i]);
const ZERO_FACE = {};

export function bakeFace(c, t0, n, cfg, withHead) {
  resetBones(c, ALL(c));                         // todo en la pose de enlace: las claves no dependen del cuerpo
  c.root.updateMatrixWorld(true);
  const bones = withHead ? [FB.neck, FB.head, ...FACE_BONES] : FACE_BONES, out = {};
  const pos = new Set(FACE_BONES);
  for (let i = 0; i < n; i++) {
    const v = faceAt(t0 + i * 1000 / 30) || ZERO_FACE;
    resetBones(c, bones);
    c.root.updateMatrixWorld(true);
    if (withHead) applyHeadRig(c, v, cfg);
    applyFaceRig(c, v, cfg);
    read(c, bones, out, pos);
  }
  return out;
}

export function bakeBody(c, t0, n, cfg, neutral) {
  resetBones(c, ALL(c));
  c.root.updateMatrixWorld(true);
  // dedos si el tramo trae manos; pie y micro si es el cantante (con posicion: el pie se coloca y el micro viaja)
  let withHands = false;
  for (let i = 0; i < n && !withHands; i += 10) { const lm = bodyAt(t0 + i * 1000 / 30); withHands = !!lm && lm.length > HAND_OFF.R && (lm[HAND_OFF.L] > 0.3 || lm[HAND_OFF.R] > 0.3); }
  const withMic = cfg.role === 'vocalist' && cfg.mic !== 'none' && !!c.bones[125];
  const keyed = [...BODY_KEYED.filter(i => cfg.head !== false || (i !== 5 && i !== 6)), ...(withHands && cfg.fingers !== false ? HAND_BONES : []), ...(withMic ? MIC_BONES : [])], out = {};
  const pos = new Set([1, ...IK_T, ...MIC_BONES]);
  const reset = [...BODY_BONES, ...IK_T, ...HAND_BONES, ...(withMic ? MIC_BONES : [])];
  let last = null;
  for (let i = 0; i < n; i++) {
    const lm = bodyAt(t0 + i * 1000 / 30) || last; if (!lm) continue;
    last = lm;
    resetBones(c, reset);
    c.root.updateMatrixWorld(true);
    applyBodyRig(c, lm, cfg, neutral);
    placeIkTargets(c);
    read(c, keyed, out, pos);
  }
  return out;
}
