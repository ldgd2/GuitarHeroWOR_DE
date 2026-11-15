// Captura con una camara del PC (webcam, capturadora, camara virtual...): MediaPipe Tasks Vision en el navegador,
// con la GPU del PC (WebGL) y a la velocidad de la camara (30-60 fotogramas por segundo).
// Produce los mismos mensajes que la app del movil y los mete por el mismo camino (onCapMsg), asi que la grabacion,
// la vista previa en el personaje y la exportacion al juego son las mismas para las dos fuentes.
//   modos: 'body' (cuerpo), 'head' (cabeza y rostro), 'face' (rostro con detalle)
// Archivos: el motor WASM va con el editor (dist/mediapipe, copiado de @mediapipe/tasks-vision al compilar) y los
// modelos .task los sirve el editor (/api/mp/model: los de la app si ya estan descargados; si no, se descargan una vez).
import { FaceLandmarker, FilesetResolver, HandLandmarker, PoseLandmarker } from '@mediapipe/tasks-vision';
import { emit } from '../core/state.js';
import { CAP, onCapMsg } from './capture.js';

export const CAM = {
  on: false, starting: false, mode: 'head', deviceId: '', precise: false, mirror: true, smooth: 0.5, hands: true,
  devices: [], fps: 0, dev: '', error: '', video: null, overlay: null, last: null, size: '', perm: 'unknown', asking: false,
};
const changed = () => emit('capture');

// ---- filtro One Euro (igual que el del movil): quita temblor en reposo sin retrasar los gestos rapidos ----
class OneEuro {
  constructor(minCut, beta) { this.minCut = minCut; this.beta = beta; this.x = null; this.dx = 0; this.t = 0; }
  static a(cut, dt) { const r = 2 * Math.PI * cut * dt; return r / (r + 1); }
  filter(v, t) {
    if (this.x === null) { this.x = v; this.t = t; return v; }
    const dt = Math.max(1e-3, (t - this.t) / 1000); this.t = t;
    const dx = (v - this.x) / dt;
    this.dx += OneEuro.a(1, dt) * (dx - this.dx);
    const cut = this.minCut + this.beta * Math.abs(this.dx);
    this.x += OneEuro.a(cut, dt) * (v - this.x);
    return this.x;
  }
}
class Bank {
  constructor(minCut, beta) { this.minCut = minCut; this.beta = beta; this.m = new Map(); }
  filter(k, v, t) { let f = this.m.get(k); if (!f) this.m.set(k, f = new OneEuro(this.minCut, this.beta)); return f.filter(v, t); }
}

// ---- permiso y camaras ----
// perm: 'granted' | 'prompt' | 'denied' | 'unknown' (navegador sin Permissions API) | 'insecure' (pagina no segura)
// Sin permiso, muchos navegadores devuelven la lista de camaras VACIA (o sin nombres): por eso el permiso se pide
// con un boton (gesto del usuario; si no, Chrome/Edge pueden silenciar la peticion) y no al abrir el panel.
const ERR = e => ({
  NotAllowedError: 'Permiso denegado. Si no salio la pregunta, el navegador la tiene bloqueada: mira las instrucciones de abajo.',
  NotFoundError: 'No hay ninguna camara conectada (o Windows no la reconoce).',
  NotReadableError: 'La camara la esta usando otro programa (Zoom, Discord, OBS, Teams...). Cierralo y prueba otra vez.',
  OverconstrainedError: 'Esa camara no admite el formato pedido: elige otra o quita "Precision alta".',
  SecurityError: 'El navegador no deja usar la camara en esta pagina.',
}[e?.name] || `Error de camara (${e?.name || 'desconocido'}): ${e?.message || e}`);

let permWatch = null;
export async function checkPermission() {
  if (!window.isSecureContext || !navigator.mediaDevices?.getUserMedia) { CAM.perm = 'insecure'; changed(); return CAM.perm; }
  try {
    const st = permWatch || (permWatch = await navigator.permissions.query({ name: 'camera' }));
    CAM.perm = st.state;
    st.onchange = () => { CAM.perm = st.state; if (st.state === 'granted') listCameras(); else changed(); };
  } catch { CAM.perm = 'unknown'; }
  changed();
  return CAM.perm;
}

