// Estado global, bus de eventos, deshacer/rehacer y utilidades.
//   'changed'   -> el proyecto cambio (marca sucio, redibuja todo)
//   'timeline'  -> solo redibujar la linea de tiempo
//   'inspector' -> solo redibujar el inspector
//   'song'      -> se abrio otra cancion
export const S = {
  proj: null, songs: [], venue: null, t: 0, playing: false, zoom: 0.08, scroll: 0, sel: null,
  undo: [], redo: [], dirty: false, ver: 0, catalog: null, anims: [], animById: new Map(), tab: 'insp',
};
export const $ = id => document.getElementById(id);

export async function api(url, opt) {
  const r = await fetch(url, opt);
  const j = await r.json();
  if (!r.ok || j.error) throw new Error(j.error || r.statusText);
  return j;
}

const handlers = {};
export const on = (evt, fn) => (handlers[evt] ||= []).push(fn);
export const emit = (evt, ...a) => (handlers[evt] || []).forEach(fn => fn(...a));
export const refresh = () => { emit('timeline'); emit('inspector'); };

export function changed() { S.dirty = true; S.ver++; emit('changed'); refresh(); }
export function select(sel) { S.sel = sel; if (sel) S.tab = 'insp'; refresh(); }
export function pushUndo() { S.undo.push(JSON.stringify(S.proj)); if (S.undo.length > 80) S.undo.shift(); S.redo = []; }
export function dropUndo() { S.undo.pop(); }
export function undo() { if (!S.undo.length) return; S.redo.push(JSON.stringify(S.proj)); S.proj = JSON.parse(S.undo.pop()); S.sel = null; changed(); }
export function redo() { if (!S.redo.length) return; S.undo.push(JSON.stringify(S.proj)); S.proj = JSON.parse(S.redo.pop()); S.sel = null; changed(); }

export function fmt(ms) { ms = Math.max(0, ms); const m = Math.floor(ms / 60000), s = (ms % 60000) / 1000; return `${m}:${s.toFixed(3).padStart(6, '0')}`; }
export const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
export const store = {
  get(k, d = null) { try { const v = localStorage.getItem(k); return v === null ? d : JSON.parse(v); } catch { return d; } },
  set(k, v) { try { localStorage.setItem(k, JSON.stringify(v)); } catch { /* sin almacenamiento */ } },
};
