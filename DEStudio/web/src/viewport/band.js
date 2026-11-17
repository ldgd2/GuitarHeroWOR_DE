// Banda en la vista 3D: personajes del juego (.glb con esqueleto de 128 huesos) animados con los .ska reales.
// Que animacion suena en cada momento, por orden de prioridad:
//   1. "Anim. de prueba" elegida en la pestana Banda
//   2. un clip activo de la cancion donde participa el musico (animacion del PAK, en su nodo de salida)
//   3. el ultimo Band_PlayLoop del musico (biblioteca anim_loops; version hombre/mujer segun el cuerpo)
//   4. bateria: pose sentada drum_s1t3 + capa aditiva drum_loop_generic_100 (asi la monta el juego)
// Rotaciones del .ska: absolutas (locales al padre), salvo las capas aditivas, que se multiplican sobre la base.
// Traslaciones: desplazamientos sobre la pose de enlace. Despues se cierra el IK (ver ik.js).
import * as THREE from 'three';
import { $, emit, on, S } from '../core/state.js';
import { hx, K, keyOf, val } from '../core/qb.js';
import { ROLE_ES, ROLES, roleOfKey, roleOfStart } from '../core/roles.js';
import { band, specOf } from '../band/config.js';
import { activeClips, animBpm, beatsAt, clipChars, loopAt } from '../song/clips.js';
import { VIEW } from '../core/view.js';
import { getAnim, sampleQ, sampleV } from './anim.js';
import { handsOnGuitar } from './hands.js';
import { applySkin, DEFAULT_SKIN } from './skin.js';
import { prepareIK, solveIK } from './ik.js';
import { editCam, editorOnly, onFrame, scene, view } from './scene.js';
import { applyCapture } from '../rig/apply.js';
import { prepareFace } from '../rig/faceRig.js';
import { placeIkTargets, prepareBody } from '../rig/bodyRig.js';
import { applyPoseLayers } from '../rig/poseLayer.js';
import { applyTextures, loader, nodePos, nodeQuat } from './venue.js';

const BASE_POSE = { drummer: 'drum_s1t3' };          // pose sobre la que se suman las capas aditivas
const IDLE = { drummer: 'drum_loop_generic_100' };   // si la cancion no da nada a ese musico
const group = new THREE.Group(); scene.add(group);
const chars = {};
const info = {};
const animId = name => S.anims.find(a => a.name.toLowerCase() === name.toLowerCase())?.id;

function load(role) {
  if (!S.catalog) return;
  const spec = specOf(role);
  const url = '/api/band/character.glb?spec=' + encodeURIComponent(JSON.stringify(spec));
  const c = chars[role] ||= {};
  if (c.url === url) return;
  c.url = url; info[role] = 'cargando modelo...';
  loader.load(url, gltf => {
    if (c.url !== url) return;
    if (c.root) group.remove(c.root);
    if (c.skel) { scene.remove(c.skel); editorOnly.delete(c.skel); c.skel = null; }
    const bones = [];
    gltf.scene.traverse(o => {
      const m = /^b(\d{3})_/.exec(o.name); if (m) bones[+m[1]] = o;
      if (o.isMesh) { o.frustumCulled = false; o.castShadow = true; o.userData.role = role; }
    });
    applyTextures(gltf.scene);
    applySkin(gltf.scene, band[role]?.skin || DEFAULT_SKIN);
    Object.assign(c, { root: gltf.scene, bones, gender: spec.gender,
      bindQ: bones.map(b => b?.quaternion.clone()), bindP: bones.map(b => b?.position.clone()) });
    prepareIK(c); prepareFace(c); prepareBody(c);
    group.add(c.root); info[role] = '';
  }, undefined, err => { if (c.url === url) info[role] = 'error al cargar: ' + (err.message || err); });
}
export const loadBand = () => ROLES.forEach(load);
export const charOf = role => chars[role];
// posicion actual (mundo) de un hueso por su nombre del juego (Bone_Chest, Bone_Head...), para las camaras
export function boneWorld(role, name) {
  const c = chars[role]; if (!c?.bones) return null;
  const n = '_' + name.toLowerCase(), b = c.bones.find(x => x && x.name.toLowerCase().endsWith(n));
  return b ? b.getWorldPosition(new THREE.Vector3()) : null;
}
on('band', role => load(role));
on('reloadBand', () => { ROLES.forEach(r => { if (chars[r]) chars[r].url = ''; }); loadBand(); });
on('bandTextures', () => ROLES.forEach(r => chars[r]?.root && applyTextures(chars[r].root)));
// cambiar solo el tono de piel no recarga el modelo
on('bandSkin', role => chars[role]?.root && applySkin(chars[role].root, band[role]?.skin || DEFAULT_SKIN));

