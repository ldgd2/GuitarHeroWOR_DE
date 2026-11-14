// Escenario: nodos reales (posiciones de los musicos) y la geometria del juego (.glb generado por el servidor).
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { $, api, emit, on, S } from '../core/state.js';
import { ROLE_COLOR, ROLE_ES } from '../core/roles.js';
import { VIEW } from '../core/view.js';
import { editorOnly, scene } from './scene.js';
import { loadStageCams } from './stagecams.js';

// Gestor de carga comun (escenario y banda): progreso en S.loading y calidad de textura (?max= del servidor).
const manager = new THREE.LoadingManager();
S.loading = { done: 0, total: 0, errors: 0 };
const progress = () => emit('loading');
manager.onStart = (u, done, total) => { Object.assign(S.loading, { done, total }); progress(); };
manager.onProgress = (u, done, total) => { Object.assign(S.loading, { done, total }); progress(); };
manager.onError = () => { S.loading.errors++; progress(); };
manager.onLoad = () => { Object.assign(S.loading, { done: 0, total: 0 }); progress(); };
manager.setURLModifier(url => url.includes('/api/asset/tex') && !url.includes('&max=') ? `${url}&max=${VIEW.texSize}` : url);

// las URI de textura del .glb son absolutas (/api/asset/tex...): base = origen, no la carpeta del .glb
export const loader = new GLTFLoader(manager).setResourcePath(location.origin);
const markers = new THREE.Group(), geo = new THREE.Group();
scene.add(markers, geo);
editorOnly.add(markers);
const FALLBACK = { guitarist: [5, 0, 6], bassist: [-5, 0, 6], vocalist: [0, 0, 6.7], drummer: [0, 1.3, 1.6] };
let token = 0;
const status = { nodes: '', geo: '' };
const showStatus = () => { $('viewInfo').textContent = [status.nodes, status.geo].filter(Boolean).join(' · '); };

export const nodePos = role => new THREE.Vector3(...(S.venue?.perf?.[role]?.pos || FALLBACK[role]));
export function nodeQuat(role) {
  const a = S.venue?.perf?.[role]?.angles || [0, 0, 0];
  return new THREE.Quaternion().setFromEuler(new THREE.Euler(a[0], a[1], a[2], 'YXZ'));
}
export function stageCenter() {
  const v = ['guitarist', 'bassist', 'vocalist', 'drummer'].map(nodePos);
  return v.reduce((a, b) => a.add(b), new THREE.Vector3()).multiplyScalar(1 / v.length);
}

function label(text, color) {
  const c = document.createElement('canvas'); c.width = 256; c.height = 48;
  const x = c.getContext('2d'); x.font = 'bold 26px system-ui'; x.fillStyle = color; x.fillText(text, 4, 32);
  const s = new THREE.Sprite(new THREE.SpriteMaterial({ map: new THREE.CanvasTexture(c), depthTest: false }));
  s.scale.set(2.4, 0.45, 1); s.renderOrder = 10; return s;
}

function buildMarkers() {
  markers.clear();
  const pts = S.venue.nodes.map(n => new THREE.Vector3(...n.pos));
  markers.add(new THREE.Points(new THREE.BufferGeometry().setFromPoints(pts), new THREE.PointsMaterial({ color: 0x6a7890, size: 0.1 })));
  for (const [r, n] of Object.entries(S.venue.perf)) {
    const ring = new THREE.Mesh(new THREE.RingGeometry(0.45, 0.6, 32), new THREE.MeshBasicMaterial({ color: ROLE_COLOR[r], side: THREE.DoubleSide }));
    ring.rotation.x = -Math.PI / 2; ring.position.set(n.pos[0], n.pos[1] + 0.02, n.pos[2]); markers.add(ring);
    const l = label(ROLE_ES[r], ROLE_COLOR[r]); l.position.set(n.pos[0], n.pos[1] + 2.5, n.pos[2]); markers.add(l);
  }
}

