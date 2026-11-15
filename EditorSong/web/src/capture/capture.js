// Captura con el movil: estado en vivo, calibracion y grabacion por tramos.
// Modos de la app: 'body' (cuerpo entero, sin cara), 'head' (cabeza y rostro), 'face' (solo rostro, mas detalle).
//   movil -> editor  {type:'cap', mode, ok, bs:{blendshape: 0..1}, q:[x,y,z,w] (cabeza), lm:[x,y,z,v]*33 (cuerpo)}
//                    {type:'capmode', mode|null}  {type:'caprec', on}  {type:'capneutral'}
//   editor -> movil  dentro de 'state': capRec
// Proyecto: P.capture = {face: {frames:[{t, v:[FACE_KEYS]}], neutral, cfg}, body: {frames:[{t, p:[x,y,z,vis]*33}], neutral, cfg}}
// (las tomas de cuerpo antiguas guardan [x,y,z]*33; el rig acepta las dos)
// GRABAR: la cancion suena desde el cursor; cada ~33 ms de cancion se guarda un fotograma. Una toma nueva solo
// sustituye el tramo que cubre (se puede rehacer una frase sin perder el resto).
import * as THREE from 'three';
import { changed, emit, pushUndo, S } from '../core/state.js';
import { setPlaying } from '../ui/playback.js';
import { onFrame } from '../viewport/scene.js';
import { BLEND, BODY_FULL, FACE_INDEX, FACE_KEYS, HAND_OFF, legacyFrame, LM_COUNT, MIC_OFF } from './channels.js';
import { bodyNeutral } from '../rig/bodyRig.js';
import { clearStream, delayOf, lerpArray, lerpObject, liveStream, pushSample, sampleAt } from './live.js';

// muestras en vivo interpoladas (ver live.js): CAP.live y CAP.liveBody se recalculan en cada fotograma
const FACE_LIVE = liveStream(lerpObject), BODY_LIVE = liveStream(lerpArray);

export const FACE_CFG = { role: 'vocalist', jaw: 1, jawMax: 20, soft: 0.6, sym: 0.4, mouth: 1, brow: 1, eyes: 1, lids: 1, head: 0.8,
  swapLR: false, mirrorHead: true, invPitch: false, exportHead: true };
export const BODY_CFG = { role: 'vocalist', head: true, ground: true, swapLR: false, fingers: true, mic: 'auto' };

export const CAP = { mode: null, ok: false, live: null, liveBody: null, raw: null, q: null, rec: null, take: null, lastT: -1, notify: null,
  src: null, rate: 0 };   // src: 'phone' | 'pc'; rate: muestras por segundo que llegan
const kindOf = mode => (mode === 'body' ? 'body' : mode ? 'face' : null);
export const capKind = () => kindOf(CAP.mode);

const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
export function capDoc(kind) {
  if (!S.proj) return null;
  const P = S.proj, C = (P.capture ||= {});
  if (!C.face) {
    C.face = { frames: [], neutral: null, cfg: { ...FACE_CFG } };
    if (P.face?.frames?.length) {   // tomas de la version anterior
      C.face.frames = P.face.frames.map(f => ({ t: f.t, v: legacyFrame(f.v) }));
      if (P.face.cfg?.role) C.face.cfg.role = P.face.cfg.role;
    }
    delete P.face;
  }
  C.body ||= { frames: [], neutral: null, cfg: { ...BODY_CFG } };
  return kind ? C[kind] : C;
}
export const capCfg = kind => ({ ...(kind === 'body' ? BODY_CFG : FACE_CFG), ...(S.proj?.capture?.[kind]?.cfg || {}) });
export function setCapCfg(kind, k, v) { const d = capDoc(kind); d.cfg = { ...capCfg(kind), [k]: v }; changed(); }

let uiAt = 0;
const ui = () => { const now = performance.now(); if (now - uiAt > 66) { uiAt = now; emit('capture'); } };