// clic (sin arrastrar) sobre un musico: abre su ficha en la pestana Banda
const ray = new THREE.Raycaster();
let down = null;
view.addEventListener('pointerdown', e => { down = [e.clientX, e.clientY]; });
view.addEventListener('pointerup', e => {
  if (!down || Math.hypot(e.clientX - down[0], e.clientY - down[1]) > 4 || e.button !== 0) return;
  if (document.body.classList.contains('kfMode')) return;   // el editor de animacion usa los clics de la vista
  const r = view.getBoundingClientRect();
  ray.setFromCamera(new THREE.Vector2(((e.clientX - r.left) / r.width) * 2 - 1, -((e.clientY - r.top) / r.height) * 2 + 1), editCam);
  // las mallas con piel no se intersectan bien con su pose animada: se usa una capsula por musico
  let best = null;
  for (const role of ROLES) {
    const c = chars[role]; if (!c?.root || !c.bones[1]) continue;
    const pel = c.bones[1].getWorldPosition(new THREE.Vector3()), head = c.bones[6]?.getWorldPosition(new THREE.Vector3()) || pel;
    const seg = new THREE.Vector3(), onRay = new THREE.Vector3();
    const d2 = ray.ray.distanceSqToSegment(pel.clone().setY(pel.y - 0.9), head.clone().setY(head.y + 0.2), onRay, seg);
    const dist = onRay.distanceTo(ray.ray.origin);
    if (d2 < 0.35 * 0.35 && (!best || dist < best.dist)) best = { role, dist };
  }
  if (best) { S.bandFocus = best.role; S.tab = 'band'; emit('inspector'); emit('band-focus'); }
});

function pick(role, t, c) {
  const pv = band[role]?.preview;
  if (pv) return { src: 'loop', id: hx(keyOf(pv)), lt: t / 1000, loop: true, name: 'prueba: ' + pv, node: role };
  if (S.proj) {
    for (const a of activeClips(t)) for (const ch of clipChars(a.def)) {
      if (roleOfKey(val(ch, 'name', 0)) !== role) continue;
      const id = hx(val(ch, 'anim', 0));
      const src = S.proj.skas.some(s => s.id === id) ? 'song' : 'loop';
      // timefactor: velocidad de reproduccion de la animacion del clip; ik_target: guitar = manos al instrumento
      const tf = val(ch, 'timefactor', 1) || 1;
      return { src, id, lt: (t - a.t0) / 1000 * tf + a.sp.sf / 30, loop: false, name: 'clip ' + (S.animById.get(id) || id),
        node: roleOfStart(val(ch, 'startnode', role)), hands: val(ch, 'ik_targetl') === K.guitar ? 'guitar' : null, inst: a.t0 };
    }
    const l = loopAt(role, t);
    if (l) {
      const id = hx(c.gender === 'f' ? (l.female || l.male) : l.male), name = S.animById.get(id) || id;
      // el juego ajusta el loop al tempo de la cancion: pulsos transcurridos x duracion de un pulso del loop
      const bpm = animBpm(name);
      // el sufijo es el tempo "de familia": un _075 en una cancion a 150 es medio tiempo, no se acelera al doble.
      // Se ajusta solo dentro de la misma octava (ratio entre 0.71 y 1.41).
      let lt = (t - l.t0) / 1000;
      if (bpm) {
        const songBpm = 60 * (beatsAt(t + 500) - beatsAt(t - 500));   // pulsos en 1 s alrededor de t
        const fam = bpm * 2 ** Math.round(Math.log2(songBpm / bpm));
        lt = (beatsAt(t) - beatsAt(l.t0)) * 60 / fam;
      }
      return { src: 'loop', id, lt, loop: true, name, node: role };
    }
  }
  const idle = IDLE[role] && animId(IDLE[role]);
  return idle ? { src: 'loop', id: idle, lt: t / 1000, loop: true, name: IDLE[role] + ' (generico)', node: role } : null;
}

const tv = new THREE.Vector3(), tq = new THREE.Quaternion();
function wrap(lt, d, loop) {
  const dur = d?.duration || 0;
  if (!(dur > 0)) return 0;
  return loop ? ((lt % dur) + dur) % dur : Math.min(Math.max(lt, 0), dur);
}

