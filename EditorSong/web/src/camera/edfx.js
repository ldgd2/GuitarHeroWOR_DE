// EDFX: lo que WoR no sabe hacer por si mismo y re-wor (el hook) dibuja encima del juego.
//   - efectos de imagen (pista Efectos y efectos marcados en las tomas)
//   - objetos 3D y textos durante toda la cancion: en las tomas del editor con su camara (que ademas el hook escribe
//     en el motor), y en el resto con la camara real del juego, que el hook lee de memoria cada frame
// Se exporta como "<cancion>.edfx" (texto) junto al PAK, mas eventos de sincronia en la pista de eventos:
// el juego los dispara en el ms exacto de la cancion y el hook los reconoce por su QBKey (ver
// re-wor/src/de/edfx.hpp). Lo que si tiene equivalente nativo se traduce al juego:
//   - columna de fuego -> nota 78 de luces (pirotecnia; deducido por estadistica de las canciones base)
//   - objetos NATIVOS ("native ..."): el hook los crea con el propio motor (native_fx.cpp):
//       foco de luz      -> GameObject con carcasa de foco y haz volumetrico (models/lighthousings)
//       columna de fuego -> ademas, particulas de fuego del juego en su posicion exacta
//       particulas       -> CreateParticleSystem con una de las 292 definiciones gp_fx_* del juego
//       modelo del juego -> GameObject con un modelo que el juego tiene en memoria
import * as THREE from 'three';
import { mkStruct, qbkey } from '../core/qb.js';
import { ownTrack, trackActive } from '../timeline/layout.js';
import { propObject } from '../viewport/props.js';
import { COMMON_PARAMS, EFFECTS, fxDefaults } from './presets.js';
import { revealTimes, stripCfg, stripMode, stripShot, stripTime } from './split.js';
import { applyStageCam, stageCamFor } from '../viewport/stagecams.js';
const tmpCam = new THREE.PerspectiveCamera(45, 16 / 9, 0.05, 2000);

const SYNC_EVERY = 2000;          // ms entre eventos de sincronia mientras hay algo que dibujar
const TRI_PER_PROP = 6000, TRI_TOTAL = 40000;
const n3 = v => +(+v).toFixed(4);
const hex8 = (color, a = 1) => new THREE.Color(color).getHexString() + Math.round(Math.max(0, Math.min(1, a)) * 255).toString(16).padStart(2, '0');

// ajustes del efecto (los suyos y los comunes, con sus valores por defecto); los colores van como <clave>r/g/b
// y, en la pantalla dividida, r0..r3 = segundos desde el inicio en que aparece cada franja (al ritmo)
function fxParams(proj, e) {
  const all = { ...fxDefaults(e.type), ...(e.params || {}) }, spec = { ...(EFFECTS[e.type]?.params || {}), ...COMMON_PARAMS };
  const out = {};
  for (const [k, v] of Object.entries(all)) {
    if (!(k in spec)) continue;
    if (spec[k].color) { const c = new THREE.Color(v || '#ffffff'); out[k + 'r'] = c.r; out[k + 'g'] = c.g; out[k + 'b'] = c.b; }
    else if (typeof v === 'number' && isFinite(v)) out[k] = v;
  }
  if (e.type === 'split') revealTimes(proj, e).forEach((at, i) => { out['r' + i] = +(Math.max(0, at - e.t) / 1000).toFixed(3); out['m' + i] = stripMode(stripCfg(e, i)); });
  return out;
}

function effectList(proj, shots) {
  const out = [];
  for (const e of proj.effects || []) if (trackActive(ownTrack(e, 'fx'))) out.push({ type: e.type, t: e.t, dur: e.dur, amount: e.amount ?? 1, params: fxParams(proj, e), e });
  for (const s of shots) for (const [k, on] of Object.entries(s.fx || {})) if (on) out.push({ type: k, t: s.t, dur: s.dur, amount: 1 });
  return out;
}