// ---- cara: reposo calibrado ----
const _q = new THREE.Quaternion(), _n = new THREE.Quaternion(), _e = new THREE.Euler();
function faceValues(bs, q) {
  const n = capDoc('face')?.neutral, o = {};
  for (const k of BLEND) {
    const x = +bs[k] || 0, b = n?.bs?.[k] || 0;
    o[k] = clamp((x - b) / Math.max(0.2, 1 - b), 0, 1);       // lo que sobra sobre tu cara en reposo
  }
  if (q && CAP.mode === 'head') {
    _n.fromArray(n?.q || [0, 0, 0, 1]).invert();
    _q.fromArray(q).premultiply(_n);
    _e.setFromQuaternion(_q, 'YXZ');
    o.yaw = clamp(THREE.MathUtils.radToDeg(_e.y), -70, 70);
    o.pitch = clamp(THREE.MathUtils.radToDeg(_e.x), -60, 60);
    o.roll = clamp(THREE.MathUtils.radToDeg(_e.z), -60, 60);
  }
  return o;
}

export function calibrateNeutral() {
  if (!S.proj) return false;
  if (capKind() === 'body') {
    if (!CAP.liveBody) return false;
    capDoc('body').neutral = bodyNeutral(CAP.liveBody);
  } else {
    if (!CAP.raw) return false;
    capDoc('face').neutral = { bs: { ...CAP.raw }, q: CAP.q ? CAP.q.slice() : [0, 0, 0, 1] };
  }
  changed(); emit('capture');
  return true;
}

// ---- cuerpo + manos + micro en un solo vector (ver channels.js) ----
const lastHand = { L: null, R: null };
export const MIC = { L: 0, R: 0, holder: null, cand: null, candT: 0, relT: 0, at: 0 };
// mano abierta ~1.9, puno ~1.1: distancia muneca-punta / muneca-nudillo de indice, corazon, anular y menique
function handOpen(h) {
  const d = (i, j) => Math.hypot(h[i * 3] - h[j * 3], h[i * 3 + 1] - h[j * 3 + 1], h[i * 3 + 2] - h[j * 3 + 2]);
  let r = 0; for (const [mcp, tip] of [[5, 8], [9, 12], [13, 16], [17, 20]]) r += d(0, tip) / Math.max(1e-3, d(0, mcp));
  return r / 4;
}
// Micro en la mano: una mano CERRADA junto a la boca un cuarto de segundo lo coge; ABIERTA unas decimas lo suelta
// (sin datos de dedos: muneca muy cerca de la boca lo coge, lejos mas de 1.5 s lo suelta). Paso suave de 0 a 1.
function detectMic(lm, hands, now) {
  const dt = MIC.at ? Math.min(0.2, (now - MIC.at) / 1000) : 0.033; MIC.at = now;
  const P = i => [lm[i * 4], lm[i * 4 + 1], lm[i * 4 + 2]], nose = P(0);
  const st = {};
  for (const [side, wi] of [['L', 15], ['R', 16]]) {
    const w = P(wi), r = hands[side] ? handOpen(hands[side]) : null;
    st[side] = { d: Math.hypot(w[0] - nose[0], w[1] - nose[1], w[2] - nose[2]), has: r !== null, closed: r !== null && r < 1.45, open: r !== null && r > 1.7 };
  }
  if (!MIC.holder) {
    const cand = ['R', 'L'].find(sd => (st[sd].has ? st[sd].closed && st[sd].d < 0.4 : st[sd].d < 0.22)) || null;
    if (cand && cand === MIC.cand) { if (now - MIC.candT > (st[cand].has ? 250 : 400)) { MIC.holder = cand; MIC.relT = 0; } }
    else { MIC.cand = cand; MIC.candT = now; }
  } else {
    const x = st[MIC.holder], rel = x.has ? x.open : x.d > 0.7;
    if (!rel) MIC.relT = 0;
    else if (!MIC.relT) MIC.relT = now;
    else if (now - MIC.relT > (x.has ? 300 : 1500)) { MIC.holder = null; MIC.cand = null; MIC.relT = 0; }
  }
  const k = 1 - Math.exp(-dt / 0.12);
  for (const sd of ['L', 'R']) MIC[sd] += ((MIC.holder === sd ? 1 : 0) - MIC[sd]) * k;
}
function composeBody(m, now = performance.now()) {
  const st = m.lm.length >= LM_COUNT * 4 ? 4 : 3, out = new Array(BODY_FULL).fill(0);
  for (let i = 0; i < LM_COUNT; i++) {
    out[i * 4] = m.lm[i * st]; out[i * 4 + 1] = m.lm[i * st + 1]; out[i * 4 + 2] = m.lm[i * st + 2]; out[i * 4 + 3] = st === 4 ? m.lm[i * st + 3] : 1;
  }
  const hands = { L: null, R: null };
  for (const side of ['L', 'R']) {
    const h = m.hands?.[side], o = HAND_OFF[side];
    if (Array.isArray(h) && h.length >= 63) { lastHand[side] = h; hands[side] = h; out[o] = 1; }
    const src = hands[side] || lastHand[side];          // sin mano: la ultima conocida con presencia 0 (interpola sin saltos)
    if (src) for (let i = 0; i < 63; i++) out[o + 1 + i] = src[i];
  }
  detectMic(out, hands, now);
  out[MIC_OFF] = MIC.L; out[MIC_OFF + 1] = MIC.R;
  return out;
}

