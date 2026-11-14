// Interpretacion de los eventos de la cancion: clips, loops y su duracion.
import { S } from '../core/state.js';
import { arr, evParams, evScr, evTime, hx, K, val } from '../core/qb.js';
import { roleOfKey } from '../core/roles.js';

export const clipDef = ev => { const p = evParams(ev); return p ? S.proj.clips[hx(val(p, 'clip', 0))] : null; };
export const clipChars = def => arr(def, 'characters')?.values || [];
export const clipCams = def => arr(def, 'cameras')?.values || [];

export function clipSpan(ev) {
  const p = evParams(ev), def = clipDef(ev);
  let sf = val(p, 'startframe'), ef = val(p, 'endframe');
  const c0 = clipChars(def)[0];
  if (sf === undefined) sf = c0 ? val(c0, 'startframe', 0) : 0;
  if (ef === undefined) ef = c0 ? val(c0, 'endframe', sf + 90) : sf + 90;
  return { sf, ef, ms: Math.max(100, (ef - sf) * 1000 / 30) };
}

export function activeClips(t) {
  const out = [];
  S.proj.events.forEach((ev, i) => {
    if (evScr(ev) !== K.Band_PlayClip) return;
    const t0 = evTime(ev), sp = clipSpan(ev);
    if (t >= t0 && t < t0 + sp.ms) out.push({ ev, i, t0, sp, def: clipDef(ev) });
  });
  // el clip mas reciente manda (en el juego un Band_PlayClip nuevo sustituye al que seguia sonando)
  return out.sort((a, b) => b.t0 - a.t0);
}

// Ultimo Band_PlayLoop del musico antes de t -> {ev, t0, male, female}
export function loopAt(role, t) {
  let best = null;
  for (const ev of S.proj.events) {
    if (evScr(ev) !== K.Band_PlayLoop) continue;
    const p = evParams(ev), t0 = evTime(ev);
    if (roleOfKey(val(p, 'name', 0)) !== role || t0 > t) continue;
    if (!best || t0 >= best.t0) best = { ev, t0, male: val(p, 'male', 0) >>> 0, female: val(p, 'female', 0) >>> 0 };
  }
  return best;
}

export const cutAt = (list, t) => { let best = null; for (const c of list) if (t >= c[0] && t < c[0] + c[1] && (!best || c[0] >= best[0])) best = c; return best; };

// Pulsos (beats) transcurridos hasta t segun las lineas de compas (fretbars = una por pulso).
export function beatsAt(t) {
  const fb = S.proj?.fretbars; if (!fb || fb.length < 2) return t / 500;
  if (t <= fb[0]) return (t - fb[0]) / (fb[1] - fb[0]);
  let lo = 0, hi = fb.length - 1;
  if (t >= fb[hi]) return hi + (t - fb[hi]) / (fb[hi] - fb[hi - 1]);
  while (hi - lo > 1) { const m = (lo + hi) >> 1; if (fb[m] <= t) lo = m; else hi = m; }
  return lo + (t - fb[lo]) / (fb[hi] - fb[lo]);
}
// Tempo al que se grabo un loop: sufijo _075, _100, _150... del nombre (null si no lo lleva)
export const animBpm = name => { const m = /_(\d{2,3})(?:_|$)/.exec(name || ''); const v = m ? +m[1] : 0; return v >= 50 && v <= 250 ? v : null; };
