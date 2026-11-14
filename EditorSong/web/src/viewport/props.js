// Objetos 3D anadidos por el usuario (S.proj.props) para planificar la puesta en escena: cajas, pantallas,
// textos, focos, modelos .glb... Se guardan en el proyecto y se ven en la vista y en el monitor.
// En el juego los dibuja re-wor (EDFX, camera/edfx.js) durante las tomas del editor; el fuego pasa a pirotecnia.
import * as THREE from 'three';
import { TransformControls } from 'three/addons/controls/TransformControls.js';
import { changed, emit, on, pushUndo, S } from '../core/state.js';
import { editCam, editorOnly, onFrame, scene, view } from './scene.js';
import { ownTrack, trackActive } from '../timeline/layout.js';
import { orbit } from './controls.js';
import { loader, stageCenter } from './venue.js';

export const PROP_KINDS = {
  box: 'Caja', sphere: 'Esfera', cylinder: 'Cilindro', plane: 'Panel', screen: 'Pantalla luminosa', text: 'Texto 3D',
  spot: 'Foco de luz', pyro: 'Columna de fuego', glb: 'Modelo .glb',
  particles: 'Particulas del juego', gamemodel: 'Modelo del juego',
};
// Objetos que el juego crea con su propio motor (re-wor/src/de/native_fx.cpp); el resto se dibuja encima
export const NATIVE_PROPS = new Set(['spot', 'pyro', 'particles', 'gamemodel']);
const group = new THREE.Group(); scene.add(group);
const objs = new Map();
export const propObject = id => objs.get(id);   // objeto three.js de un objeto (para exportar su geometria)
export const props = () => (S.proj ? (S.proj.props ||= []) : []);

const gizmo = new TransformControls(editCam, view);
gizmo.setSize(0.8);
scene.add(gizmo);
editorOnly.add(gizmo);
gizmo.addEventListener('dragging-changed', e => {
  orbit.enabled = !e.value;
  if (e.value) pushUndo();
  else { syncFrom(gizmo.object); changed(); }
});
gizmo.addEventListener('objectChange', () => syncFrom(gizmo.object));
export const setGizmoMode = m => gizmo.setMode(m);

function syncFrom(o) {
  const p = o && props().find(x => x.id === o.userData.propId); if (!p) return;
  p.pos = o.position.toArray().map(v => +v.toFixed(3));
  p.rot = [o.rotation.x, o.rotation.y, o.rotation.z].map(v => +THREE.MathUtils.radToDeg(v).toFixed(1));
  p.scale = o.scale.toArray().map(v => +v.toFixed(3));
  emit('inspector');
}

function textTexture(text, color) {
  const c = document.createElement('canvas'); c.width = 1024; c.height = 256;
  const x = c.getContext('2d'); x.font = 'bold 150px system-ui'; x.textAlign = 'center'; x.textBaseline = 'middle';
  x.fillStyle = color; x.shadowColor = color; x.shadowBlur = 30; x.fillText(text || 'TEXTO', 512, 128);
  const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace; return t;
}