// pide el permiso (llamar desde un clic) y lista las camaras ya con nombre
export async function askPermission() {
  CAM.error = ''; CAM.asking = true; changed();
  try {
    const s = await navigator.mediaDevices.getUserMedia({ video: true, audio: false });
    s.getTracks().forEach(t => t.stop());
    CAM.perm = 'granted';
    await listCameras();
  } catch (e) {
    CAM.error = ERR(e);
    if (e?.name === 'NotAllowedError') CAM.perm = 'denied';
  }
  CAM.asking = false; changed();
}

export async function listCameras() {
  try {
    const devs = await navigator.mediaDevices.enumerateDevices();
    const vids = devs.filter(d => d.kind === 'videoinput');
    if (vids.some(d => d.label)) CAM.perm = 'granted';
    CAM.devices = vids.filter(d => d.deviceId).map((d, i) => ({ id: d.deviceId, label: d.label || `Camara ${i + 1}` }));
    if (!CAM.devices.some(d => d.id === CAM.deviceId)) CAM.deviceId = CAM.devices[0]?.id || '';
    if (CAM.perm === 'granted' && !vids.length) CAM.error = ERR({ name: 'NotFoundError' });
  } catch (e) {
    CAM.error = ERR(e);
  }
  changed();
  return CAM.devices;
}
if (navigator.mediaDevices?.addEventListener) navigator.mediaDevices.addEventListener('devicechange', () => listCameras());

// el <video> vive siempre en la pagina (oculto, pero decodificando): el panel solo lo muestra mientras esta abierto,
// asi cambiar de pestana no para la captura
export function ensureVideo() {
  if (CAM.video) return CAM.video;
  let park = document.getElementById('camPark');
  if (!park) {
    park = document.createElement('div'); park.id = 'camPark';
    Object.assign(park.style, { position: 'fixed', left: '0', top: '0', width: '2px', height: '2px', overflow: 'hidden', opacity: '0', pointerEvents: 'none' });
    document.body.appendChild(park);
  }
  const v = document.createElement('video');
  v.className = 'camVideo'; v.muted = true; v.playsInline = true;
  park.appendChild(v);
  CAM.video = v;
  return v;
}
export const parkVideo = () => { const v = CAM.video, park = document.getElementById('camPark'); if (v && park && v.parentElement !== park) park.appendChild(v); };

// ---- MediaPipe ----
let fileset = null, face = null, pose = null, hand = null, handBank = null, stream = null, loop = 0, bank = null, bodyBank = null, lastQ = null, lastTs = 0;
const MODELS = { face: 'face_landmarker.task', body: 'pose_landmarker_full.task', bodyHeavy: 'pose_landmarker_heavy.task', hand: 'hand_landmarker.task' };

async function create(kind) {
  fileset ||= await FilesetResolver.forVisionTasks(new URL('./mediapipe', location.href).href.replace(/\/$/, ''));
  const model = `/api/mp/model?name=${kind === 'face' ? MODELS.face : kind === 'hand' ? MODELS.hand : CAM.precise ? MODELS.bodyHeavy : MODELS.body}`;
  const make = delegate => (kind === 'hand'
    ? HandLandmarker.createFromOptions(fileset, {
      baseOptions: { modelAssetPath: model, delegate }, runningMode: 'VIDEO', numHands: 2,
      minHandDetectionConfidence: 0.4, minHandPresenceConfidence: 0.4, minTrackingConfidence: 0.4,
    })
    : kind === 'face'
    ? FaceLandmarker.createFromOptions(fileset, {
      baseOptions: { modelAssetPath: model, delegate }, runningMode: 'VIDEO', numFaces: 1,
      minFaceDetectionConfidence: 0.4, minFacePresenceConfidence: 0.4, minTrackingConfidence: 0.4,
      outputFaceBlendshapes: true, outputFacialTransformationMatrixes: true,
    })
    : PoseLandmarker.createFromOptions(fileset, {
      baseOptions: { modelAssetPath: model, delegate }, runningMode: 'VIDEO', numPoses: 1,
      minPoseDetectionConfidence: 0.4, minPosePresenceConfidence: 0.4, minTrackingConfidence: 0.4,
    }));
  try { const r = await make('GPU'); CAM.dev = 'GPU'; return r; } catch { const r = await make('CPU'); CAM.dev = 'CPU'; return r; }
}

