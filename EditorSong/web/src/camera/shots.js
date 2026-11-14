// Tomas de camara del editor (S.proj.shots): se ven en el monitor y, al compilar, se convierten en lo que el
// juego entiende (ver materialize):
//   1. una camara .ska nueva generada fotograma a fotograma (POST /api/camera/make)
//   2. un clip nuevo con esa camara en el slot 0, anclada al nodo del musico, y un musico con su loop actual
//      (el juego no tiene clips sin musicos: 0 de 527 en las canciones del juego)
//   3. el evento Band_PlayClip y el corte de la pista de momento numero 3 (= camara del slot 0)
// Conversion al espacio del archivo, inversa exacta de la que usa el monitor (viewport/program.js):
//   mundo = nodo + Rnodo * (q0 * p1)         -> p1 = q0^-1 * pos_local
//   rot   = Rnodo * q0 * q1 * giro180(Y)      -> q1 = q0^-1 * rot_local * giro180^-1 ; archivo = conjugado(q1)
import * as THREE from 'three';
import { api, S } from '../core/state.js';
import { hx, K, mkStruct, qbkey, registerName, val } from '../core/qb.js';
import { clipSlotOf, NODE_OF, ROLES } from '../core/roles.js';
import { beatsAt, loopAt } from '../song/clips.js';
import { nodePos, nodeQuat } from '../viewport/venue.js';
import { FRAMINGS, PRESETS, ROLE_AIM } from './presets.js';
import { shotPriority, shotTrack, trackActive } from '../timeline/layout.js';
import { buildEdfx } from './edfx.js';
import { activeFx, shakeOf } from '../viewport/fx.js';

export const shots = () => (S.proj.shots ||= []);
let nextId = 1;
export function newShot(t, role = 'guitarist', preset = 'mano', framing = 'medio') {
  const f = FRAMINGS[framing];
  const id = Math.max(nextId, ...shots().map(s => s.id + 1)); nextId = id + 1;
  return { id, t: Math.round(t), dur: 3000, role, preset, framing,
    p: { dist: f.dist, azim: 20, height: f.height, fov: f.fov, aim: ROLE_AIM[role], amount: 1, roll: 18 }, fx: {} };
}
// toma que se ve en t: la de la pista de tomas mas arriba (pistas apagadas no cuentan); en la misma pista, la ultima
export const shotAt = t => {
  if (!S.proj?.shots?.length) return null;
  const order = shotPriority();
  let best = null, bp = 1e9;
  for (const s of S.proj.shots) {
    if (t < s.t || t >= s.t + s.dur) continue;
    const tr = shotTrack(s); if (!trackActive(tr)) continue;
    const pr = order.indexOf(tr);
    if (pr < bp || (pr === bp && s.t >= best.t)) { best = s; bp = pr; }
  }
  return best;
};

// ruido determinista (mismo resultado en la vista previa y al compilar)
function noise(t, seed, freqs) {
  let v = 0;
  freqs.forEach(([f, a], i) => { v += a * Math.sin(t * f + seed * 1.37 + i * 2.1) * Math.cos(t * f * 0.53 + seed * 0.71 + i); });
  return v;
}
const up = new THREE.Vector3(0, 1, 0), m4 = new THREE.Matrix4(), e3 = new THREE.Euler();

// pose local (espacio del musico) en el instante t (ms de la cancion)
export function localPose(s, t) {
  const u = Math.min(1, Math.max(0, (t - s.t) / Math.max(1, s.dur))), ts = (t - s.t) / 1000;
  const P = s.p, r = { dist: P.dist, azim: P.azim, height: P.height, fov: P.fov, lat: 0, pan: 0, tilt: 0, roll: 0, hand: 0, shake: 0, aim: P.aim };
  (PRESETS[s.preset] || PRESETS.fija).run(r, u, P, beatsAt(t));
  const a = THREE.MathUtils.degToRad(r.azim);
  const pos = new THREE.Vector3(Math.sin(a) * r.dist + Math.cos(a) * r.lat, r.height, Math.cos(a) * r.dist - Math.sin(a) * r.lat);
  const target = new THREE.Vector3(Math.cos(a) * r.lat * 0.6, r.aim, -Math.sin(a) * r.lat * 0.6);
  // mano: balanceo lento; temblor: alta frecuencia
  const h = r.hand, k = r.shake, sd = s.id;
  pos.x += h * noise(ts, sd, [[0.9, 0.012], [2.1, 0.004]]) + k * noise(ts, sd + 9, [[23, 0.03], [37, 0.015]]);
  pos.y += h * noise(ts, sd + 3, [[0.7, 0.01], [1.9, 0.003]]) + k * noise(ts, sd + 11, [[29, 0.03], [41, 0.012]]);
  pos.z += h * noise(ts, sd + 5, [[0.5, 0.01]]);
  m4.lookAt(pos, target, up);
  const q = new THREE.Quaternion().setFromRotationMatrix(m4);
  e3.set(THREE.MathUtils.degToRad(r.tilt + h * noise(ts, sd + 7, [[1.1, 0.5], [2.7, 0.15]]) + k * noise(ts, sd + 13, [[31, 2.2]])),
    THREE.MathUtils.degToRad(r.pan + h * noise(ts, sd + 8, [[0.8, 0.6]]) + k * noise(ts, sd + 14, [[27, 2.5]])),
    THREE.MathUtils.degToRad(r.roll + h * noise(ts, sd + 2, [[0.6, 0.35]]) + k * noise(ts, sd + 15, [[33, 1.5]])), 'YXZ');
  q.multiply(new THREE.Quaternion().setFromEuler(e3));
  return { pos, q, fov: Math.min(150, Math.max(5, r.fov)), u };
}

