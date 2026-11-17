// Fotogramas clave genericos del estudio: pistas {prop: [{t, v, ease}]} por objeto y clip, muestreo con las
// curvas de keyframes/easing.js, y deshacer. Los usan el editor de interfaces (2D, 2.5D y 3D) y sus exportadores
// (DEStudio/editor/ui/export.py implementa el mismo muestreo en Python).
import { easeValue } from '../keyframes/easing.js';

export const lerp = (a, b, f) => Array.isArray(a) ? a.map((x, i) => x + (b[i] - x) * f) : a + (b - a) * f;
const EPS = 1e-3;

/** valor de una pista en t (undefined si no tiene claves). La curva de una clave rige el tramo que llega a ella. */
export function sample(keys, t) {
  if (!keys?.length) return undefined;
  if (t <= keys[0].t) return keys[0].v;
  for (let i = 0; i < keys.length - 1; i++) {
    const a = keys[i], b = keys[i + 1];
    if (t < b.t) {
      const span = b.t - a.t;
      if (span <= 0) return b.v;
      if (b.ease === 'hold') return a.v;
      return lerp(a.v, b.v, easeValue(b.ease || 'linear', b, (t - a.t) / span));
    }
  }
  return keys[keys.length - 1].v;
}

export const trackSet = (clip, id) => clip?.tracks?.[id] || null;

/** base + lo que digan las pistas en t */
export function evalTracks(base, tracks, t) {
  const s = { ...base };
  if (tracks) for (const k in tracks) { const v = sample(tracks[k], t); if (v !== undefined) s[k] = v; }
  return s;
}

export function setKey(clip, id, prop, t, v) {
  clip.tracks[id] ??= {};
  const ks = (clip.tracks[id][prop] ??= []);
  const at = ks.find(k => Math.abs(k.t - t) < EPS);
  if (at) at.v = v;
  else { ks.push({ t: +t.toFixed(3), v, ease: ks.length ? 'linear' : undefined }); ks.sort((a, b) => a.t - b.t); }
}

export function removeKey(clip, id, prop, t) {
  const tr = clip?.tracks?.[id];
  if (!tr?.[prop]) return;
  tr[prop] = tr[prop].filter(k => Math.abs(k.t - t) >= EPS);
  if (!tr[prop].length) delete tr[prop];
  if (!Object.keys(tr).length) delete clip.tracks[id];
}

export const keyAt = (clip, id, prop, t) => clip?.tracks?.[id]?.[prop]?.find(k => Math.abs(k.t - t) < EPS);

/** mueve las claves de `props` que estan en `from` a `to` (si no chocan con otra) */
export function moveKeys(clip, id, props, from, to) {
  for (const p of props) {
    const ks = clip.tracks[id]?.[p];
    const k = ks?.find(q => Math.abs(q.t - from) < EPS);
    if (k && !ks.some(q => q !== k && Math.abs(q.t - to) < EPS)) { k.t = to; ks.sort((a, b) => a.t - b.t); }
  }
}

/** todos los tiempos con clave de un objeto */
export const keyTimes = tr => [...new Set(Object.values(tr || {}).flat().map(k => k.t))].sort((a, b) => a - b);

export class History {
  constructor() { this.past = []; this.future = []; }
  push(p) { this.past.push(JSON.stringify(p)); if (this.past.length > 200) this.past.shift(); this.future = []; }
  undo(p) { if (!this.past.length) return null; this.future.push(JSON.stringify(p)); return JSON.parse(this.past.pop()); }
  redo(p) { if (!this.future.length) return null; this.past.push(JSON.stringify(p)); return JSON.parse(this.future.pop()); }
}

export const hex = c => '#' + c.slice(0, 3).map(x => Math.max(0, Math.min(255, Math.round(x))).toString(16).padStart(2, '0')).join('');
export const unhex = h => [1, 3, 5].map(i => parseInt(h.slice(i, i + 2), 16));
export const fmt = v => (Math.round(v * 1000) / 1000).toString();
let seq = 1;
export const uid = p => `${p}${Date.now().toString(36).slice(-4)}${(seq++).toString(36)}`;