export async function startWebcam(opts = {}) {
  Object.assign(CAM, opts);
  stopWebcam(false);
  CAM.starting = true; CAM.error = ''; changed();
  try {
    const hi = CAM.precise || CAM.mode === 'face';
    stream = await navigator.mediaDevices.getUserMedia({ audio: false, video: {
      deviceId: CAM.deviceId ? { exact: CAM.deviceId } : undefined,
      width: { ideal: hi ? 1280 : 960 }, height: { ideal: hi ? 720 : 540 }, frameRate: { ideal: 60 },
    } });
    const v = ensureVideo();
    v.muted = true; v.playsInline = true; v.srcObject = stream;
    await v.play();
    CAM.video = v;
    if (CAM.perm !== 'granted' || !CAM.devices.length) { CAM.perm = 'granted'; listCameras(); }
    const st = stream.getVideoTracks()[0].getSettings();
    CAM.size = `${st.width}x${st.height} a ${Math.round(st.frameRate || 0)}/s`;
    const kind = CAM.mode === 'body' ? 'body' : 'face';
    if (kind === 'face') face = await create('face');
    else {
      pose = await create('body');
      if (CAM.hands) { const dev = CAM.dev; try { hand = await create('hand'); } catch (e) { hand = null; CAM.error = `Sin manos (el cuerpo sigue): ${e?.message || e}`; } CAM.dev = dev; }
    }
    const mc = 3.5 - 2.8 * CAM.smooth;   // mas suavizado = corte mas bajo en reposo
    bank = new Bank(2.0, 0.6); bodyBank = new Bank(mc * 0.5, 0.8); handBank = new Bank(mc * 0.8, 1.2); lastQ = null;
    CAM.on = true; CAM.starting = false;
    CAP.pcOn = true;
    onCapMsg({ type: 'capmode', mode: CAM.mode, src: 'pc' });
    run();
  } catch (e) {
    CAM.starting = false;
    CAM.error = e?.name ? ERR(e) : `No se pudo iniciar: ${e?.message || e}`;
    if (e?.name === 'NotAllowedError') CAM.perm = 'denied';
    stopWebcam(false);
  }
  changed();
}

export function stopWebcam(notify = true) {
  cancelAnimationFrame(loop); loop = 0;
  if (CAM.video?.cancelVideoFrameCallback && CAM.vfc) CAM.video.cancelVideoFrameCallback(CAM.vfc);
  stream?.getTracks().forEach(t => t.stop()); stream = null;
  face?.close(); face = null; pose?.close(); pose = null; hand?.close(); hand = null;
  const was = CAM.on; CAM.on = false; CAM.fps = 0; CAM.last = null;
  if (notify && was) onCapMsg({ type: 'capmode', mode: null, src: 'pc' });
  CAP.pcOn = false;
  changed();
}

// un resultado por fotograma de la camara (requestVideoFrameCallback; si no hay, requestAnimationFrame)
let frames = 0, fpsT = 0;
function run() {
  const v = CAM.video;
  const next = () => { if (!CAM.on) return; if (v.requestVideoFrameCallback) CAM.vfc = v.requestVideoFrameCallback(step); else loop = requestAnimationFrame(step); };
  let lastFrameTime = -1;
  function step() {
    if (!CAM.on) return;
    if (v.readyState >= 2 && v.currentTime !== lastFrameTime) {
      lastFrameTime = v.currentTime;
      const ts = Math.max(performance.now(), lastTs + 1); lastTs = ts;
      try { detect(ts); } catch (e) { CAM.error = `Error de MediaPipe: ${e?.message || e}`; changed(); }
      frames++;
      if (ts - fpsT >= 500) { CAM.fps = frames * 1000 / (ts - fpsT || 1); frames = 0; fpsT = ts; changed(); }
    }
    next();
  }
  next();
}