function build(p) {
  const color = new THREE.Color(p.color || '#f5a83b');
  let o;
  const std = () => new THREE.MeshStandardMaterial({ color, roughness: 0.6, metalness: 0.1 });
  switch (p.kind) {
    case 'sphere': o = new THREE.Mesh(new THREE.SphereGeometry(0.5, 32, 16), std()); break;
    case 'cylinder': o = new THREE.Mesh(new THREE.CylinderGeometry(0.5, 0.5, 1, 32), std()); break;
    case 'plane': o = new THREE.Mesh(new THREE.PlaneGeometry(1, 1), new THREE.MeshStandardMaterial({ color, side: THREE.DoubleSide })); break;
    case 'screen': o = new THREE.Mesh(new THREE.PlaneGeometry(1.6, 0.9), new THREE.MeshBasicMaterial({ color, side: THREE.DoubleSide })); break;
    case 'text': o = new THREE.Mesh(new THREE.PlaneGeometry(2, 0.5), new THREE.MeshBasicMaterial({ map: textTexture(p.text, p.color || '#ffffff'), transparent: true, side: THREE.DoubleSide, depthWrite: false })); break;
    case 'spot': {
      o = new THREE.Group();
      const l = new THREE.SpotLight(color, 40, 30, 0.35, 0.5); l.position.set(0, 0, 0); l.target.position.set(0, -1, 0);
      o.add(l, l.target);
      const cone = new THREE.Mesh(new THREE.ConeGeometry(0.9, 4, 24, 1, true), new THREE.MeshBasicMaterial({ color, transparent: true, opacity: 0.12, blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide }));
      cone.position.y = -2; o.add(cone);
      o.add(new THREE.Mesh(new THREE.CylinderGeometry(0.15, 0.2, 0.3, 16), new THREE.MeshStandardMaterial({ color: 0x222222 })));
      break;
    }
    case 'pyro': {
      o = new THREE.Group();
      const flame = new THREE.Mesh(new THREE.ConeGeometry(0.35, 3, 16, 1, true), new THREE.MeshBasicMaterial({ color: 0xff7a1a, transparent: true, opacity: 0.6, blending: THREE.AdditiveBlending, depthWrite: false }));
      flame.position.y = 1.5; flame.userData.flame = true; o.add(flame);
      o.add(new THREE.Mesh(new THREE.BoxGeometry(0.5, 0.25, 0.5), new THREE.MeshStandardMaterial({ color: 0x333333 })));
      break;
    }
    case 'particles': {   // vista previa: nube de puntos del color elegido (en el juego: el sistema de particulas real)
      o = new THREE.Group();
      const n = 160, pos = new Float32Array(n * 3);
      for (let i = 0; i < n; i++) { const a = Math.random() * Math.PI * 2, r = Math.random() * 0.6; pos.set([Math.cos(a) * r, Math.random() * 2.5, Math.sin(a) * r], i * 3); }
      const g = new THREE.BufferGeometry(); g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
      const pts = new THREE.Points(g, new THREE.PointsMaterial({ color, size: 0.08, transparent: true, opacity: 0.85, blending: THREE.AdditiveBlending, depthWrite: false }));
      pts.userData.flame = true; o.add(pts);
      o.add(new THREE.Mesh(new THREE.CylinderGeometry(0.25, 0.3, 0.15, 16), new THREE.MeshStandardMaterial({ color: 0x333333 })));
      break;
    }
    case 'gamemodel': {   // vista previa: silueta (en el juego: el modelo real)
      o = new THREE.Group();
      const m = new THREE.MeshStandardMaterial({ color: 0x555566, roughness: 0.8 });
      const body = new THREE.Mesh(new THREE.CapsuleGeometry(0.25, 1.0, 4, 12), m); body.position.y = 0.9; o.add(body);
      const head = new THREE.Mesh(new THREE.SphereGeometry(0.16, 16, 12), m); head.position.y = 1.75; o.add(head);
      break;
    }
    case 'glb': {
      o = new THREE.Group();
      if (p.url) loader.load(p.url, g => { o.add(g.scene); }, undefined, () => { o.add(new THREE.Mesh(new THREE.BoxGeometry(0.5, 0.5, 0.5), new THREE.MeshBasicMaterial({ color: 0xff0000, wireframe: true }))); });
      break;
    }
    default: o = new THREE.Mesh(new THREE.BoxGeometry(1, 1, 1), std());
  }
  o.traverse(m => { if (m.isMesh) { m.castShadow = true; m.userData.propId = p.id; } });
  o.userData.propId = p.id;
  return o;
}

