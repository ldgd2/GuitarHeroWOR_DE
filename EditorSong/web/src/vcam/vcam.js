// Camara virtual con el movil (app Flutter en EditorSong/vcam_app, puente en editor/server/vcam.py).
//   movil -> editor: {type:'pose', q:[x,y,z,w], p:[x,y,z], zoom}   giro (giroscopio) y desplazamiento (mando +
//                    acelerometro) RELATIVOS a la pose de partida, en el espacio de la camara (x der., y arriba, -z delante)
//                    {type:'rec', on} grabar / parar   {type:'live', on} mover el monitor sin grabar   {type:'recenter'}
//   editor -> movil: fotogramas JPEG del monitor (binario) y {type:'state', ...}
// Al GRABAR: la toma armada (o la seleccionada; si no hay, una nueva en el cursor) pasa a animacion por keyframes,
// se reproduce la cancion desde su inicio y cada fotograma se guarda como clave (30 por segundo) hasta su final
// (o hasta que pares, si la toma es nueva).
import * as THREE from 'three';
import { api, changed, emit, pushUndo, S } from '../core/state.js';
import { newShot, worldPose } from '../camera/shots.js';
import { keepRendering, mon, onAfterMonitor, onFrame, progCam } from '../viewport/scene.js';
import { seek, setPlaying } from '../ui/playback.js';
import { CAP, onCapMsg } from '../capture/capture.js';

// Enganches para que otro modulo (el editor de keyframes) reciba la grabacion en su copia de la toma
// en vez de en el proyecto: target() -> objeto con forma de toma, before() antes de grabar, after() al acabar.
export const VCAM_HOOKS = { target: null, before: null, after: null };

export const VCAM = { ws: null, info: null, editor: false, phone: false, live: false, rec: false, target: null,
  pose: { q: [0, 0, 0, 1], p: [0, 0, 0], zoom: 1 }, base: null, shot: null, growing: false, lastKey: -1, fps: 12, error: '' };

const D2R = THREE.MathUtils.degToRad, R2D = THREE.MathUtils.radToDeg;
// texto -> tal cual; fotogramas (ArrayBuffer) -> binario; objetos -> JSON. (Antes los fotogramas se convertian en "{}" y
// el telefono no recibia ninguna imagen.)
const send = m => {
  if (VCAM.ws?.readyState !== 1) return;
  VCAM.ws.send(typeof m === 'string' || m instanceof ArrayBuffer || ArrayBuffer.isView(m) ? m : JSON.stringify(m));
};
const ui = () => emit('inspector');

export async function vcamConnect() {
  if (VCAM.ws && VCAM.ws.readyState <= 1) return;
  try { VCAM.info = await api('/api/vcam/info'); } catch { VCAM.error = 'el servidor no responde'; ui(); return; }
  if (!VCAM.info.started) { VCAM.error = `no se pudo abrir el puerto ${VCAM.info.port}`; ui(); return; }
  const ws = new WebSocket(`ws://127.0.0.1:${VCAM.info.port}/editor`);
  ws.binaryType = 'arraybuffer';
  VCAM.ws = ws;
  ws.onopen = () => { VCAM.editor = true; VCAM.error = ''; ui(); };
  ws.onclose = () => { VCAM.editor = VCAM.phone = false; if (VCAM.rec) stopRec(); VCAM.live = false; VCAM.ws = null; ui(); setTimeout(vcamConnect, 3000); };
  ws.onmessage = ev => { if (typeof ev.data === 'string') try { onMsg(JSON.parse(ev.data)); } catch { /* mensaje roto */ } };
}

function onMsg(m) {
  if (typeof m.type === 'string' && m.type.startsWith('cap')) { onCapMsg(m); return; }
  if (m.type === 'link') {
    VCAM.phone = !!m.phone;
    if (!m.phone) { if (VCAM.rec) stopRec(); VCAM.live = false; onCapMsg({ type: 'capmode', mode: null }); }
    ui();
  }
  else if (m.type === 'pose') VCAM.pose = { q: m.q || [0, 0, 0, 1], p: m.p || [0, 0, 0], zoom: m.zoom || 1 };
  else if (m.type === 'rec') { if (m.on) startRec(); else stopRec(); }
  else if (m.type === 'live') { VCAM.live = !!m.on; VCAM.base = null; ui(); }
  else if (m.type === 'recenter') VCAM.rebase = true;
  else if (m.type === 'quality') { VCAM.fps = Math.min(30, Math.max(4, m.fps || 12)); VCAM.width = Math.min(1280, Math.max(320, m.width || 640)); }
}

export function vcamArm(id) { VCAM.target = id; ui(); sendState(); }
export function vcamLive(on) { VCAM.live = on; VCAM.base = null; send({ type: 'live', on }); ui(); }

const poseOf = s => { const W = worldPose(s, s.t); return { p: W.p.clone(), q: W.q.clone(), fov: W.fov }; };
const progPose = () => {
  const asp = mon.clientWidth / Math.max(1, mon.clientHeight);
  return { p: progCam.position.clone(), q: progCam.quaternion.clone(), fov: R2D(2 * Math.atan(Math.tan(D2R(progCam.fov) / 2) * asp)) };
};

