// Camara de programa: lo que veria el jugador en cada momento.
// Camara de clip (.ska de 3 huesos): hueso 0 = cambio de ejes, hueso 1 = camara, relativa al nodo del musico.
// Con las rotaciones conjugadas (ver editor/formats/ska.py) la camara mira hacia +Z de R0*R1 (medido sobre 73
// camaras: el eje +Z apunta al musico); three.js mira hacia -Z, de ahi el giro de 180 grados.
import * as THREE from 'three';
import { $, S } from '../core/state.js';
import { evParams, hx, nameOf, val } from '../core/qb.js';
import { clipSlotOf, ROLE_ES, roleOfNode } from '../core/roles.js';
import { activeClips, clipCams, cutAt } from '../song/clips.js';
import { VIEW } from '../core/view.js';
import { getAnim, sampleQ, sampleV } from './anim.js';
import { mon, onFrame, progCam, setMonitorSplit } from './scene.js';
import { revealTimes, splitParams, stripCfg, stripRect, stripShot, stripTime } from '../camera/split.js';
import { nodePos, nodeQuat } from './venue.js';
import { applyStageCam, stageCamFor } from './stagecams.js';
import { shotAt, worldPose } from '../camera/shots.js';
import { PRESETS } from '../camera/presets.js';
import { applyFx, shakeOf } from './fx.js';

const q0 = new THREE.Quaternion(), q1 = new THREE.Quaternion(), p = new THREE.Vector3();
const UP = new THREE.Vector3(0, 1.3, 0);

function clipCamera(t, slot) {
  for (const a of activeClips(t)) {
    const cam = clipCams(a.def).find(c => val(c, 'slot', -1) === slot); if (!cam) continue;
    const id = hx(val(cam, 'anim', 0)), d = getAnim('song', id);
    if (!d) return 'cargando animacion de camara...';
    if (d.error) return `<span class="err">camara ${id}: ${d.error}</span>`;
    const role = roleOfNode(val(cam, 'name', '')), base = nodePos(role), nq = nodeQuat(role);
    const lt = Math.min(Math.max((t - a.t0) / 1000 + a.sp.sf / 30, 0), d.duration || 1e9);
    d.rot.get(0) ? sampleQ(d.rot.get(0), lt, q0) : q0.identity();
    d.rot.get(1) ? sampleQ(d.rot.get(1), lt, q1) : q1.identity();
    d.pos.get(1) ? sampleV(d.pos.get(1), lt, p) : p.set(0, 0, 0);
    progCam.position.copy(base).add(p.applyQuaternion(q0).applyQuaternion(nq));
    if (VIEW.axis === 'look') progCam.lookAt(base.clone().add(UP));
    else {
      progCam.quaternion.copy(nq).multiply(q0).multiply(q1);
      if (VIEW.axis === 'flip') progCam.rotateY(Math.PI);
    }
    const fov = fovAt(d.fov, lt);   // horizontal; algunas camaras lo animan (zoom)
    const asp = mon.clientWidth / Math.max(1, mon.clientHeight);
    progCam.fov = THREE.MathUtils.radToDeg(2 * Math.atan(Math.tan(THREE.MathUtils.degToRad(fov) / 2) / asp));
    return `Clip «${nameOf(val(evParams(a.ev), 'clip', 0))}» &middot; camara ${slot + 1} &middot; sigue a ${ROLE_ES[role]} &middot; FOV ${fov.toFixed(0)}&deg;`;
  }
  return null;
}

// FOV de una camara .ska en el instante lt (claves [t, grados]; interpolacion lineal)
function fovAt(keys, lt) {
  if (!keys.length) return 72;
  if (lt <= keys[0][0]) return keys[0][1];
  for (let i = 1; i < keys.length; i++) if (lt <= keys[i][0]) {
    const [t0, f0] = keys[i - 1], [t1, f1] = keys[i];
    return f0 + (f1 - f0) * (t1 > t0 ? (lt - t0) / (t1 - t0) : 0);
  }
  return keys.at(-1)[1];
}

