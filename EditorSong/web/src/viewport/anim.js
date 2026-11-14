// Animaciones .ska descargadas del servidor (ya en la convencion de three.js) y su muestreo.
//   fuente 'song' -> animaciones del PAK de la cancion (clips y camaras)
//   fuente 'loop' -> biblioteca del juego (pak/archive/anim_loops)
import * as THREE from 'three';
import { S } from '../core/state.js';

const cache = new Map();

function prepare(d) {
  const rot = new Map(), pos = new Map();
  for (const [b, a] of Object.entries(d.rot)) rot.set(+b, Float32Array.from(a));
  for (const [b, a] of Object.entries(d.pos)) pos.set(+b, Float32Array.from(a));
  return { duration: d.duration || 0, camera: d.camera, additive: !!d.additive, single: !!d.single, fov: d.fov || [], rot, pos };
}

// devuelve los datos si ya estan, null mientras se cargan (y lanza la carga)
export function getAnim(src, id) {
  const key = src + ':' + id;
  const c = cache.get(key);
  if (c) return c.data;
  const entry = { data: null };
  cache.set(key, entry);
  const url = src === 'song' ? `/api/ska?full=1&song=${encodeURIComponent(S.proj.song.id)}&id=${id}` : `/api/band/anim?id=${id}`;
  fetch(url).then(r => r.json()).then(j => { entry.data = j.error ? { error: j.error } : prepare(j); })
    .catch(e => { entry.data = { error: String(e) }; });
  return null;
}
export function clearSongAnims() { for (const k of [...cache.keys()]) if (k.startsWith('song:')) cache.delete(k); }

function find(a, stride, t) {
  const n = a.length / stride;
  if (t <= a[0]) return [0, 0, 0];
  if (t >= a[(n - 1) * stride]) return [n - 1, n - 1, 0];
  let lo = 0, hi = n - 1;
  while (hi - lo > 1) { const m = (lo + hi) >> 1; if (a[m * stride] <= t) lo = m; else hi = m; }
  const t0 = a[lo * stride], t1 = a[hi * stride];
  return [lo, hi, t1 > t0 ? (t - t0) / (t1 - t0) : 0];
}
const qa = new THREE.Quaternion(), qb = new THREE.Quaternion();
export function sampleQ(a, t, out) {
  const [i, j, f] = find(a, 5, t);
  qa.fromArray(a, i * 5 + 1); qb.fromArray(a, j * 5 + 1);
  return out.slerpQuaternions(qa, qb, f);
}
export function sampleV(a, t, out) {
  const [i, j, f] = find(a, 4, t), o = i * 4 + 1, p = j * 4 + 1;
  return out.set(a[o] + (a[p] - a[o]) * f, a[o + 1] + (a[p + 1] - a[o + 1]) * f, a[o + 2] + (a[p + 2] - a[o + 2]) * f);
}