// ---------- animacion personalizada (keyframes) ----------
// s.keys = [{t: ms desde el inicio de la toma, p: [x,y,z] mundo, q: [x,y,z,w] (three, mira a -Z), fov: horizontal}]
// s.interp: 'smooth' (Catmull-Rom en posicion, slerp suavizado) | 'linear'
const _qa = new THREE.Quaternion(), _qb = new THREE.Quaternion();
const V = a => new THREE.Vector3(a[0], a[1], a[2]);
export function keyPose(s, t) {
  const K = s.keys, rt = t - s.t, u = Math.min(1, Math.max(0, rt / Math.max(1, s.dur)));
  if (rt <= K[0].t || K.length === 1) return { p: V(K[0].p), q: new THREE.Quaternion(...K[0].q), fov: K[0].fov, u };
  const L = K.at(-1);
  if (rt >= L.t) return { p: V(L.p), q: new THREE.Quaternion(...L.q), fov: L.fov, u };
  let i = 1; while (i < K.length - 1 && K[i].t < rt) i++;
  const a = K[i - 1], b = K[i];
  let f = (rt - a.t) / Math.max(1e-6, b.t - a.t);
  let p;
  if (s.interp === 'linear') p = V(a.p).lerp(V(b.p), f);
  else {
    const p0 = V((K[i - 2] || a).p), p1 = V(a.p), p2 = V(b.p), p3 = V((K[i + 1] || b).p);
    const f2 = f * f, f3 = f2 * f;
    p = p1.clone().multiplyScalar(2).add(p2.clone().sub(p0).multiplyScalar(f))
      .add(p0.clone().multiplyScalar(2).sub(p1.clone().multiplyScalar(5)).add(p2.clone().multiplyScalar(4)).sub(p3).multiplyScalar(f2))
      .add(p1.clone().multiplyScalar(3).sub(p0).sub(p2.clone().multiplyScalar(3)).add(p3).multiplyScalar(f3)).multiplyScalar(0.5);
    if (b.t - a.t > 120) f = f * f * (3 - 2 * f);   // entre claves separadas, giro suave (las grabadas del movil van densas)
  }
  _qa.set(...a.q); _qb.set(...b.q);
  return { p, q: _qa.clone().slerp(_qb, f), fov: a.fov + (b.fov - a.fov) * f, u };
}

// pose en el mundo (para el monitor)
export function worldPose(s, t) {
  if (s.preset === 'keys' && s.keys?.length) return keyPose(s, t);
  const L = localPose(s, t), nq = nodeQuat(s.role);
  return { p: L.pos.clone().applyQuaternion(nq).add(nodePos(s.role)), q: nq.clone().multiply(L.q), fov: L.fov, u: L.u };
}

// ---------- compilacion ----------
const Q0inv = new THREE.Quaternion(0.5, 0.5, 0.5, 0.5).invert();
const FLIPinv = new THREE.Quaternion().setFromAxisAngle(up, Math.PI).invert();
export const shotName = s => `es_cam_${S.proj.song.name.replace(/[^a-z0-9]/gi, '').slice(0, 20)}_${s.id}`;

// pose en el espacio del musico a partir de la pose en el MUNDO: vale para presets, keyframes y lo grabado con el movil
// (antes se usaba localPose, que no conoce las claves: el juego recibia una camara quieta en vez de la animacion)
function poseLocal(s, t) {
  const W = worldPose(s, t), nqi = nodeQuat(s.role).clone().invert();
  return { pos: W.p.clone().sub(nodePos(s.role)).applyQuaternion(nqi), q: nqi.multiply(W.q), fov: W.fov };
}

export function bakeKeys(s) {
  const n = Math.max(2, Math.round(s.dur / 1000 * 30)), keys = [], fov = [];
  let lastF = null;
  for (let i = 0; i <= n; i++) {
    const tl = i / 30, L = poseLocal(s, s.t + tl * 1000);
    // temblor de imagen (efecto shake) horneado en la camara, igual que en el monitor
    const sh = shakeOf(activeFx(s.t + tl * 1000, s), s.t + tl * 1000);
    if (sh) L.q.multiply(new THREE.Quaternion().setFromEuler(new THREE.Euler(sh[0], sh[1], sh[2])));
    const p1 = L.pos.clone().applyQuaternion(Q0inv);
    const q1 = Q0inv.clone().multiply(L.q).multiply(FLIPinv);
    keys.push([+tl.toFixed(5), p1.x, p1.y, p1.z, -q1.x, -q1.y, -q1.z, q1.w]);   // archivo = conjugado
    const f = +L.fov.toFixed(3);
    if (lastF === null || Math.abs(f - lastF) > 0.05 || i === n) { fov.push([+tl.toFixed(5), f]); lastF = f; }
  }
  return { keys, fov };
}

