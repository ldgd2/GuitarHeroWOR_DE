// Retoques de pose -> lo que entiende Guitar Hero (al compilar, sobre la copia del proyecto que va al PAK).
// El juego no sabe sumar capas: se hornea la pose FINAL del musico (su animacion + IK + retoque) en el tramo del
// retoque, a 30/s y en trozos de 10 s, como animaciones de clip con la mascara de huesos de los clips del juego:
//   0-15 (raiz, columna, cabeza, brazos, muslo izq.), 18 (muslo der.), 25/26 (pies), 33-62 (dedos),
//   93/94 (objetivos de IK de los pies), 112 (guitarra), 122-127 (objetivos de manos)
// Rodillas y tobillos no van en la mascara: el motor los cierra con IK hacia 93/94, que se colocan donde quedan
// los pies. Cada trozo es un clip con ese musico y su evento Band_PlayClip. Si el retoque toca un brazo, ese
// brazo sigue la animacion (ik_target slave); si no, guitarra y bajo siguen con las manos en el instrumento.
import { api } from '../core/state.js';
import { evParams, evScr, hx, K, mem, mkStruct, qbkey, registerName, val } from '../core/qb.js';
import { clipSlotOf, ROLE_ES } from '../core/roles.js';
import { poseTrack, trackActive } from '../timeline/layout.js';
import { bakeRole } from '../viewport/band.js';
import { ARM_L, ARM_R, layerBones, poseLayers } from './poseLayer.js';

const CHUNK = 300, FPS = 30;
const range = (a, b) => Array.from({ length: b - a + 1 }, (_, i) => a + i);
export const CLIP_BONES = [...range(0, 15), 18, 25, 26, ...range(33, 62), 93, 94, 112, ...range(122, 127)];
const POS_BONES = [0, 1, 93, 94, 112, 122, 123, 124, 125, 126, 127];
const safe = s => s.replace(/[^A-Za-z0-9_]/g, '').slice(0, 20);

export async function materializePoses(P, warnings, onProgress) {
  const res = { layers: 0, clips: 0 };
  const list = poseLayers().filter(L => L.keys?.length && trackActive(poseTrack(L)));
  if (!list.length) return res;
  const song = safe(P.song?.name || 'song');
  for (const L of list) {
    const role = L.role, total = Math.max(2, Math.floor(L.dur / (1000 / FPS)) + 1);
    const touched = layerBones(L);
    const ikL = [...touched].some(i => ARM_L.has(i)), ikR = [...touched].some(i => ARM_R.has(i));
    const instr = role === 'guitarist' || role === 'bassist';
    if (role === 'drummer') warnings.push(`Retoque "${L.name}": la bateria la mueve el juego segun las notas; el retoque puede no verse entero.`);
    // El juego lleva un clip por musico a la vez (en las canciones del juego los clips solapados son siempre de
    // musicos distintos). Los clips que empiezan dentro del retoque y llevan a este musico se quedan con sus camaras
    // y el resto de la banda, pero sin el (su animacion ya va horneada en el nuestro).
    const stripped = stripRole(P, role, L.t, L.t + L.dur);
    if (stripped.cam) warnings.push(`Retoque "${L.name}": se cruza con una toma de camara del editor que solo puede apoyarse en ${ROLE_ES[role]}; en el juego manda el retoque y esa camara puede no verse.`);
    if (stripped.removed) warnings.push(`Retoque "${L.name}": ${stripped.removed} clip(s) solo tenian a ${ROLE_ES[role]}; se quitan y sus camaras de clip pasan a plano medio.`);
    for (let s = 0; s < total - 1; s += CHUNK) {
      const n = Math.min(CHUNK + 1, total - s), t = Math.round(L.t + s * 1000 / FPS);
      onProgress?.(`${L.name}: ${Math.round(s / total * 100)} %`);
      let bones;
      try { bones = await bakeRole(role, t, n, CLIP_BONES, POS_BONES); }
      catch (err) { warnings.push(`Retoque "${L.name}": ${err.message}; no se exporta.`); break; }
      const name = `es_pose_${song}_${role.slice(0, 3)}_${t}`;
      const ska = await api('/api/anim/make', { method: 'POST', body: JSON.stringify({ name, n, bones }) });
      P.skas = (P.skas || []).filter(x => x.id !== ska.id).concat([ska]);
      const key = parseInt(ska.id, 16); registerName(key, name);
      const frames = n - 1, clipName = name + '_clip', ck = qbkey(clipName); registerName(ck, clipName);
      const ik = side => (instr && !side ? K.guitar : K.slave);
      const chr = [['qbkey', 'name', K[role]], ['string', 'startnode', role + '_start'], ['qbkey', 'anim', key],
        ['int', 'startframe', 0], ['int', 'endframe', frames], ['int', 'timefactor', 1],
        ['qbkey', 'ik_targetl', ik(ikL)], ['qbkey', 'ik_targetr', ik(ikR)]];
      if (role !== 'drummer') chr.push(['qbkey', 'strum', K.true], ['qbkey', 'fret', K.true], ['qbkey', 'chord', K.true]);
      P.clips[hx(ck)] = mkStruct([['array', 'characters', { elem: 'struct', values: [mkStruct(chr)] }],
        ['array', 'cameras', { elem: 'empty', values: [] }], ['array', 0x65C1ECD3, { elem: 'empty', values: [] }]]);
      P.events.push(mkStruct([['int', 'time', t], ['qbkey', 'scr', K.Band_PlayClip], ['struct', 'params', mkStruct([
        ['qbkey', 'clip', ck], ['int', 'startframe', 0], ['int', 'endframe', frames]])]]));
      res.clips++;
    }
    res.layers++;
  }
  P.events.sort((x, y) => val(x, 'time', 0) - val(y, 'time', 0));
  fixClipCuts(P, list);
  return res;
}