export function onCapMsg(m) {
  // con la camara del PC encendida manda ella: lo que llegue del telefono se ignora
  if (CAP.pcOn && m.src !== 'pc') return;
  if (m.type === 'capmode') {
    CAP.mode = m.mode || null; CAP.ok = false;
    clearStream(FACE_LIVE); clearStream(BODY_LIVE); CAP.live = CAP.liveBody = null;
    lastHand.L = lastHand.R = null; Object.assign(MIC, { L: 0, R: 0, holder: null, cand: null, candT: 0, relT: 0, at: 0 });
    if (!CAP.mode && CAP.rec) stopRec();
    emit('capture'); return;
  }
  if (m.type === 'capneutral') { calibrateNeutral(); return; }
  if (m.type === 'caprec') { if (m.on) startRec(); else stopRec(); return; }
  if (m.type !== 'cap') return;
  if (m.mode && m.mode !== CAP.mode) CAP.mode = m.mode;
  if (m.src) CAP.src = m.src;
  if (!m.ok) { if (CAP.ok) { CAP.ok = false; emit('capture'); } return; }
  if (CAP.mode === 'body') {
    if (!Array.isArray(m.lm) || m.lm.length < LM_COUNT * 3) return;
    const full = composeBody(m);
    pushSample(BODY_LIVE, full);
    CAP.liveBody ||= full;
  } else {
    if (!m.bs) return;
    CAP.raw = m.bs; CAP.q = m.q || null;
    pushSample(FACE_LIVE, faceValues(m.bs, m.q));
    CAP.live ||= FACE_LIVE.buf.at(-1).v;
  }
  CAP.rate = 1000 / (CAP.mode === 'body' ? BODY_LIVE : FACE_LIVE).dt;
  CAP.ok = true;
  ui();
}

// ---- grabacion ----
export function startRec() {
  const kind = capKind();
  if (!S.proj || CAP.rec || !kind) return;
  capDoc(); pushUndo();
  CAP.rec = kind; CAP.take = []; CAP.lastT = -1;
  setPlaying(true);
  emit('capture'); CAP.notify?.();
}

export function stopRec() {
  const kind = CAP.rec; if (!kind) return;
  CAP.rec = null;
  const take = CAP.take || []; CAP.take = null;
  setPlaying(false);
  if (take.length > 1 && S.proj) {
    const d = capDoc(kind), t0 = take[0].t, t1 = take.at(-1).t;
    d.frames = d.frames.filter(f => f.t < t0 || f.t > t1).concat(take).sort((a, b) => a.t - b.t);
    changed();
  }
  emit('capture'); CAP.notify?.();
}

