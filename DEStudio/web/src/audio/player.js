// Audio de la cancion en el editor. El servidor da UNA mezcla WAV de los grupos activos (bateria, guitarra,
// bajo, voz, resto, publico); se descarga entera como blob (busqueda instantanea) y se reproduce en un
// <audio>. Mientras suena, el reloj del editor ES el del audio: todo queda sincronizado con la musica.
import { api, emit, on, S, store } from '../core/state.js';

const el = new Audio();
el.preload = 'auto';
el.preservesPitch = true;
export const A = S.audio = { info: null, muted: new Set(store.get('audio:muted', ['crowd'])), volume: store.get('audio:vol', 0.8),
  ready: false, loading: false, error: null, offset: 0 };
el.volume = A.volume;
let url = null, reqId = 0;

const songId = () => S.proj?.song?.id;
export const activeGroups = () => (A.info?.groups || []).map(g => g.id).filter(g => !A.muted.has(g));

async function loadMix() {
  const id = songId(); if (!id || !A.info?.available) return;
  const my = ++reqId, groups = activeGroups();
  A.loading = true; A.ready = false; emit('audio');
  try {
    const r = await fetch(`/api/audio/mix?id=${encodeURIComponent(id)}&groups=${groups.join(',')}`);
    if (!r.ok) throw new Error(r.statusText);
    const blob = await r.blob();
    if (my !== reqId) return;
    const was = !el.paused, t = S.t;
    if (url) URL.revokeObjectURL(url);
    url = URL.createObjectURL(blob);
    el.src = url;
    await new Promise(res => { el.onloadedmetadata = res; el.onerror = res; });
    el.currentTime = Math.max(0, (t + A.offset) / 1000);
    A.ready = true;
    if (was || S.playing) el.play().catch(() => {});
  } catch (e) { A.error = e.message; }
  if (my === reqId) { A.loading = false; emit('audio'); }
}

export async function loadSongAudio() {
  reqId++; el.pause(); A.info = null; A.ready = false; A.error = null; emit('audio');
  const id = songId(); if (!id) return;
  A.loading = true; emit('audio');
  try { A.info = await api('/api/audio?id=' + encodeURIComponent(id)); } catch (e) { A.info = { available: false, reason: e.message }; }
  A.loading = false;
  // la cancion dura al menos lo que su audio (las customs pueden no tener camaras hasta el final)
  const sec = Math.max(0, ...(A.info?.groups || []).map(g => g.seconds));
  if (S.proj && sec * 1000 > S.proj.length) { S.proj.length = Math.round(sec * 1000); emit('timeline'); }
  emit('audio');
  if (A.info?.available) loadMix();
}

export function toggleGroup(g) {
  if (A.muted.has(g)) A.muted.delete(g); else A.muted.add(g);
  store.set('audio:muted', [...A.muted]);
  emit('audio'); loadMix();
}
export function setVolume(v) { A.volume = v; el.volume = v; store.set('audio:vol', v); emit('audio'); }

// llamados desde ui/playback.js
export function audioPlay(on) {
  if (!A.ready) return;
  if (on) { el.currentTime = Math.max(0, (S.t + A.offset) / 1000); el.playbackRate = S.speed || 1; el.play().catch(() => {}); } else el.pause();
}
export function audioSeek(t) { if (A.ready) el.currentTime = Math.max(0, (t + A.offset) / 1000); }
export function audioRate(r) { el.playbackRate = r; }
// tiempo de la cancion segun el audio (null si no hay audio sonando)
export const audioClock = () => (A.ready && !el.paused ? el.currentTime * 1000 - A.offset : null);
export const audioEnded = () => A.ready && el.ended;

on('song', () => { if (songId() !== A.song) { A.song = songId(); loadSongAudio(); } });
