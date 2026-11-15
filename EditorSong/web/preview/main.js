// Vista previa 3D de la app del movil (WebView): un personaje de WoR que sigue la captura en tiempo real.
// Usa EXACTAMENTE el mismo rig que el editor (rig/bodyRig.js, rig/faceRig.js) con su configuracion por defecto, asi
// que lo que se ve en el telefono es lo que el editor grabara y exportara.
// API (la llama Dart con runJavaScript):
//   PV.load(base64 del .glb en trozos: PV.chunk(s) ... PV.finish())
//   PV.mode('face'|'head'|'body')   PV.face(bs, q)   PV.body(lm, hands)   PV.lost()   PV.neutral()   PV.view(dist)
// Avisa a Dart por el canal 'Preview': {ready}, {loaded, tris}, {error}
import * as THREE from 'three';
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js';
import { applyBodyRig, bodyNeutral, BODY_BONES } from '../src/rig/bodyRig.js';
import { applyFaceRig, applyHeadRig, FACE_BONES, FB } from '../src/rig/faceRig.js';
import { BLEND, BODY_FULL, HAND_OFF, LM_COUNT } from '../src/capture/channels.js';
import { lerpArray, lerpObject, liveStream, pushSample, sampleAt } from '../src/capture/live.js';

// muestras interpoladas (como en el editor): fluido a 60/s aunque la deteccion de menos
const FACE_LIVE = liveStream(lerpObject), BODY_LIVE = liveStream(lerpArray);
// cuerpo + manos en el vector completo del editor (channels.js): sin mano, la ultima conocida con presencia 0
const lastHand = { L: null, R: null };
function packBody(lm, hands) {
  const st = lm.length >= LM_COUNT * 4 ? 4 : 3, out = new Array(BODY_FULL).fill(0);
  for (let i = 0; i < LM_COUNT; i++) { out[i * 4] = lm[i * st]; out[i * 4 + 1] = lm[i * st + 1]; out[i * 4 + 2] = lm[i * st + 2]; out[i * 4 + 3] = st === 4 ? lm[i * st + 3] : 1; }
  for (const sd of ['L', 'R']) {
    const h = hands?.[sd], o = HAND_OFF[sd];
    if (Array.isArray(h) && h.length >= 63) { lastHand[sd] = h; out[o] = 1; }
    const src = (Array.isArray(h) && h) || lastHand[sd];
    if (src) for (let i = 0; i < 63; i++) out[o + 1 + i] = src[i];
  }
  return out;
}

const FACE_CFG = { jaw: 1, jawMax: 20, soft: 0.6, sym: 0.4, mouth: 1, brow: 1, eyes: 1, lids: 1, head: 0.8, swapLR: false, mirrorHead: true, invPitch: false };
const BODY_CFG = { head: true, ground: true, swapLR: false };
const post = m => { try { window.Preview?.postMessage(JSON.stringify(m)); } catch { /* sin canal */ } };

const renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance' });
renderer.setPixelRatio(Math.min(2, devicePixelRatio || 1));
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
document.body.appendChild(renderer.domElement);
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x07080a);
const cam = new THREE.PerspectiveCamera(30, 1, 0.05, 200);
scene.add(new THREE.HemisphereLight(0xdfe6ff, 0x2a2018, 1.4));
const key = new THREE.DirectionalLight(0xffffff, 2.2); key.position.set(1.5, 3, 4); scene.add(key);
const rim = new THREE.DirectionalLight(0xe3a043, 1.6); rim.position.set(-3, 2.5, -3); scene.add(rim);
const floor = new THREE.Mesh(new THREE.CircleGeometry(1.2, 48), new THREE.MeshBasicMaterial({ color: 0x15171c }));
floor.rotation.x = -Math.PI / 2; scene.add(floor);

let c = null, mode = 'head', live = null, liveBody = null, raw = null, rawQ = null, neutralFace = null, neutralBody = null;
let target = new THREE.Vector3(0, 1, 0), dist = 3, yawView = 0, lastData = 0;

function resize() {
  const w = innerWidth, h = innerHeight;
  renderer.setSize(w, h, false); renderer.domElement.style.width = w + 'px'; renderer.domElement.style.height = h + 'px';
  cam.aspect = w / Math.max(1, h); cam.updateProjectionMatrix();
}
addEventListener('resize', resize); resize();

// encuadre: cara y cabeza de cerca; cuerpo entero
function frame() {
  if (!c) return;
  c.root.updateMatrixWorld(true);
  const head = c.bones[FB.head]?.getWorldPosition(new THREE.Vector3()) || new THREE.Vector3(0, 1.6, 0);
  const pel = c.bones[1]?.getWorldPosition(new THREE.Vector3()) || new THREE.Vector3(0, 1, 0);
  if (mode === 'body') { target.set(0, pel.y * 0.95, 0); dist = (head.y + 0.25) / Math.tan(THREE.MathUtils.degToRad(cam.fov / 2)) * 0.5; }
  else { target.copy(head).add(new THREE.Vector3(0, 0.04, 0)); dist = mode === 'face' ? 0.62 : 0.95; }
}