function propTriangles(p, budget) {
  const o = propObject(p.id); if (!o) return [];
  o.updateMatrixWorld(true);
  const tris = [], a = new THREE.Vector3(), b = new THREE.Vector3(), c = new THREE.Vector3();
  const meshes = [];
  o.traverse(m => {
    if (!m.isMesh || !m.geometry?.attributes?.position) return;
    const mat = [].concat(m.material)[0];
    if (mat?.blending === THREE.AdditiveBlending || mat?.map && p.kind === 'text') return;   // conos de luz, texto (va aparte)
    meshes.push([m, mat]);
  });
  let total = 0;
  for (const [m] of meshes) { const g = m.geometry; total += (g.index ? g.index.count : g.attributes.position.count) / 3; }
  const stride = Math.max(1, Math.ceil(total / budget));
  for (const [m, mat] of meshes) {
    const g = m.geometry, pos = g.attributes.position, idx = g.index;
    const n = (idx ? idx.count : pos.count) / 3;
    const col = hex8(mat?.color || p.color || '#ffffff', mat?.transparent ? mat.opacity ?? 1 : 1);
    for (let i = 0; i < n; i += stride) {
      const [i0, i1, i2] = idx ? [idx.getX(i * 3), idx.getX(i * 3 + 1), idx.getX(i * 3 + 2)] : [i * 3, i * 3 + 1, i * 3 + 2];
      a.fromBufferAttribute(pos, i0).applyMatrix4(m.matrixWorld);
      b.fromBufferAttribute(pos, i1).applyMatrix4(m.matrixWorld);
      c.fromBufferAttribute(pos, i2).applyMatrix4(m.matrixWorld);
      tris.push(`tri ${[a, b, c].flatMap(v => [n3(v.x), n3(v.y), n3(v.z)]).join(' ')} ${col}`);
    }
  }
  return tris;
}