onFrame(() => {
  // valor en vivo de este fotograma (interpolado entre las muestras que rodean "ahora - retraso")
  const now = performance.now();
  if (CAP.ok) {
    const f = sampleAt(FACE_LIVE, now), b = sampleAt(BODY_LIVE, now);
    if (f) CAP.live = f;
    if (b) CAP.liveBody = b;
  }
  if (!CAP.rec) return;
  if (!S.playing) { stopRec(); return; }      // pausa desde el PC = fin de la toma
  // lo que se ve ahora ocurrio hace "retraso" ms: se graba en ese instante de la cancion. Se rellenan TODOS los huecos
  // de 33 ms desde el ultimo fotograma (preguntando al flujo interpolado por cada instante), asi la toma sale a 30/s
  // aunque el editor pinte a menos (escenario pesado, PC ocupado)
  if (!CAP.ok) return;
  const stream = CAP.rec === 'body' ? BODY_LIVE : FACE_LIVE;
  const tNow = Math.round(S.t - delayOf(stream));
  let t = CAP.lastT < 0 || tNow - CAP.lastT > 400 ? tNow : CAP.lastT + 33;
  for (; t <= tNow; t += 33) {
    const v = sampleAt(stream, now - (tNow - t)); if (!v) continue;
    if (CAP.rec === 'body') {
      // x, y, z y visibilidad (el rig deja en reposo lo que no se ve: piernas fuera de cuadro, etc.)
      const st = v.length >= LM_COUNT * 4 ? 4 : 3, p = [];
      for (let i = 0; i < LM_COUNT; i++) p.push(+v[i * st].toFixed(3), +v[i * st + 1].toFixed(3), +v[i * st + 2].toFixed(3), st === 4 ? +v[i * st + 3].toFixed(2) : 1);
      for (let i = LM_COUNT * 4; i < v.length; i++) p.push(+v[i].toFixed(3));     // manos y micro
      CAP.take.push({ t, p });
    } else {
      CAP.take.push({ t, v: FACE_KEYS.map(k => (typeof v[k] === 'number' ? +v[k].toFixed(3) : null)) });
    }
    CAP.lastT = t;
  }
  if (CAP.take.length % 10 === 0) emit('capture');
});

// tramos grabados (huecos de mas de 0.4 s separan tomas)
export function takes(kind) {
  const f = S.proj?.capture?.[kind]?.frames || [], out = [];
  for (let i = 0; i < f.length; i++) {
    if (!out.length || f[i].t - f[i - 1].t > 400) out.push({ t0: f[i].t, t1: f[i].t, i0: i, i1: i });
    else { out.at(-1).t1 = f[i].t; out.at(-1).i1 = i; }
  }
  return out;
}
export function deleteTake(kind, k) {
  const tk = takes(kind)[k]; if (!tk) return;
  pushUndo(); S.proj.capture[kind].frames.splice(tk.i0, tk.i1 - tk.i0 + 1); changed(); emit('capture');
}

// valor grabado en t (interpolado; nada fuera de los tramos)
function bracket(f, t) {
  if (!f?.length || t < f[0].t || t > f.at(-1).t) return null;
  let lo = 0, hi = f.length - 1;
  while (hi - lo > 1) { const m = (lo + hi) >> 1; if (f[m].t <= t) lo = m; else hi = m; }
  const a = f[lo], b = f[hi];
  if (b.t - a.t > 400) return null;
  return [a, b, b.t === a.t ? 0 : (t - a.t) / (b.t - a.t)];
}
export function faceAt(t) {
  const br = bracket(S.proj?.capture?.face?.frames, t); if (!br) return null;
  const [a, b, k] = br, o = {};
  FACE_KEYS.forEach((key, i) => {
    const x = a.v[i], y = b.v[i];
    o[key] = x === null || x === undefined ? (FACE_INDEX[key] >= BLEND.length ? null : 0) : x + ((y ?? x) - x) * k;
  });
  return o;
}
export function bodyAt(t) {
  const br = bracket(S.proj?.capture?.body?.frames, t); if (!br) return null;
  const [a, b, k] = br;
  if (a.p.length !== b.p.length) return k < 0.5 ? a.p : b.p;   // toma antigua (x,y,z) junto a una nueva (con visibilidad)
  return a.p.map((x, i) => x + (b.p[i] - x) * k);
}

// lo que se ve ahora: en vivo si el movil esta en ese modo (o grabando), si no lo grabado
export function faceNow() {
  if (capKind() === 'face' && CAP.ok && CAP.live && (CAP.rec || !S.playing)) return CAP.live;
  return faceAt(S.t);
}
export function bodyNow() {
  if (capKind() === 'body' && CAP.ok && CAP.liveBody && (CAP.rec || !S.playing)) return CAP.liveBody;
  return bodyAt(S.t);
}
