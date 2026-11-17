// Escena three.js compartida, las dos vistas (edicion y monitor de programa) y el bucle de dibujo.
import * as THREE from 'three';
import { $ } from '../core/state.js';
import { VIEW } from '../core/view.js';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/addons/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/addons/postprocessing/UnrealBloomPass.js';
import { OutputPass } from 'three/addons/postprocessing/OutputPass.js';
import { ShaderPass } from 'three/addons/postprocessing/ShaderPass.js';

// Algunos materiales del juego (normales nulas, mezclas aditivas) dan pixeles NaN/infinitos: el desenfoque del
// bloom los extiende y la imagen sale negra. Se limpian antes del bloom.
const SANITIZE = {
  uniforms: { tDiffuse: { value: null } },
  vertexShader: 'varying vec2 vUv; void main() { vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }',
  fragmentShader: `uniform sampler2D tDiffuse; varying vec2 vUv;
    void main() { vec4 c = texture2D(tDiffuse, vUv);
      bvec4 bad = bvec4(!(c.r == c.r) || abs(c.r) > 1e4, !(c.g == c.g) || abs(c.g) > 1e4, !(c.b == c.b) || abs(c.b) > 1e4, !(c.a == c.a));
      if (any(bad)) c = vec4(0.0, 0.0, 0.0, 1.0);
      gl_FragColor = clamp(c, 0.0, 32.0); }`,
};

export const view = $('view'), mon = $('monitor');
const mk = canvas => {
  const r = new THREE.WebGLRenderer({ canvas, antialias: true });
  r.outputColorSpace = THREE.SRGBColorSpace; r.shadowMap.type = THREE.PCFSoftShadowMap; return r;
};
export const R1 = mk(view), R2 = mk(mon);

export const scene = new THREE.Scene();
scene.background = new THREE.Color(0x0d0f13);
export const hemi = new THREE.HemisphereLight(0xffffff, 0x303040, 1.3); scene.add(hemi);
export const sun = new THREE.DirectionalLight(0xffffff, 1.6); sun.position.set(4, 12, 14); scene.add(sun, sun.target);
sun.shadow.mapSize.set(2048, 2048); sun.shadow.bias = -0.0005;
Object.assign(sun.shadow.camera, { left: -12, right: 12, top: 12, bottom: -12, near: 1, far: 60 });
export const grid = new THREE.GridHelper(60, 60, 0x334455, 0x222a33); scene.add(grid);

export const editCam = new THREE.PerspectiveCamera(50, 1, 0.05, 2000); editCam.position.set(0, 6, 20);
export const progCam = new THREE.PerspectiveCamera(45, 16 / 9, 0.05, 2000);
export const camHelper = new THREE.CameraHelper(progCam); scene.add(camHelper);

const hooks = [];
export const onFrame = fn => hooks.push(fn);
const afterMon = [];
export const onAfterMonitor = fn => afterMon.push(fn);   // justo despues de dibujar el monitor (p. ej. capturarlo)
// objetos que solo se ven en la vista de edicion (marcadores, rejilla, gizmo, esqueletos), nunca en el monitor
export const editorOnly = new Set([grid]);
// Aspecto del juego (VIEW.post): tonemapping filmico ACES, bloom (el resplandor de focos, pirotecnia y brillos
// de WoR) y niebla de profundidad. Se aplica al monitor de programa y, si se pide, tambien a la vista de edicion.
const post = new Map();
function composer(R) {
  let c = post.get(R);
  if (!c) {
    const ec = new EffectComposer(R), rp = new RenderPass(scene, progCam);
    const bloom = new UnrealBloomPass(new THREE.Vector2(512, 512), 0.6, 0.45, 0.78);
    ec.addPass(rp); ec.addPass(new ShaderPass(SANITIZE)); ec.addPass(bloom); ec.addPass(new OutputPass());
    c = { ec, rp, bloom, w: 0, h: 0 }; post.set(R, c);
  }
  return c;
}
const FOG = new THREE.FogExp2(0x0d0f13, 0.0045);
function draw(R, cam, usePost) {
  const tm = VIEW.post ? THREE.ACESFilmicToneMapping : THREE.NoToneMapping;
  if (R.toneMapping !== tm) { R.toneMapping = tm; scene.traverse(o => { if (o.material) [].concat(o.material).forEach(m => { m.needsUpdate = true; }); }); }
  R.toneMappingExposure = VIEW.exposure;
  scene.fog = VIEW.post && VIEW.fog ? FOG : null;
  if (!((VIEW.post || VIEW.fxBloom > 0) && usePost)) { R.render(scene, cam); return; }
  const c = composer(R), sz = R.getSize(new THREE.Vector2());
  if (c.w !== sz.x || c.h !== sz.y) { c.ec.setPixelRatio(R.getPixelRatio()); c.ec.setSize(sz.x, sz.y); c.w = sz.x; c.h = sz.y; }
  // glow de la pista de efectos: el mismo bloom con mas fuerza y su umbral
  c.rp.camera = cam; c.bloom.strength = VIEW.bloom + 1.6 * (VIEW.fxBloom || 0);
  c.bloom.threshold = VIEW.fxBloom > 0 && VIEW.fxBloomThreshold != null ? 0.35 + 0.6 * VIEW.fxBloomThreshold : 0.78;
  c.ec.render();
}
function renderClean(R, cam, usePost = true) {
  const was = [...editorOnly].map(o => [o, o.visible]);
  for (const [o] of was) o.visible = false;
  draw(R, cam, usePost);
  for (const [o, v] of was) o.visible = v;
}