async function loopFor(role, t) {
  // musico que va en el clip: el del plano (si tiene loop); la bateria toca segun las notas, asi que para
  // ella se usa otro musico con loop y la camara sigue anclada a la bateria
  for (const r of [role, ...ROLES.filter(x => x !== role && x !== 'drummer')]) {
    if (r === 'drummer') continue;
    const l = loopAt(r, t);
    if (l?.male) return { role: r, anim: l.male };
  }
  return null;
}

// Copia del proyecto con las tomas convertidas a camaras/clips/eventos del juego (no modifica S.proj).
export async function materialize(proj, onProgress) {
  const P = JSON.parse(JSON.stringify(proj)), list = (proj.shots || []).filter(s => trackActive(shotTrack(s)));
  const warnings = [];
  for (const [n, s] of list.entries()) {
    onProgress?.(n, list.length);
    const name = shotName(s);
    const { keys, fov } = bakeKeys(s);
    const ska = await api('/api/camera/make', { method: 'POST', body: JSON.stringify({ name, keys, fov }) });
    P.skas = P.skas.filter(x => x.id !== ska.id).concat([ska]);
    const who = await loopFor(s.role, s.t);
    if (!who) { warnings.push(`Toma ${n + 1}: no hay ningun musico con loop en ese momento; se omite.`); continue; }
    let tf = 1;
    try { const a = await api('/api/band/anim?id=' + hx(who.anim)); if (a.duration > 0) tf = Math.min(1, a.duration / (s.dur / 1000)); } catch { /* sin datos */ }
    const frames = Math.max(2, Math.round(s.dur / 1000 * 30));
    const clipName = name + '_clip', ck = qbkey(clipName); registerName(ck, clipName);
    const ik = who.role === 'vocalist' ? K.slave : K.guitar;
    const ch = [['qbkey', 'name', K[who.role]], ['string', 'startnode', who.role + '_start'], ['qbkey', 'anim', who.anim],
      ['int', 'startframe', 0], ['int', 'endframe', frames], tf === 1 ? ['int', 'timefactor', 1] : ['float', 'timefactor', +tf.toFixed(4)],
      ['qbkey', 'ik_targetl', ik], ['qbkey', 'ik_targetr', ik], ['qbkey', 'strum', K.true], ['qbkey', 'fret', K.true], ['qbkey', 'chord', K.true]];
    P.clips[hx(ck)] = mkStruct([['array', 'characters', { elem: 'struct', values: [mkStruct(ch)] }],
      ['array', 'cameras', { elem: 'struct', values: [mkStruct([['int', 'slot', 0], ['string', 'name', NODE_OF[s.role]], ['qbkey', 'anim', parseInt(ska.id, 16)]])] }],
      ['array', 0x65C1ECD3, { elem: 'empty', values: [] }]]);
    P.events.push(mkStruct([['int', 'time', s.t], ['qbkey', 'scr', K.Band_PlayClip], ['struct', 'params', mkStruct([
      ['qbkey', 'clip', ck], ['int', 'startframe', 0], ['int', 'endframe', frames]])]]));
    // corte de momento 3 (= slot 0) durante la toma; los cortes de momento que se solapan se recortan
    const a = s.t, b = s.t + s.dur, mom = [];
    for (const c of P.cameras.moment) {
      const c0 = c[0], c1 = c[0] + c[1];
      if (c1 <= a || c0 >= b) { mom.push(c); continue; }
      if (c0 < a) mom.push([c0, a - c0, c[2]]);
      // el resto de un corte de camara de clip se queda sin su clip (el nuestro lo sustituye): plano medio
      if (c1 > b) mom.push([b, c1 - b, clipSlotOf(c[2]) !== null ? 29 : c[2]]);
    }
    mom.push([a, Math.min(65535, s.dur), 3]);
    // cortes de clip posteriores cuyo clip mas reciente es el nuestro (ya terminado): sin clip -> plano medio
    const clipStarts = P.events.filter(ev => val(ev, 'scr', 0) >>> 0 === K.Band_PlayClip).map(ev => val(ev, 'time', 0));
    for (const c of mom) {
      if (c[0] < b || clipSlotOf(c[2]) === null) continue;
      const last = Math.max(-1, ...clipStarts.filter(x => x <= c[0]));
      if (last === a) c[2] = 29;
    }
    P.cameras.moment = mom.sort((x, y) => x[0] - y[0]);
  }
  // efectos, objetos y textos: archivo .edfx para re-wor + eventos de sincronia; fuego -> pirotecnia
  const fx = buildEdfx(proj, P, list, worldPose);
  P.edfx = fx.text;
  delete P.shots; delete P.props; delete P.effects; delete P.layout;
  P.events.sort((x, y) => val(x, 'time', 0) - val(y, 'time', 0));
  return { proj: P, warnings, edfx: fx.info };
}