let lastFx = {};
onFrame(() => {
  if (!S.proj) return;
  const asp0 = mon.clientWidth / Math.max(1, mon.clientHeight);
  // 1. tomas del editor (se compilan como clip + camara propia): mandan sobre todo lo demas
  const sh = shotAt(S.t);
  lastFx = applyFx(sh, S.t);
  if (sh) {
    const W = worldPose(sh, S.t);
    progCam.position.copy(W.p); progCam.quaternion.copy(W.q);
    progCam.fov = THREE.MathUtils.radToDeg(2 * Math.atan(Math.tan(THREE.MathUtils.degToRad(W.fov) / 2) / asp0));
    $('monInfo').innerHTML = `Toma ${sh.id} &middot; ${PRESETS[sh.preset]?.label || sh.preset} &middot; ${ROLE_ES[sh.role]} &middot; FOV ${W.fov.toFixed(0)}&deg;`;
    return;
  }
  const t = S.t, mc = cutAt(S.proj.cameras.moment, t), ac = cutAt(S.proj.cameras.auto, t);
  const slot = mc ? clipSlotOf(mc[2]) : null;
  let info = slot !== null ? clipCamera(t, slot) : null;
  const placed = info && !info.startsWith('cargando') && !info.includes('class="err"');
  if (slot !== null && !info) info = `<span class="err">Camara del clip ${slot + 1} sin ningun clip sonando que la tenga: en el juego cae bajo el escenario</span>`;
  if (!placed) {
    // plano de escenario: la camara real del escenario para ese numero (si no, un plano general)
    const cut = (slot === null && mc) ? mc : ac;
    const sel = cut ? stageCamFor(cut[2], cut[0]) : null;
    const asp = mon.clientWidth / Math.max(1, mon.clientHeight);
    const txt = sel?.cam ? applyStageCam(progCam, sel, cut, t, asp) : null;
    if (txt) info ||= `Escenario · corte ${cut[2]} · ${txt}`;
    else {
      const v = nodePos('vocalist');
      progCam.position.set(v.x, 2.2, v.z + 9); progCam.lookAt(v.x, 1.2, v.z - 1); progCam.fov = 45;
      info ||= cut ? `Corte ${cut[2]}${sel?.name ? ' (' + sel.name + ')' : ''}: este escenario no trae esa camara; plano general`
        : 'Sin corte de camara';
    }
  }
  $('monInfo').innerHTML = info;
});

// temblor de imagen de la pista de efectos: se suma a cualquier camara (despues de colocarla)
onFrame(() => {
  const sk = S.proj && shakeOf(lastFx, S.t);
  if (sk) { progCam.rotateX(sk[0]); progCam.rotateY(sk[1]); progCam.rotateZ(sk[2] * 0.5); }
});

// Pantalla dividida en el monitor: cada franja se dibuja EN VIVO con su camara (la suya propia, o la del juego
// congelada en el momento en que aparece la franja)
const stripCams = Array.from({ length: 4 }, () => new THREE.PerspectiveCamera(45, 1, 0.05, 2000));
const frozen = new Map();
const D2R = THREE.MathUtils.degToRad, R2D = THREE.MathUtils.radToDeg;
setMonitorSplit(asp => {
  const sp = lastFx.split, e = sp?.e;
  if (!S.proj || !e) { frozen.clear(); return null; }
  const p = splitParams(e), n = Math.max(2, Math.min(4, p.n | 0)), at = revealTimes(S.proj, e);
  const W = mon.clientWidth, H = Math.max(1, mon.clientHeight), out = [];
  const progH = 2 * Math.atan(Math.tan(D2R(progCam.fov) / 2) * asp);   // FOV horizontal del monitor
  for (let i = 0; i < n; i++) {
    const key = e.id + ':' + i;
    if (S.t < at[i]) { frozen.delete(key); continue; }
    const cfg = stripCfg(e, i), cam = stripCams[i];
    let hfov;
    const r0 = stripRect(n, p.dir, i), sa0 = (r0[2] * W) / Math.max(1, r0[3] * H);
    if (cfg.cam === 'own') {
      const s = stripShot(e, i, at[i]), Wp = worldPose(s, stripTime(s, S.t));
      cam.position.copy(Wp.p); cam.quaternion.copy(Wp.q); hfov = D2R(Wp.fov);
    } else if (cfg.cam === 'perf') {
      const sel = stageCamFor(cfg.perf ?? 0, at[i]);
      if (!sel?.cam || !applyStageCam(cam, sel, [at[i], e.t + e.dur - at[i], cfg.perf], S.t, sa0)) continue;
      hfov = 2 * Math.atan(Math.tan(D2R(cam.fov) / 2) * sa0);
    } else if (cfg.cam === 'live') {
      cam.position.copy(progCam.position); cam.quaternion.copy(progCam.quaternion); hfov = progH;
    } else {
      if (!frozen.has(key)) frozen.set(key, { p: progCam.position.clone(), q: progCam.quaternion.clone(), h: progH });
      const f = frozen.get(key); cam.position.copy(f.p); cam.quaternion.copy(f.q); hfov = f.h;
    }
    const r = stripRect(n, p.dir, i), clip = r.slice();
    if (p.mode === 1) { const k = Math.min(1, (S.t - at[i]) / 300); if (p.dir === 1) clip[3] *= Math.max(0.02, k); else clip[2] *= Math.max(0.02, k); }
    const sa = (r[2] * W) / Math.max(1, r[3] * H);
    cam.aspect = sa; cam.fov = R2D(2 * Math.atan(Math.tan(hfov / 2) / sa)); cam.updateProjectionMatrix();
    out.push({ rect: r, clip, cam });
  }
  return out;
});
