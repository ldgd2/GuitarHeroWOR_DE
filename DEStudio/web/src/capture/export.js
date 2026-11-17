// Captura -> lo que entiende Guitar Hero (se hace al compilar, sobre la copia del proyecto que va al PAK):
//  * Cara: animaciones faciales parciales (huesos b063..b092, y cuello/cabeza en el modo cabeza y rostro) de hasta
//    10 s, y eventos {time, scr: Band_PlayFacialAnim, params: {name: VOCALIST, anim}} en <cancion>_facial, igual
//    que las canciones del juego (gh6_singer_male_<cancion>_N). Los eventos de cara de ese musico que caen dentro
//    de lo grabado se sustituyen.
//  * Cuerpo: animaciones de clip (pelvis, columna, brazos, muslos, pies y objetivos de IK) de hasta 10 s, un clip
//    por trozo con ese musico y su evento Band_PlayClip. Los clips de ese musico dentro de lo grabado se quitan.
import { api } from '../core/state.js';
import { evParams, evScr, hx, K, mkStruct, qbkey, registerName, val } from '../core/qb.js';
import { clipSlotOf } from '../core/roles.js';
import { charOf } from '../viewport/band.js';
import { bakeBody, bakeFace } from '../rig/apply.js';
import { BLEND } from './channels.js';
import { capCfg, takes } from './capture.js';

const CHUNK = 300;                 // fotogramas a 30/s por animacion (10 s, como las faciales cortas del juego)
const FPS = 30;

// trozos de hasta 10 s; cada uno empieza en el ultimo fotograma del anterior (sin huecos entre animaciones)
function chunks(tk) {
  const total = Math.max(2, Math.floor((tk.t1 - tk.t0) / (1000 / FPS)) + 1), out = [];
  for (let s = 0; s < total - 1; s += CHUNK) out.push({ t: Math.round(tk.t0 + s * 1000 / FPS), n: Math.min(CHUNK + 1, total - s) });
  return out;
}
const inTakes = (list, t) => list.some(tk => t >= tk.t0 - 1 && t <= tk.t1 + 1);
const safe = s => s.replace(/[^A-Za-z0-9_]/g, '').slice(0, 22);

async function makeAnim(P, name, n, bones) {
  const ska = await api('/api/anim/make', { method: 'POST', body: JSON.stringify({ name, n, bones }) });
  P.skas = (P.skas || []).filter(x => x.id !== ska.id).concat([ska]);
  const key = parseInt(ska.id, 16); registerName(key, name);
  return key;
}

export async function materializeCapture(P, warnings) {
  const res = { face: 0, body: 0 };
  const cap = P.capture; if (!cap) return res;
  const song = safe(P.song?.name || 'song');
  // ---- cara ----
  const fT = cap.face?.frames?.length ? takes('face') : [];
  if (fT.length) {
    const cfg = capCfg('face'), role = cfg.role, c = charOf(role);
    if (!c?.root || !c.faceH) warnings.push(`Cara: el modelo de ${role} no esta cargado en el visor (pestana Banda); no se exporta.`);
    else if (!Array.isArray(P.facial)) warnings.push('Cara: no se pudo leer el array de caras de la cancion; vuelve a abrirla y compila otra vez.');
    else {
      const evs = [];
      for (const tk of fT) {
        const fr = cap.face.frames.slice(tk.i0, tk.i1 + 1);
        const withHead = cfg.exportHead !== false && fr.some(f => typeof f.v[BLEND.length] === 'number');
        for (const ch of chunks(tk)) {
          const key = await makeAnim(P, `es_face_${song}_${role.slice(0, 3)}_${ch.t}`, ch.n, bakeFace(c, ch.t, ch.n, cfg, withHead));
          evs.push(mkStruct([['int', 'time', ch.t], ['qbkey', 'scr', K.Band_PlayFacialAnim],
            ['struct', 'params', mkStruct([['qbkey', 'name', K[role]], ['qbkey', 'anim', key]])]]));
          res.face++;
        }
      }
      P.facial = P.facial.filter(ev => !(evScr(ev) === K.Band_PlayFacialAnim && val(evParams(ev), 'name', 0) >>> 0 === K[role]
        && inTakes(fT, val(ev, 'time', 0)))).concat(evs).sort((a, b) => val(a, 'time', 0) - val(b, 'time', 0));
    }
  }
  // ---- cuerpo ----
  const bT = cap.body?.frames?.length ? takes('body') : [];
  if (bT.length) {
    const cfg = capCfg('body'), role = cfg.role, c = charOf(role);
    if (!c?.root || !c.bodyChar) warnings.push(`Cuerpo: el modelo de ${role} no esta cargado en el visor (pestana Banda); no se exporta.`);
    else {
      // fuera los clips de ese musico que empiezan dentro de lo grabado
      const hasRole = ck => (P.clips[hx(ck)] ? (val(P.clips[hx(ck)], 'characters')?.values || []).some(ch => val(ch, 'name', 0) >>> 0 === K[role]) : false);
      P.events = P.events.filter(ev => !(evScr(ev) === K.Band_PlayClip && inTakes(bT, val(ev, 'time', 0)) && hasRole(val(evParams(ev), 'clip', 0))));
      for (const tk of bT) {
        for (const ch of chunks(tk)) {
          const name = `es_body_${song}_${role.slice(0, 3)}_${ch.t}`;
          const key = await makeAnim(P, name, ch.n, bakeBody(c, ch.t, ch.n, cfg, cap.body.neutral));
          const frames = ch.n - 1, clipName = name + '_clip', ck = qbkey(clipName); registerName(ck, clipName);
          const chr = [['qbkey', 'name', K[role]], ['string', 'startnode', role + '_start'], ['qbkey', 'anim', key],
            ['int', 'startframe', 0], ['int', 'endframe', frames], ['int', 'timefactor', 1],
            ['qbkey', 'ik_targetl', K.slave], ['qbkey', 'ik_targetr', K.slave], ['qbkey', 'strum', K.true], ['qbkey', 'fret', K.true], ['qbkey', 'chord', K.true]];
          P.clips[hx(ck)] = mkStruct([['array', 'characters', { elem: 'struct', values: [mkStruct(chr)] }],
            ['array', 'cameras', { elem: 'empty', values: [] }], ['array', 0x65C1ECD3, { elem: 'empty', values: [] }]]);
          P.events.push(mkStruct([['int', 'time', ch.t], ['qbkey', 'scr', K.Band_PlayClip], ['struct', 'params', mkStruct([
            ['qbkey', 'clip', ck], ['int', 'startframe', 0], ['int', 'endframe', frames]])]]));
          // estos clips no traen camara: los cortes de "camara del clip" de ese tramo pasan a plano medio
          const a = ch.t, b = ch.t + frames * 1000 / FPS;
          for (const cut of P.cameras?.moment || []) if (cut[0] >= a && cut[0] < b && clipSlotOf(cut[2]) !== null) cut[2] = 29;
          res.body++;
        }
      }
    }
  }
  return res;
}