const clipChars = def => val(def, 'characters')?.values || [];
const clipCams = def => val(def, 'cameras')?.values || [];
function stripRole(P, role, a0, a1) {
  const out = { removed: 0, cam: false }, cams = new Set(P.__camClips || []);
  P.events = P.events.filter(ev => {
    if (evScr(ev) !== K.Band_PlayClip) return true;
    const tm = val(ev, 'time', 0); if (tm < a0 || tm >= a1) return true;
    const par = evParams(ev), ck = hx(val(par, 'clip', 0)), def = P.clips[ck];
    if (!def || !clipChars(def).some(ch => val(ch, 'name', 0) >>> 0 === K[role])) return true;
    if (cams.has(ck)) { out.cam = true; return true; }
    const others = clipChars(def).filter(ch => val(ch, 'name', 0) >>> 0 !== K[role]);
    if (!others.length) { out.removed++; return false; }
    const copy = JSON.parse(JSON.stringify(def));
    mem(copy, 'characters').v.values = JSON.parse(JSON.stringify(others));
    const nk = qbkey(`es_sin_${role.slice(0, 3)}_${ck.slice(2)}`); registerName(nk, `es_sin_${role.slice(0, 3)}_${ck.slice(2)}`);
    P.clips[hx(nk)] = copy;
    mem(par, 'clip').v = nk;
    return true;
  });
  return out;
}

// cortes de "camara del clip" dentro de los retoques: si ya no hay ningun clip con camaras sonando, plano medio
function fixClipCuts(P, layers) {
  const spans = P.events.filter(ev => evScr(ev) === K.Band_PlayClip).map(ev => {
    const par = evParams(ev), def = P.clips[hx(val(par, 'clip', 0))], t = val(ev, 'time', 0);
    const tf = +val(par, 'timefactor', 1) || 1;
    return { t, e: t + (val(par, 'endframe', 0) - val(par, 'startframe', 0)) / 30 * 1000 / tf, cams: def ? clipCams(def).length : 0 };
  });
  for (const cut of P.cameras?.moment || []) {
    if (clipSlotOf(cut[2]) === null || !layers.some(L => cut[0] >= L.t && cut[0] < L.t + L.dur)) continue;
    if (!spans.some(s => s.cams && s.t <= cut[0] + 1 && s.e > cut[0])) cut[2] = 29;
  }
}