// base: pose (o null = pose de enlace); d: animacion (aditiva o no)
function pose(c, base, d, lt, loop) {
  const t = wrap(lt, d, loop);
  c.bones.forEach((b, i) => {
    if (!b) return;
    const br = base?.rot.get(i), bp = base?.pos.get(i), r = d?.rot.get(i), p = d?.pos.get(i);
    if (br) sampleQ(br, 0, b.quaternion); else b.quaternion.copy(c.bindQ[i]);
    if (r) {
      if (d.additive) b.quaternion.multiply(sampleQ(r, t, tq));
      else sampleQ(r, t, b.quaternion);
    }
    b.position.copy(c.bindP[i]);
    if (bp) b.position.add(sampleV(bp, 0, tv));
    if (p) b.position.add(sampleV(p, t, tv));
  });
}

// Fundido entre animaciones (el juego mezcla ~0.25 s al cambiar de loop o clip)
const BLEND = 0.3;
function startBlend(c) {
  c.from = c.bones.map(b => b && [b.quaternion.clone(), b.position.clone()]);
  c.blendT = 0;
}
function blend(c, dt) {
  if (!c.from) return;
  c.blendT += dt;
  const k = Math.min(1, c.blendT / BLEND), s = k * k * (3 - 2 * k);
  if (k >= 1) { c.from = null; return; }
  c.bones.forEach((b, i) => {
    const f = c.from[i]; if (!b || !f) return;
    b.quaternion.slerpQuaternions(f[0], b.quaternion.clone(), s);
    b.position.lerpVectors(f[1], b.position.clone(), s);
  });
}

// Manos al instrumento (clips con ik_target = guitar): el juego lleva la mano derecha al punto de rasgueo
// (hueso 115) y la izquierda al mastil; posicion medida en los loops de guitarra, en el espacio del hueso 112.
const NECK = new THREE.Vector3(0.47, -0.06, 0.06), wp = new THREE.Vector3();
function handsToGuitar(c) {
  const G = c.bones[112], S115 = c.bones[115], L = c.bones[126], R = c.bones[127];
  if (!G || !S115 || !L || !R) return false;
  c.root.updateMatrixWorld(true);
  L.position.copy(L.parent.worldToLocal(G.localToWorld(wp.copy(NECK))));
  R.position.copy(R.parent.worldToLocal(S115.getWorldPosition(wp)));
  return true;
}

// pistas que cuentan para el IK: las de la base y las de la animacion
const tracksOf = (base, d) => ({
  target: b => !!(base?.pos.has(b) || base?.rot.has(b) || d?.pos.has(b) || d?.rot.has(b)),
  keyed: b => !!(d && !d.additive && d.rot.has(b)),
  hasRot: b => !!(base?.rot.has(b) || (d && !d.additive && d.rot.has(b))),
});

let infoTick = 0;
// Precarga: pide ya las animaciones de los proximos segundos para que nadie se quede congelado al empezar un clip
let prefetchT = 0;
function prefetch() {
  for (const role of ROLES) {
    const c = chars[role]; if (!c?.root) continue;
    for (const ahead of [800, 2000, 4000]) { const p = pick(role, S.t + ahead, c); if (p) getAnim(p.src, p.id); }
  }
}

// Pose de un musico en t: animacion (+ base aditiva), IK, manos a la guitarra, captura y retoques de pose.
// Devuelve {p, baseName, waiting}. blendDt: segundos para el fundido entre animaciones (null = sin fundido).
function poseRole(role, c, t, blendDt) {
  const p = pick(role, t, c);
  c.root.position.copy(nodePos(p?.node || role));
  c.root.quaternion.copy(nodeQuat(p?.node || role));
  let d = p ? getAnim(p.src, p.id) : null;
  if (d?.error) { p.name += ' (no encontrada)'; d = null; }
  const baseName = (d?.additive || role === 'drummer') && BASE_POSE[role];
  let base = baseName ? getAnim('loop', animId(baseName)) : null;
  if (base?.error) base = null;
  const waiting = (p && !d && !p.name.includes('(')) || (baseName && !base);
  if (!waiting) {            // mientras carga la siguiente animacion se mantiene la pose anterior
    if (blendDt != null) {
      const key = (p?.id || '') + (p?.node || '') + (p?.inst ?? '');   // cada clip es una instancia (aunque repita animacion)
      if (c.cur !== undefined && c.cur !== key) startBlend(c);
      c.cur = key;
    }
    pose(c, base, d, p?.lt || 0, p?.loop);
    if (blendDt != null) blend(c, blendDt);
    // la bateria trae los brazos animados: solo se cierran las piernas (pies al suelo/pedales)
    const tr = tracksOf(base, d);
    c.ik = solveIK(c, tr, VIEW.ik, role === 'drummer' ? [2, 3] : null);
    // guitarra y bajo: manos al instrumento con las poses de trastes/rasgueo del juego (bucles y clips de guitarra)
    if (VIEW.ik && (role === 'guitarist' || role === 'bassist') && (p?.hands === 'guitar' || p?.loop)) {
      const hn = handsOnGuitar(c, role, blendDt ?? 0);
      if (hn) c.ik += hn;
      else if (p?.hands === 'guitar' && handsToGuitar(c)) c.ik += solveIK(c, { ...tr, hasRot: () => false }, true, [0, 1]);
    }
    if (blendDt != null) applyCapture(role, c);   // captura del movil: cuerpo, cabeza y rostro (rig del juego)
    applyPoseLayers(role, c, t);                   // retoques de pose (claves por hueso encima de todo)
  }
  return { p, baseName, waiting, d, base };
}