function startRec() {
  if (!S.proj || VCAM.rec) return;
  const hooked = VCAM_HOOKS.target?.();
  if (hooked) VCAM_HOOKS.before?.(); else pushUndo();
  let s = hooked || S.proj.shots?.find(x => x.id === VCAM.target) || (S.sel?.type === 'shot' && S.proj.shots?.find(x => x.id === S.sel.id));
  VCAM.growing = !s;
  if (!s) {   // sin toma elegida: una nueva en el cursor, desde la camara que se ve ahora
    s = newShot(S.t, S.shotRole || 'guitarist', 'keys'); s.dur = 600000;
    s.keys = []; VCAM.base = progPose(); (S.proj.shots ||= []).push(s);
  } else VCAM.base = poseOf(s);
  s.preset = 'keys'; s.keys = []; s.interp = 'linear'; s.fps = Math.max(s.fps || 15, 30);
  VCAM.shot = s; VCAM.rec = true; VCAM.lastKey = -1;
  S.sel = { type: 'shot', id: s.id };
  seek(s.t); setPlaying(true);
  send({ type: 'recenter' });
  changed(); sendState();
}

function stopRec() {
  if (!VCAM.rec) return;
  const s = VCAM.shot;
  VCAM.rec = false; VCAM.shot = null;
  setPlaying(false);
  if (s) {
    if (VCAM.growing) s.dur = Math.max(500, s.keys.at(-1)?.t || 500);
    if (!s.keys.length) { const b = VCAM.base; s.keys = [{ t: 0, p: b.p.toArray(), q: b.q.toArray(), fov: b.fov }]; }
  }
  const hooked = VCAM_HOOKS.target?.() === s;
  VCAM.base = null;
  if (hooked) VCAM_HOOKS.after?.(); else changed();
  sendState();
}

// pose final = pose de partida + movimiento del movil. El giro es relativo a la camara de partida; el desplazamiento
// viene en un marco NIVELADO (y = arriba de verdad, -z = hacia donde miraba la camara al empezar, en horizontal),
// asi caminar, agacharse o subir con el telefono mueve la camara igual en el escenario.
const _q = new THREE.Quaternion(), _v = new THREE.Vector3(), _f = new THREE.Vector3(), Y = new THREE.Vector3(0, 1, 0);
function yawOf(q) {
  _f.set(0, 0, -1).applyQuaternion(q);
  return new THREE.Quaternion().setFromAxisAngle(Y, Math.atan2(-_f.x, -_f.z));
}
function composed(base) {
  const { q, p, zoom } = VCAM.pose;
  _q.set(q[0], q[1], q[2], q[3]).normalize();
  const pos = base.p.clone().add(_v.set(p[0], p[1], p[2]).applyQuaternion(yawOf(base.q)));
  const rot = base.q.clone().multiply(_q);
  const fov = Math.min(150, Math.max(5, R2D(2 * Math.atan(Math.tan(D2R(base.fov) / 2) / Math.max(0.2, zoom)))));
  return { p: pos, q: rot, fov };
}

// despues de que program.js coloque la camara de programa (se registra al final de la carga de modulos)
setTimeout(() => onFrame(() => {
  if (!S.proj || !(VCAM.live || VCAM.rec) || !VCAM.phone) return;
  if (!VCAM.base) VCAM.base = progPose();
  let P = composed(VCAM.base);
  if (VCAM.rebase) { VCAM.base = P; VCAM.rebase = false; P = composed(VCAM.base); }
  const asp = mon.clientWidth / Math.max(1, mon.clientHeight);
  progCam.position.copy(P.p); progCam.quaternion.copy(P.q);
  progCam.fov = R2D(2 * Math.atan(Math.tan(D2R(P.fov) / 2) / asp));
  if (VCAM.rec && VCAM.shot && S.playing) {
    const s = VCAM.shot, rt = Math.round(S.t - s.t);
    if (rt > s.dur) { stopRec(); return; }
    if (rt >= 0 && rt - VCAM.lastKey >= 1000 / 30) {
      s.keys.push({ t: rt, p: P.p.toArray().map(v => +v.toFixed(4)), q: P.q.toArray().map(v => +v.toFixed(5)), fov: +P.fov.toFixed(2) });
      VCAM.lastKey = rt;
    }
  } else if (VCAM.rec && !S.playing) stopRec();   // pausa desde el PC = fin de la toma
}), 0);

// fotogramas del monitor hacia el movil (despues de dibujarlo: el buffer de WebGL aun es valido)
const snap = document.createElement('canvas'), sctx = snap.getContext('2d');
let lastSend = 0, busy = false;
onAfterMonitor(() => {
  if (!VCAM.phone || busy || VCAM.ws?.readyState !== 1 || VCAM.ws.bufferedAmount > 400000) return;
  const now = performance.now(); if (now - lastSend < 1000 / VCAM.fps) return;
  lastSend = now;
  const w = VCAM.width || 640, h = Math.round(w * mon.clientHeight / Math.max(1, mon.clientWidth));
  if (snap.width !== w || snap.height !== h) { snap.width = w; snap.height = h; }
  sctx.drawImage(mon, 0, 0, w, h);
  busy = true;
  snap.toBlob(b => { busy = false; if (b) b.arrayBuffer().then(buf => send(buf)); }, 'image/jpeg', 0.6);
});

function sendState() {
  const s = VCAM.shot || S.proj?.shots?.find(x => x.id === VCAM.target);
  send({ type: 'state', rec: VCAM.rec, live: VCAM.live, playing: !!S.playing, t: Math.round(S.t), hidden: document.hidden, capRec: !!CAP.rec,
    shot: s ? { id: s.id, t: s.t, dur: VCAM.growing && VCAM.rec ? 0 : s.dur } : null, keys: VCAM.shot?.keys.length || 0 });
}
setInterval(() => { if (VCAM.phone) sendState(); }, 250);
CAP.notify = sendState;
// el editor sigue dibujando (y mandando imagen) aunque su ventana quede tapada o minimizada mientras usas el telefono
keepRendering(() => VCAM.phone);
setTimeout(vcamConnect, 500);