// proj: proyecto del editor; P: copia que se compila (se le anaden eventos y notas); shots: tomas activas;
// worldPose(s, t) -> {p, q, fov} de la camara de la toma en el mundo.
export function buildEdfx(proj, P, shots, worldPose) {
  const song = P.song.name;
  const lines = ['EDFX 1', `song ${song}`];
  const windows = [];
  const fx = effectList(proj, shots);
  for (const f of fx) {
    const extra = f.params ? ' ' + Object.entries(f.params).map(([k, v]) => `${k}=${n3(v)}`).join(' ') : '';
    lines.push(`fx ${f.type} ${Math.round(f.t)} ${Math.round(f.dur)} ${n3(f.amount)}${extra}`); windows.push([f.t, f.t + f.dur]);
    // pantalla dividida: camara propia de cada franja (15 fotogramas por segundo desde que aparece)
    if (f.type === 'split' && f.e) {
      const at = revealTimes(proj, f.e);
      at.forEach((t0, i) => {
        const c = stripCfg(f.e, i);
        if (c.cam !== 'own' && c.cam !== 'perf') return;
        const s = stripShot(f.e, i, t0), sel = c.cam === 'perf' ? stageCamFor(c.perf ?? 0, t0) : null;
        if (c.cam === 'perf' && !sel?.cam) return;
        lines.push(`scam ${i} ${Math.round(t0)}`);
        for (let tl = 0; t0 + tl <= f.t + f.dur + 1; tl += 1000 / 15) {
          let W;
          if (sel) {   // camara del escenario: la misma que en la vista previa, a 16:9
            applyStageCam(tmpCam, sel, [t0, f.t + f.dur - t0, c.perf], t0 + tl, 16 / 9);
            W = { p: tmpCam.position, q: tmpCam.quaternion, fov: THREE.MathUtils.radToDeg(2 * Math.atan(Math.tan(THREE.MathUtils.degToRad(tmpCam.fov) / 2) * 16 / 9)) };
          } else W = worldPose(s, stripTime(s, t0 + tl));
          lines.push(`k ${Math.round(tl)} ${n3(W.p.x)} ${n3(W.p.y)} ${n3(W.p.z)} ${n3(W.q.x)} ${n3(W.q.y)} ${n3(W.q.z)} ${n3(W.q.w)} ${n3(W.fov)}`);
        }
      });
    }
  }

  const props = (proj.props || []).filter(p => trackActive(ownTrack(p, 'props')));
  // objetos sin tiempo (anadidos desde el panel): toda la cancion
  const pt = p => p.t ?? 0, pdur = p => p.t == null ? (proj.length || 600000) : (p.dur ?? 8000);
  // fuego: traduccion nativa (pirotecnia del juego) en su inicio y cada 4 s
  let pyro = 0;
  for (const p of props.filter(x => x.kind === 'pyro')) {
    // con tiempo: al empezar y cada 4 s; sin tiempo (toda la cancion): al empezar cada toma del editor
    const at = p.t == null ? shots.map(s => s.t) : Array.from({ length: Math.ceil(pdur(p) / 4000) }, (_, i) => pt(p) + i * 4000);
    for (const t of at) { (P.notes.lightshow_notes ||= []).push([Math.round(t), 78, 100, 100]); pyro++; }
  }
  if (pyro) P.notes.lightshow_notes.sort((x, y) => x[0] - y[0]);

  // objetos nativos: los crea el motor del juego
  // linea: native <tipo> <t> <dur> x y z qx qy qz qw r g b v0 v1 v2 <recurso>
  const fixSpot = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(1, 0, 0), Math.PI / 2);   // haz del motor (+Z) -> el del editor (-Y)
  let natives = 0;
  const nativeLine = (kind, p, q, rgb, v, asset) => {
    const pos = p.pos || [0, 0, 0];
    lines.push(`native ${kind} ${Math.round(pt(p))} ${Math.round(pdur(p))} ${pos.map(n3).join(' ')} ${[q.x, q.y, q.z, q.w].map(n3).join(' ')} ${rgb.map(n3).join(' ')} ${v.map(n3).join(' ')} ${asset}`);
    windows.push([pt(p), pt(p) + pdur(p)]);
    natives++;
  };
  for (const p of props) {
    const rot = (p.rot || [0, 0, 0]).map(THREE.MathUtils.degToRad);
    const q = new THREE.Quaternion().setFromEuler(new THREE.Euler(rot[0], rot[1], rot[2]));
    const c = new THREE.Color(p.color || '#ffffff'), rgb = [c.r, c.g, c.b];
    if (p.kind === 'spot') nativeLine('light', p, q.clone().multiply(fixSpot), rgb, [p.range ?? 8, p.radius ?? 1.2, p.housing ?? 0], 'foco');
    else if (p.kind === 'pyro') { nativeLine('pfx', p, q, rgb, [0, 0, 0], 'gp_fx_fireplume_01'); nativeLine('pfx', p, q, rgb, [0, 0, 0], 'gp_fx_fireplume_bottom_01'); }
    else if (p.kind === 'particles') nativeLine('pfx', p, q, rgb, [0, 0, 0], p.pfx || 'gp_fx_sparkfountain_sparks_01');
    else if (p.kind === 'gamemodel' && p.model) nativeLine('model', p, q, rgb, [0, 0, 0], p.model.replace(/\s+/g, '_'));
  }

  // objetos y textos: toda su duracion. Todas las tomas van con su camara: el hook la usa para dibujar y la
  // escribe en el motor mientras dura la toma.
  const visual = props.filter(p => !['pyro', 'spot', 'particles', 'gamemodel'].includes(p.kind));
  const used = shots;
  for (const s of used) {
    lines.push(`shot ${Math.round(s.t)} ${Math.round(s.dur)}`);
    for (let tl = 0; tl <= s.dur + 1; tl += 1000 / Math.min(60, Math.max(5, s.fps || 15))) {
      const W = worldPose(s, s.t + Math.min(tl, s.dur));
      lines.push(`k ${Math.round(tl)} ${n3(W.p.x)} ${n3(W.p.y)} ${n3(W.p.z)} ${n3(W.q.x)} ${n3(W.q.y)} ${n3(W.q.z)} ${n3(W.q.w)} ${n3(W.fov)}`);
    }
  }
  let triTotal = 0, skipped = 0;
  for (const p of visual) {
    windows.push([pt(p), pt(p) + pdur(p)]);
    if (p.kind === 'text') {
      const sc = p.scale || [1, 1, 1];
      lines.push(`text ${Math.round(pt(p))} ${Math.round(pdur(p))} ${(p.pos || [0, 0, 0]).map(n3).join(' ')} ${n3(0.32 * sc[1])} ${hex8(p.color || '#ffffff')} ${(p.text || 'TEXTO').replace(/\s+/g, ' ')}`);
      continue;
    }
    const tris = propTriangles(p, Math.min(TRI_PER_PROP, TRI_TOTAL - triTotal));
    if (!tris.length) continue;
    triTotal += tris.length;
    lines.push(`prop ${Math.round(pt(p))} ${Math.round(pdur(p))}`, ...tris);
  }

  // eventos de sincronia: al empezar cada tramo, cada 2 s y al acabar
  const times = [];
  for (const s of used) windows.push([s.t, s.t + s.dur]);
  for (const [a, b] of windows) { for (let t = a; t < b; t += SYNC_EVERY) times.push(Math.round(t)); times.push(Math.round(b)); }
  const uniq = [...new Set(times)].sort((x, y) => x - y).filter((t, i, arr) => i === 0 || t - arr[i - 1] >= 250);
  uniq.forEach((t, i) => {
    const key = qbkey(`edfx_${song}_${i}`);
    lines.push(`sync ${key.toString(16).padStart(8, '0')} ${t}`);
    P.events.push(mkStruct([['int', 'time', t], ['qbkey', 'scr', key], ['struct', 'params', mkStruct([])]]));
  });
  const info = { fx: fx.length, shots: used.length, props: visual.length - skipped, skipped, pyro, natives, sync: uniq.length };
  return { text: lines.length > 2 ? lines.join('\n') + '\n' : '', info };
}