function update(dt) {
  if ((prefetchT -= dt) <= 0) { prefetchT = 0.4; prefetch(); }
  group.visible = VIEW.band;
  if (!VIEW.band) return;
  for (const role of ROLES) {
    const c = chars[role]; if (!c?.root) continue;
    const { p, baseName, waiting } = poseRole(role, c, S.t, dt);
    if (window.__band) (window.__band.dbg ||= {})[role] = p && `${p.name} lt=${p.lt.toFixed(3)} node=${p.node}`;
    if (VIEW.skeleton && !c.skel) { c.skel = new THREE.SkeletonHelper(c.root); scene.add(c.skel); editorOnly.add(c.skel); }
    if (c.skel) c.skel.visible = VIEW.skeleton;
    if (!(info[role] || '').startsWith('cargando modelo'))
      info[role] = p ? p.name + (baseName ? ' + ' + baseName : '') + (waiting ? ' (cargando)' : '') : 'pose de enlace';
  }
  if ((infoTick += dt) > 0.25) {
    infoTick = 0;
    $('bandInfo').innerHTML = ROLES.filter(r => chars[r]).map(r => `<div><b>${ROLE_ES[r]}</b> ${info[r] || ''}${chars[r].ik ? ` <span class="muted">IK ${chars[r].ik}</span>` : ''}</div>`).join('');
  }
}

// ---- horneado para exportar: n fotogramas a 30/s desde t0 -> {hueso: {q: [...], p: [...] | null}} locales ----
// Es la misma pose que se ve en el visor (sin fundidos ni captura en vivo). Antes de empezar espera a que esten
// descargadas todas las animaciones del tramo. Los objetivos de IK (93/94 pies, 126/127 manos) se colocan donde
// quedan manos y pies, asi el IK del motor reproduce brazos y piernas.
const waitAnims = async (list, ms = 20000) => {
  const t0 = performance.now();
  for (;;) {
    if (list.every(([src, id]) => getAnim(src, id) !== null)) return true;
    if (performance.now() - t0 > ms) return false;
    await new Promise(r => setTimeout(r, 60));
  }
};
export async function bakeRole(role, t0, n, bones, posBones) {
  const c = chars[role]; if (!c?.root) throw new Error(`el modelo de ${ROLE_ES[role]} no esta cargado`);
  const need = [];
  for (let i = 0; i < n; i += 5) {
    const p = pick(role, t0 + i * 1000 / 30, c);
    if (p) need.push([p.src, p.id]);
  }
  if (BASE_POSE[role]) need.push(['loop', animId(BASE_POSE[role])]);
  if (!await waitAnims(need)) throw new Error('las animaciones del tramo no terminan de cargar');
  const out = {}, pos = new Set(posBones);
  for (let i = 0; i < n; i++) {
    poseRole(role, c, t0 + i * 1000 / 30, null);
    c.root.updateMatrixWorld(true);
    placeIkTargets(c);
    for (const j of bones) {
      const b = c.bones[j]; if (!b) continue;
      const o = (out[j] ||= { q: [], p: pos.has(j) ? [] : null });
      o.q.push(+b.quaternion.x.toFixed(6), +b.quaternion.y.toFixed(6), +b.quaternion.z.toFixed(6), +b.quaternion.w.toFixed(6));
      if (o.p) { const bp = c.bindP[j]; o.p.push(+(b.position.x - bp.x).toFixed(6), +(b.position.y - bp.y).toFixed(6), +(b.position.z - bp.z).toFixed(6)); }
    }
  }
  c.cur = undefined;   // el siguiente fotograma del visor vuelve a posar sin fundido raro
  return out;
}
onFrame(update);
if (new URLSearchParams(location.search).has('debug')) window.__band = { chars, S, update, scene, editCam, THREE };   // pruebas automaticas