const resetBones = list => { for (const i of list) { const b = c.bones[i]; if (b) { b.quaternion.copy(c.bindQ[i]); b.position.copy(c.bindP[i]); } } };
const ALL = () => c.bones.map((b, i) => i).filter(i => c.bones[i]);

// igual que faceValues() del editor: lo que sobra sobre la cara en reposo; cabeza respecto al reposo
const _q = new THREE.Quaternion(), _n = new THREE.Quaternion(), _e = new THREE.Euler();
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
function faceValues(bs, q) {
  const n = neutralFace, o = {};
  for (const k of BLEND) { const x = +bs[k] || 0, b = n?.bs?.[k] || 0; o[k] = clamp((x - b) / Math.max(0.2, 1 - b), 0, 1); }
  if (q && mode === 'head') {
    _n.fromArray(n?.q || [0, 0, 0, 1]).invert();
    _q.fromArray(q).premultiply(_n);
    _e.setFromQuaternion(_q, 'YXZ');
    o.yaw = clamp(THREE.MathUtils.radToDeg(_e.y), -70, 70);
    o.pitch = clamp(THREE.MathUtils.radToDeg(_e.x), -60, 60);
    o.roll = clamp(THREE.MathUtils.radToDeg(_e.z), -60, 60);
  }
  return o;
}

let chunks = [];
const PV = window.PV = {
  chunk(s) { chunks.push(s); },
  finish() {
    const bin = atob(chunks.join('')); chunks = [];
    const buf = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) buf[i] = bin.charCodeAt(i);
    new GLTFLoader().parse(buf.buffer, '', gltf => {
      const bones = [];
      let tris = 0;
      gltf.scene.traverse(o => {
        const m = /^b(\d{3})_/.exec(o.name); if (m) bones[+m[1]] = o;
        if (o.isMesh) {
          o.frustumCulled = false;
          tris += (o.geometry.index?.count || 0) / 3;
          for (const mt of [].concat(o.material)) { if (mt.transparent && mt.alphaTest > 0) mt.transparent = false; }
        }
      });
      c = { root: gltf.scene, bones, bindQ: bones.map(b => b?.quaternion.clone()), bindP: bones.map(b => b?.position.clone()) };
      scene.add(gltf.scene);
      frame();
      post({ loaded: true, tris: Math.round(tris) });
    }, e => post({ error: String(e?.message || e) }));
  },
  mode(m) { mode = m; live = liveBody = null; FACE_LIVE.buf.length = BODY_LIVE.buf.length = 0; if (c) { resetBones(ALL()); } frame(); },
  face(bs, q) { raw = bs; rawQ = q || null; pushSample(FACE_LIVE, faceValues(bs, q)); lastData = performance.now(); },
  body(lm, hands) { const f = packBody(lm, hands); pushSample(BODY_LIVE, f); liveBody ||= f; lastData = performance.now(); },
  lost() { lastData = 0; },
  neutral() {
    if (mode === 'body') { if (liveBody) neutralBody = bodyNeutral(liveBody); }
    else if (raw) neutralFace = { bs: { ...raw }, q: rawQ ? rawQ.slice() : [0, 0, 0, 1] };
    if (raw) { FACE_LIVE.buf.length = 0; pushSample(FACE_LIVE, faceValues(raw, rawQ)); }
  },
  orbit(dx) { yawView = clamp(yawView + dx, -1.4, 1.4); },
};

// gesto de un dedo: girar la vista alrededor del personaje
let px = null;
addEventListener('pointerdown', e => { px = e.clientX; });
addEventListener('pointermove', e => { if (px != null) { PV.orbit((e.clientX - px) * 0.008); px = e.clientX; } });
addEventListener('pointerup', () => { px = null; });
addEventListener('dblclick', () => { yawView = 0; });

// reposo animado cuando no hay deteccion (respira un poco) para que no parezca colgado
const idleQ = new THREE.Quaternion(), idleE = new THREE.Euler();
let lastT = performance.now();
function tick(now) {
  requestAnimationFrame(tick);
  const dt = Math.min(0.1, (now - lastT) / 1000); lastT = now;
  if (c) {
    const fresh = now - lastData < 600;
    live = sampleAt(FACE_LIVE, now) || live; liveBody = sampleAt(BODY_LIVE, now) || liveBody;
    if (mode === 'body') {
      resetBones([...BODY_BONES, ...FACE_BONES]);
      if (fresh && liveBody) applyBodyRig(c, liveBody, BODY_CFG, neutralBody);
    } else {
      resetBones([FB.neck, FB.head, ...FACE_BONES]);
      c.root.updateMatrixWorld(true);
      if (fresh && live) { applyHeadRig(c, live, FACE_CFG); applyFaceRig(c, live, FACE_CFG); }
    }
    if (!fresh && c.bones[3]) {   // respiracion
      idleQ.setFromEuler(idleE.set(Math.sin(now / 900) * 0.015, 0, 0));
      c.bones[3].quaternion.multiply(idleQ);
    }
    void dt;
  }
  const p = target.clone().add(new THREE.Vector3(Math.sin(yawView) * dist, 0.05 * dist, Math.cos(yawView) * dist));
  cam.position.lerp(p, 0.2); cam.lookAt(target);
  renderer.render(scene, cam);
}
requestAnimationFrame(tick);
post({ ready: true });