const r3 = v => Math.round(v * 1000) / 1000;
function detect(ts) {
  const v = CAM.video;
  if (face) {
    const r = face.detectForVideo(v, ts);
    const shapes = r.faceBlendshapes?.[0]?.categories, lms = r.faceLandmarks?.[0];
    if (!shapes || !lms) { send({ ok: false }); CAM.last = null; return; }
    const bs = {};
    for (const c of shapes) if (c.categoryName !== '_neutral') bs[c.categoryName] = r3(Math.min(1, Math.max(0, bank.filter(c.categoryName, c.score, ts))));
    const m = { ok: true, bs };
    const mat = r.facialTransformationMatrixes?.[0]?.data;
    if (CAM.mode === 'head' && mat) m.q = headQuat(mat, ts);
    CAM.last = { kind: 'face', pts: lms };
    send(m);
  } else if (pose) {
    const r = pose.detectForVideo(v, ts);
    const w = r.worldLandmarks?.[0], n2 = r.landmarks?.[0];
    if (!w || w.length < 33) { send({ ok: false }); CAM.last = null; return; }
    const lm = [];
    for (let i = 0; i < 33; i++) {
      const vis = w[i].visibility ?? n2?.[i]?.visibility ?? 1;
      lm.push(r3(bodyBank.filter('x' + i, w[i].x, ts)), r3(bodyBank.filter('y' + i, w[i].y, ts)), r3(bodyBank.filter('z' + i, w[i].z, ts)), Math.round(vis * 100) / 100);
    }
    const m = { ok: true, lm };
    // manos: se asignan al lado del cuerpo por cercania a cada muneca en la imagen (no por la etiqueta de MediaPipe,
    // que depende de si la imagen esta en espejo)
    let handPts = null;
    if (hand && n2) {
      const hr = hand.detectForVideo(v, ts);
      if (hr.landmarks?.length) {
        m.hands = {}; handPts = [];
        const wr = { L: n2[15], R: n2[16] }, used = new Set();
        const pairs = [];
        hr.landmarks.forEach((h, k) => { for (const sd of ['L', 'R']) pairs.push([Math.hypot(h[0].x - wr[sd].x, h[0].y - wr[sd].y), k, sd]); });
        pairs.sort((a, b) => a[0] - b[0]);
        for (const [dd, k, sd] of pairs) {
          if (used.has(k) || m.hands[sd] || dd > 0.2) continue;
          used.add(k);
          const wl = hr.worldLandmarks?.[k]; if (!wl) continue;
          const arr = [];
          for (let i = 0; i < 21; i++) arr.push(r3(handBank.filter(sd + 'x' + i, wl[i].x, ts)), r3(handBank.filter(sd + 'y' + i, wl[i].y, ts)), r3(handBank.filter(sd + 'z' + i, wl[i].z, ts)));
          m.hands[sd] = arr; handPts.push(hr.landmarks[k]);
        }
      }
    }
    CAM.last = { kind: 'body', pts: n2, hands: handPts };
    send(m);
  }
  drawOverlay();
}
const send = m => onCapMsg({ type: 'cap', mode: CAM.mode, src: 'pc', ...m });

// matriz 4x4 por columnas (cara canonica -> camara) -> cuaternion filtrado y con signo continuo (como en el movil)
function headQuat(m, t) {
  const col = (c) => { const x = m[c * 4], y = m[c * 4 + 1], z = m[c * 4 + 2], s = Math.hypot(x, y, z) || 1; return [x / s, y / s, z / s]; };
  const [a, d, g] = col(0), [b, e, h] = col(1), [c, f, k] = col(2);
  const q = [0, 0, 0, 1], tr = a + e + k;
  if (tr > 0) { const s = Math.sqrt(tr + 1) * 2; q[3] = 0.25 * s; q[0] = (h - f) / s; q[1] = (c - g) / s; q[2] = (d - b) / s; }
  else if (a > e && a > k) { const s = Math.sqrt(1 + a - e - k) * 2; q[3] = (h - f) / s; q[0] = 0.25 * s; q[1] = (b + d) / s; q[2] = (c + g) / s; }
  else if (e > k) { const s = Math.sqrt(1 + e - a - k) * 2; q[3] = (c - g) / s; q[0] = (b + d) / s; q[1] = 0.25 * s; q[2] = (f + h) / s; }
  else { const s = Math.sqrt(1 + k - a - e) * 2; q[3] = (d - b) / s; q[0] = (c + g) / s; q[1] = (f + h) / s; q[2] = 0.25 * s; }
  if (lastQ && lastQ[0] * q[0] + lastQ[1] * q[1] + lastQ[2] * q[2] + lastQ[3] * q[3] < 0) for (let i = 0; i < 4; i++) q[i] = -q[i];
  for (let i = 0; i < 4; i++) q[i] = bank.filter('q' + i, q[i], t);
  const n = Math.hypot(...q) || 1;
  lastQ = q.map(x => x / n);
  return lastQ.map(x => Math.round(x * 10000) / 10000);
}