// materiales: originales (iluminados) o planos (MeshBasic con la luz horneada en los colores de vertice);
// "sin texturas" quita el mapa (y lo guarda para volver a ponerlo)
export function applyTextures(root) {
  root.traverse(o => {
    if (!o.isMesh) return;
    for (const m of [].concat(o.material, o.userData.lit || [], o.userData.flat || [])) {
      if (!m) continue;
      if (!('texMap' in m.userData)) m.userData.texMap = m.map || null;
      const want = VIEW.textures ? m.userData.texMap : null;
      if (m.map !== want) { m.map = want; m.needsUpdate = true; }
    }
  });
}
// Modos de mezcla del motor de Neversoft (material.userData.blend): 0 opaco, 1 suma, 2 suma fija, 5/6 mezcla alfa.
// Los planos de humo, haces de luz y brillos son de suma: con mezcla normal salen como paneles grises que tapan
// a la banda. Con suma solo iluminan y no escriben profundidad.
const ADDITIVE = new Set([1, 2]);
function fixBlend(m) {
  if (!m || m.userData.blendFixed) return;
  m.userData.blendFixed = true;
  if (ADDITIVE.has(m.userData.blend)) {
    // en el juego su intensidad la modula el espectaculo de luces; aqui se atenuan para no blanquear la escena
    Object.assign(m, { transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, alphaTest: 0, opacity: 0.25 });
    m.userData.fx = true; m.needsUpdate = true;
  }
}
// Publico: tarjetas con plantilla 0xdd6d5659 y textura de 64x32; el juego pinta encima el render del publico,
// aqui saldrian como bloques de color que tapan los planos desde la grada.
const CROWD = new Set(['0xdd6d5659']);
// humo, haces de luz y brillos (opcion "Humo y haces de luz") y publico (opcion "Publico")
function applyFx() {
  geo.traverse(o => {
    if (!o.isMesh) return;
    const u = (o.userData.lit || o.material).userData;
    if (u.fx) o.visible = VIEW.fx;
    else if (CROWD.has(u.template)) o.visible = VIEW.crowd;
  });
}
function applyFlat() {
  geo.traverse(o => { if (o.isMesh) fixBlend(o.userData.lit || o.material); });
  geo.traverse(o => {
    if (!o.isMesh) return;
    o.userData.lit ||= o.material;
    if (VIEW.flat) {
      const m = o.userData.lit;
      o.userData.flat ||= new THREE.MeshBasicMaterial({ map: m.userData.texMap ?? m.map, vertexColors: m.vertexColors, transparent: m.transparent,
        alphaTest: m.alphaTest, side: m.side, depthWrite: m.depthWrite, opacity: m.opacity, blending: m.blending });
      o.userData.flat.userData = { ...m.userData };
      o.material = o.userData.flat;
    } else o.material = o.userData.lit;
    o.receiveShadow = true;
  });
  applyTextures(geo);
  applyFx();
}

export async function loadVenue(zone) {
  const my = ++token;
  loadStageCams(zone);
  S.venue = await api('/api/venue?zone=' + encodeURIComponent(zone));
  if (my !== token) return;
  buildMarkers();
  status.nodes = `${zone}: ${S.venue.nodes.length} nodos`; status.geo = ''; showStatus();
  geo.clear(); S.venueZone = zone;
  if (!VIEW.geometry) return;
  status.geo = 'cargando geometria del escenario...'; showStatus();
  loader.load('/api/venue/glb?zone=' + encodeURIComponent(zone), gltf => {
    if (my !== token) return;
    geo.add(gltf.scene); applyFlat();
    let n = 0; gltf.scene.traverse(o => { if (o.isMesh) n++; });
    status.geo = `${n} mallas del juego`; showStatus();
  }, undefined, err => { if (my === token) { status.geo = 'no se pudo cargar la geometria: ' + (err.message || err); showStatus(); } });
}

// recargar todo (p. ej. tras cambiar la calidad de textura): escenario y personajes
export function reloadAssets() {
  if (S.venueZone) loadVenue(S.venueZone);
  emit('reloadBand');
}

on('view', k => {
  markers.visible = VIEW.markers;
  if (k === 'flat') applyFlat();
  if (k === 'fx' || k === 'crowd') applyFx();
  if (k === 'textures') { applyTextures(geo); emit('bandTextures'); }
  if (k === 'texSize') reloadAssets();
  if (k === 'geometry' && S.venueZone) loadVenue(S.venueZone);
});
markers.visible = VIEW.markers;