function fit(r, c) {
  const w = c.clientWidth, h = c.clientHeight, d = devicePixelRatio || 1;
  if (c.width !== Math.floor(w * d) || c.height !== Math.floor(h * d)) { r.setPixelRatio(d); r.setSize(w, h, false); }
  return w / Math.max(1, h);
}

// Pantalla dividida (vista previa): fn(aspecto) -> [{rect: [x, y, ancho, alto] (0..1, desde arriba), cam}] o null
let splitFn = null;
export const setMonitorSplit = fn => { splitFn = fn; };
function drawSplit(R, c, strips) {
  const W = c.clientWidth, H = c.clientHeight;
  R.setScissorTest(true);
  for (const s of strips) {
    const [x, y, w, h] = s.rect, px = x * W, py = (1 - y - h) * H, pw = Math.max(1, w * W), ph = Math.max(1, h * H);
    const [cx, cy, cw, ch] = s.clip || s.rect;   // parte visible (barrido de entrada)
    R.setViewport(px, py, pw, ph); R.setScissor(cx * W, (1 - cy - ch) * H, Math.max(1, cw * W), Math.max(1, ch * H));
    renderClean(R, s.cam, false);
    // separacion entre franjas
    R.setScissor(px, py + ph - 1, pw, 2); R.clear(); R.setScissor(px + pw - 1, py, 2, ph); R.clear();
  }
  R.setScissorTest(false); R.setViewport(0, 0, W, H);
}

let last = performance.now();
function frame(now) {
  try { frameBody(now); } catch (err) { console.error('[frame]', err); }
  requestAnimationFrame(frame);
}
function frameBody(now) {
  const dt = Math.min(0.1, (now - last) / 1000); last = now;
  for (const fn of hooks) { try { fn(dt); } catch (err) { console.error('[frame]', err); } }   // un fallo no congela el dibujo
  grid.visible = VIEW.grid;
  // sombras: solo con luces dinamicas (con la luz horneada los materiales no reciben luz)
  const sh = VIEW.shadows && !VIEW.flat;
  if (R1.shadowMap.enabled !== sh) { R1.shadowMap.enabled = R2.shadowMap.enabled = sh; scene.traverse(o => { if (o.material) [].concat(o.material).forEach(m => { m.needsUpdate = true; }); }); }
  sun.castShadow = sh;
  if (editCam.fov !== VIEW.fov) editCam.fov = VIEW.fov;
  const a1 = fit(R1, view), a2 = fit(R2, mon);
  editCam.aspect = a1; editCam.updateProjectionMatrix();
  const keepFov = progCam.fov;
  // vista principal: camara de edicion, o la de programa si se pide "ver por la camara de programa"
  camHelper.visible = !VIEW.through;
  if (VIEW.through) { progCam.aspect = a1; progCam.updateProjectionMatrix(); renderClean(R1, progCam); }
  else draw(R1, editCam, VIEW.postEdit);
  camHelper.visible = false;
  progCam.fov = keepFov; progCam.aspect = a2; progCam.updateProjectionMatrix(); camHelper.update();
  renderClean(R2, progCam);
  let strips = null;
  try { strips = splitFn?.(a2); } catch (err) { console.error('[split]', err); }
  if (strips?.length) drawSplit(R2, mon, strips);
  for (const fn of afterMon) { try { fn(); } catch (err) { console.error('[monitor]', err); } }
}
export const start = () => requestAnimationFrame(frame);

// Con la pestana oculta el navegador para requestAnimationFrame (y frena los temporizadores a 1 por segundo). Un
// temporizador dentro de un Worker no se frena: mientras cond() sea cierto, dibuja ~20 veces por segundo.
export function keepRendering(cond) {
  try {
    const src = 'setInterval(() => postMessage(0), 50);';
    const w = new Worker(URL.createObjectURL(new Blob([src], { type: 'text/javascript' })));
    w.onmessage = () => {
      if (!document.hidden || !cond()) return;
      try { frameBody(performance.now()); } catch (err) { console.error('[frame]', err); }
    };
  } catch (err) { console.warn('sin dibujo en segundo plano', err); }
}
// pruebas sin ventana (?debug): avanzar un fotograma a mano
if (new URLSearchParams(location.search).has('debug')) window.__frame = () => frame(performance.now());