const sig = p => JSON.stringify([p.kind, p.color, p.text, p.url, p.pfx, p.model]);
export function syncProps() {
  const list = S.proj ? props() : [];
  const ids = new Set(list.map(p => p.id));
  for (const [id, o] of objs) if (!ids.has(id) || o.userData.sig !== sig(list.find(p => p.id === id))) {
    if (gizmo.object === o) gizmo.detach();
    group.remove(o); objs.delete(id);
  }
  for (const p of list) {
    let o = objs.get(p.id);
    if (!o) { o = build(p); o.userData.sig = sig(p); objs.set(p.id, o); group.add(o); }
    o.position.fromArray(p.pos || [0, 0, 0]);
    o.rotation.set(...(p.rot || [0, 0, 0]).map(THREE.MathUtils.degToRad));
    o.scale.fromArray(p.scale || [1, 1, 1]);
  }
  const sel = S.sel?.type === 'prop' ? objs.get(S.sel.id) : null;
  if (sel !== gizmo.object) { if (sel) gizmo.attach(sel); else gizmo.detach(); }
}
on('changed', syncProps); on('song', syncProps); on('inspector', syncProps);

let nextId = 1;
export function addProp(kind, extra = {}) {
  if (!S.proj) return null;
  pushUndo();
  const id = Math.max(nextId, ...props().map(p => p.id + 1)); nextId = id + 1;
  const c = stageCenter();
  const p = { id, kind, name: PROP_KINDS[kind] + ' ' + id, pos: [+(c.x + 2).toFixed(2), kind === 'spot' ? 6 : 0.5, +(c.z + 3).toFixed(2)],
    rot: [0, 0, 0], scale: [1, 1, 1], color: kind === 'text' ? '#ffffff' : kind === 'screen' ? '#3aa0ff' : '#f5a83b', text: kind === 'text' ? 'ROCK' : '',
    ...(kind === 'spot' ? { housing: 0, range: 8, radius: 1.2 } : {}),
    ...(kind === 'particles' ? { pfx: 'gp_fx_sparkfountain_sparks_01' } : {}),
    ...(kind === 'gamemodel' ? { model: 'GH_Rockers/Silhouette_M.skin' } : {}),
    ...extra, id };
  props().push(p);
  S.sel = { type: 'prop', id }; S.tab = 'props';
  changed();
  return p;
}

// llamas animadas
let tt = 0;
// cada objeto se ve solo en su tramo de tiempo (pista "Objetos 3D") y si su pista esta activa
onFrame(dt => {
  tt += dt;
  if (!S.proj) return;
  for (const p of props()) {
    const o = objs.get(p.id); if (!o) continue;
    const t0 = p.t ?? 0, t1 = t0 + (p.dur ?? S.proj.length);
    o.visible = p.visible !== false && trackActive(ownTrack(p, 'props')) && (S.sel?.type === 'prop' && S.sel.id === p.id || (S.t >= t0 && S.t < t1));
  }
  for (const o of objs.values()) o.traverse(m => { if (m.userData.flame) { m.scale.set(1 + Math.sin(tt * 20) * 0.08, 0.85 + Math.abs(Math.sin(tt * 13)) * 0.3, 1); } });
});

// clic sobre un objeto lo selecciona (el gizmo se maneja con el raton encima)
const ray = new THREE.Raycaster();
let down = null;
view.addEventListener('pointerdown', e => { down = [e.clientX, e.clientY]; });
view.addEventListener('pointerup', e => {
  if (!down || Math.hypot(e.clientX - down[0], e.clientY - down[1]) > 4 || e.button !== 0 || gizmo.dragging) return;
  const r = view.getBoundingClientRect();
  ray.setFromCamera(new THREE.Vector2(((e.clientX - r.left) / r.width) * 2 - 1, -((e.clientY - r.top) / r.height) * 2 + 1), editCam);
  const hit = ray.intersectObjects(group.children, true)[0];
  if (hit) { let o = hit.object; while (o && o.userData.propId === undefined) o = o.parent; if (o) { S.sel = { type: 'prop', id: o.userData.propId }; S.tab = 'props'; emit('inspector'); emit('timeline'); } }
});