// ---- dibujo de lo detectado sobre el video del panel ----
const FACE_RINGS = [[10, 338, 297, 332, 284, 251, 389, 356, 454, 323, 361, 288, 397, 365, 379, 378, 400, 377, 152, 148, 176, 149, 150, 136, 172, 58, 132, 93, 234, 127, 162, 21, 54, 103, 67, 109],
  [61, 185, 40, 39, 37, 0, 267, 269, 270, 409, 291, 375, 321, 405, 314, 17, 84, 181, 91, 146], [78, 191, 80, 81, 82, 13, 312, 311, 310, 415, 308, 324, 318, 402, 317, 14, 87, 178, 88, 95],
  [33, 246, 161, 160, 159, 158, 157, 173, 133, 155, 154, 153, 145, 144, 163, 7], [362, 398, 384, 385, 386, 387, 388, 466, 263, 249, 390, 373, 374, 380, 381, 382],
  [70, 63, 105, 66, 107], [336, 296, 334, 293, 300]];
const POSE_LINES = [[11, 12], [11, 13], [13, 15], [12, 14], [14, 16], [11, 23], [12, 24], [23, 24], [23, 25], [25, 27], [24, 26], [26, 28], [27, 31], [28, 32]];
function drawOverlay() {
  const cv = CAM.overlay, v = CAM.video; if (!cv || !v) return;
  const w = cv.clientWidth, h = cv.clientHeight; if (!w || !h) return;
  if (cv.width !== w * devicePixelRatio) { cv.width = w * devicePixelRatio; cv.height = h * devicePixelRatio; }
  const g = cv.getContext('2d'); g.setTransform(devicePixelRatio, 0, 0, devicePixelRatio, 0, 0); g.clearRect(0, 0, w, h);
  const L = CAM.last; if (!L?.pts) return;
  // el video se ve con object-fit: contain
  const s = Math.min(w / v.videoWidth, h / v.videoHeight), ox = (w - v.videoWidth * s) / 2, oy = (h - v.videoHeight * s) / 2;
  const X = p => ox + (CAM.mirror ? 1 - p.x : p.x) * v.videoWidth * s, Y = p => oy + p.y * v.videoHeight * s;
  g.lineWidth = 2; g.strokeStyle = 'rgba(227,160,67,.9)'; g.fillStyle = 'rgba(227,160,67,.9)';
  if (L.kind === 'face') {
    for (const ring of FACE_RINGS) { g.beginPath(); ring.forEach((i, k) => { const p = L.pts[i]; if (p) (k ? g.lineTo(X(p), Y(p)) : g.moveTo(X(p), Y(p))); }); g.closePath(); g.stroke(); }
  } else {
    for (const [a, b] of POSE_LINES) {
      const p = L.pts[a], q = L.pts[b]; if (!p || !q) continue;
      g.globalAlpha = Math.max(0.15, Math.min(p.visibility ?? 1, q.visibility ?? 1));
      g.beginPath(); g.moveTo(X(p), Y(p)); g.lineTo(X(q), Y(q)); g.stroke();
    }
    g.globalAlpha = 1;
    g.strokeStyle = 'rgba(110,190,255,.95)';
    for (const h of L.hands || []) for (const ch of [[0, 1, 2, 3, 4], [0, 5, 6, 7, 8], [5, 9, 10, 11, 12], [9, 13, 14, 15, 16], [13, 17, 18, 19, 20], [0, 17]]) {
      g.beginPath(); ch.forEach((i, k) => (k ? g.lineTo(X(h[i]), Y(h[i])) : g.moveTo(X(h[i]), Y(h[i])))); g.stroke();
    }
  }
}
