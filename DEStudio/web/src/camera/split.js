// Pantalla dividida: cuando aparece cada franja, que rectangulo ocupa y que camara muestra.
// Cada franja puede llevar su CAMARA PROPIA (una toma del editor: musico, encuadre y movimiento; el movimiento dura
// "anim" segundos desde que aparece la franja y luego se queda quieta) o la camara del juego congelada al aparecer.
// Lo usan la vista previa (viewport/program.js), la exportacion (camera/edfx.js) y el juego (native_fx.cpp).
import { FRAMINGS, ROLE_AIM, fxDefaults } from './presets.js';

export const splitParams = e => ({ ...fxDefaults('split'), ...(e.params || {}) });

export function stripRect(n, dir, i) {
  if (dir === 2 && n === 4) return [(i % 2) / 2, ((i / 2) | 0) / 2, 0.5, 0.5];
  if (dir === 1) return [i / n, 0, 1 / n, 1];
  return [0, i / n, 1, 1 / n];
}

// ms (de la cancion) en que aparece cada franja: cada "beats" pulsos desde el inicio del efecto
export function revealTimes(proj, e) {
  const p = splitParams(e), n = Math.max(2, Math.min(4, p.n | 0)), fb = proj.fretbars || [];
  const j0 = fb.findIndex(b => b >= e.t - 1);
  return Array.from({ length: n }, (_, i) => {
    const b = j0 >= 0 ? fb[j0 + i * p.beats] : undefined;
    return b != null && b < e.t + e.dur ? b : e.t + e.dur * 0.8 * i / n;
  });
}

// fuentes de camara de una franja
export const STRIP_SOURCES = [['own', 'Camara propia (toma del editor)'], ['perf', 'Camara del escenario (de la actuacion)'],
  ['live', 'La camara de la cancion, en vivo'], ['game', 'La camara de la cancion, congelada al aparecer']];
// modo que lee el juego (m0..m3): 0 congelada, 1 con claves exportadas (propia o de escenario), 2 en vivo
export const stripMode = c => c.cam === 'live' ? 2 : c.cam === 'game' ? 0 : 1;

// configuracion de la franja i (se crea al vuelo con valores por defecto)
const ROLE_CYCLE = ['guitarist', 'vocalist', 'bassist', 'drummer'];
const MOVES = ['acercar', 'orbita', 'grua', 'travelling'];
export function stripCfg(e, i) {
  e.strips ||= [];
  if (!e.strips[i]) {
    const role = ROLE_CYCLE[i % 4], f = FRAMINGS.medio;
    e.strips[i] = { cam: 'own', role, preset: MOVES[i % 4], framing: 'medio', anim: 1.5,
      p: { dist: f.dist, azim: [20, -25, 35, -15][i % 4], height: f.height, fov: f.fov, aim: ROLE_AIM[role], amount: 1, roll: 18 } };
  }
  return e.strips[i];
}

// toma "virtual" de la franja para worldPose(): empieza al aparecer y su movimiento dura "anim" segundos
export function stripShot(e, i, at) {
  const c = stripCfg(e, i);
  return { id: 9000 + e.id * 4 + i, t: at, dur: Math.max(50, (c.anim ?? 1.5) * 1000), role: c.role, preset: c.preset, framing: c.framing, p: c.p, fx: {} };
}
// instante de la toma virtual: el movimiento se para al terminar "anim"
export const stripTime = (s, t) => Math.min(Math.max(t, s.t), s.t + s.dur);
