// Operaciones de edicion sobre el proyecto: mover, redimensionar, anadir y borrar elementos.
import { changed, dropUndo, pushUndo, S } from '../core/state.js';
import { same, selList, setSelection } from '../core/selection.js';
import { layerOf, newLayer, poseLayers } from '../rig/poseLayer.js';
import { trackById } from './layout.js';
import { evParams, evScr, evTime, hx, K, mkStruct, setVal, val } from '../core/qb.js';
import { ROLE_ES, roleOfKey } from '../core/roles.js';
import { clipChars, clipSpan } from '../song/clips.js';
import { dialog } from '../ui/overlay.js';
import { newShot } from '../camera/shots.js';
import { addProp } from '../viewport/props.js';

export const shotOf = sel => S.proj.shots?.find(s => s.id === sel.id);
export const fxOf = sel => S.proj.effects?.find(e => e.id === sel.id);
export const propOf = sel => S.proj.props?.find(e => e.id === sel.id);
const objOf = sel => (sel.type === 'shot' ? shotOf(sel) : sel.type === 'fx' ? fxOf(sel) : sel.type === 'prop' ? propOf(sel) : sel.type === 'pose' ? layerOf(sel) : null);

// tiempo y duracion de cualquier elemento (para mover varios a la vez)
export function getT(sel) {
  const P = S.proj, o = objOf(sel);
  if (o) return o.t ?? 0;
  if (sel.type === 'cam') return P.cameras[sel.track][sel.i]?.[0] ?? 0;
  if (sel.type === 'note') return P.notes[sel.track][sel.i]?.[0] ?? 0;
  if (sel.type === 'event') return val(P.events[sel.i], 'time', 0);
  return 0;
}
export function getLen(sel) {
  const P = S.proj, o = objOf(sel);
  if (o) return o.dur ?? 0;
  if (sel.type === 'cam') return P.cameras[sel.track][sel.i]?.[1] ?? 0;
  if (sel.type === 'note') return P.notes[sel.track][sel.i]?.[2] ?? 0;
  return 0;
}

export function snapT(t, free) {
  if (free || !S.snap || !S.proj.fretbars.length) return Math.max(0, Math.round(t));
  const fb = S.proj.fretbars; let best = fb[0];
  for (let i = 0; i < fb.length - 1; i++) for (let k = 0; k < 4; k++) {
    const v = fb[i] + (fb[i + 1] - fb[i]) * k / 4; if (Math.abs(v - t) < Math.abs(best - t)) best = v;
  }
  return Math.round(best);
}

export function setT(sel, t) {
  const P = S.proj;
  if (sel.type === 'shot') { const s = shotOf(sel); if (s) s.t = t; }
  else if (sel.type === 'fx') { const e = fxOf(sel); if (e) e.t = t; }
  else if (sel.type === 'prop') { const e = propOf(sel); if (e) e.t = t; }
  else if (sel.type === 'pose') { const e = layerOf(sel); if (e) e.t = t; }
  else if (sel.type === 'cam') P.cameras[sel.track][sel.i][0] = t;
  else if (sel.type === 'note') P.notes[sel.track][sel.i][0] = t;
  else if (sel.type === 'event') setVal(P.events[sel.i], 'time', 'int', t);
}

export function setLen(sel, len) {
  const P = S.proj; len = Math.max(30, Math.round(len));
  if (sel.type === 'shot') { const s = shotOf(sel); if (s) s.dur = Math.min(len, 65000); }
  else if (sel.type === 'fx') { const e = fxOf(sel); if (e) e.dur = len; }
  else if (sel.type === 'prop') { const e = propOf(sel); if (e) e.dur = len; }
  else if (sel.type === 'pose') { const e = layerOf(sel); if (e) { e.dur = len; for (const k of e.keys || []) k.t = Math.min(k.t, len); } }
  else if (sel.type === 'cam') P.cameras[sel.track][sel.i][1] = Math.min(len, 65535);
  else if (sel.type === 'note') P.notes[sel.track][sel.i][2] = Math.min(len, 65535);
  else if (sel.type === 'event' && evScr(P.events[sel.i]) === K.Band_PlayClip) {
    const p = evParams(P.events[sel.i]), sp = clipSpan(P.events[sel.i]);
    setVal(p, 'startframe', 'int', sp.sf); setVal(p, 'endframe', 'int', Math.round(sp.sf + len * 30 / 1000));
  }
}

const nearest = (list, t) => list.sort((a, b) => Math.abs(evTime(a) - t) - Math.abs(evTime(b) - t))[0];
function duplicateAt(src, t) { const ev = JSON.parse(JSON.stringify(src)); setVal(ev, 'time', 'int', t); S.proj.events.push(ev); return { type: 'event', i: S.proj.events.length - 1 }; }

export function addAt(lane, t) {
  const P = S.proj; pushUndo();
  const fail = msg => { dropUndo(); if (msg) dialog(msg); };
  const kind = lane.kind;
  if (kind === 'shots') {
    const s = newShot(t, S.shotRole || 'guitarist', S.shotPreset || 'mano');
    s.track = lane.id; (P.shots ||= []).push(s); S.sel = { type: 'shot', id: s.id }; S.tab = 'cams';
  } else if (kind === 'fx') {
    const id = Math.max(0, ...(P.effects || []).map(e => e.id)) + 1;
    (P.effects ||= []).push({ id, t, dur: 2000, type: S.fxType || 'letterbox', amount: 1, track: lane.id });
    S.sel = { type: 'fx', id }; S.tab = 'fx';
  } else if (kind === 'props') {
    dropUndo(); const p = addProp(S.propKind || 'box', { t, dur: 8000, track: lane.id }); if (p) S.sel = { type: 'prop', id: p.id };
    return;
  } else if (kind === 'pose') {
    const L = newLayer(lane.role, t); L.track = lane.id; poseLayers().push(L);
    S.sel = { type: 'pose', id: L.id }; S.tab = 'insp';
  } else if (kind === 'auto' || kind === 'moment') {
    P.cameras[kind].push([t, 3000, kind === 'auto' ? 8 : 33]);
    S.sel = { type: 'cam', track: kind, i: P.cameras[kind].length - 1 };
  } else if (kind === 'light' || kind === 'note') {
    const track = kind === 'light' ? 'lightshow_notes' : lane.track;
    const note = kind === 'light' ? (Array.isArray(lane.notes) && lane.notes.length ? lane.notes[0] : 82) : track === 'anim_notes' ? 96 : 84;
    const len = kind === 'light' && lane.notes?.includes?.(78) ? 100 : track === 'anim_notes' ? 49 : 400;
    P.notes[track].push([t, note, len, 100]); S.sel = { type: 'note', track, i: P.notes[track].length - 1 };
  } else if (kind === 'clip') {
    const key = Object.keys(P.clips).find(k => clipChars(P.clips[k]).some(c => roleOfKey(val(c, 'name', 0)) === lane.role));
    if (!key) return fail(`No hay ningun clip con ${ROLE_ES[lane.role]}. Crea uno con <b>Nuevo clip</b>.`);
    const c0 = clipChars(P.clips[key])[0];
    P.events.push(mkStruct([['int', 'time', t], ['qbkey', 'scr', K.Band_PlayClip], ['struct', 'params', mkStruct([
      ['qbkey', 'clip', parseInt(key, 16)], ['int', 'startframe', val(c0, 'startframe', 0)], ['int', 'endframe', val(c0, 'endframe', 90)], ['int', 'timefactor', 1]])]]));
    S.sel = { type: 'event', i: P.events.length - 1 };
  } else if (kind === 'loop') {
    const prev = nearest(P.events.filter(ev => evScr(ev) === K.Band_PlayLoop && roleOfKey(val(evParams(ev), 'name', 0)) === lane.role), t);
    if (prev) S.sel = duplicateAt(prev, t);
    else {
      const loop = S.anims.find(a => a.role === lane.role && !a.additive && !/_f(_|$)/i.test(a.name));
      if (!loop) return fail('No hay loops de ese musico.');
      const fem = S.anims.find(a => a.name.toLowerCase() === loop.name.toLowerCase() + '_f') || loop;
      P.events.push(mkStruct([['int', 'time', t], ['qbkey', 'scr', K.Band_PlayLoop], ['struct', 'params', mkStruct([
        ['qbkey', 'name', K[lane.role]], ['qbkey', 'male', parseInt(loop.id, 16)], ['qbkey', 'female', parseInt(fem.id, 16)]])]]));
      S.sel = { type: 'event', i: P.events.length - 1 };
    }
  } else if (kind === 'events') {
    const prev = nearest(P.events.filter(ev => ![K.Band_PlayClip, K.Band_PlayLoop].includes(evScr(ev))), t);
    if (!prev) return fail('No hay eventos de este tipo que duplicar en esta cancion.');
    S.sel = duplicateAt(prev, t);
  } else return fail();
  changed();
}

export function deleteSel() {
  const s = S.sel, P = S.proj; if (!s || !P) return;
  if (selList().length > 1) { deleteSelection(); return; }
  pushUndo();
  if (s.type === 'shot') P.shots = P.shots.filter(x => x.id !== s.id);
  else if (s.type === 'pose') P.poseLayers = poseLayers().filter(x => x.id !== s.id);
  else if (s.type === 'fx') P.effects = P.effects.filter(x => x.id !== s.id);
  else if (s.type === 'prop') P.props = P.props.filter(x => x.id !== s.id);
  else if (s.type === 'cam') P.cameras[s.track].splice(s.i, 1);
  else if (s.type === 'note') P.notes[s.track].splice(s.i, 1);
  else if (s.type === 'event') P.events.splice(s.i, 1);
  else if (s.type === 'clipdef') {
    const used = P.events.some(ev => evScr(ev) === K.Band_PlayClip && hx(val(evParams(ev), 'clip', 0)) === s.key);
    if (used) { dropUndo(); dialog('Ese clip se usa en la linea de tiempo: borra antes sus eventos.'); return; }
    delete P.clips[s.key];
  }
  S.sel = null; changed();
}

export function newChar(role) {
  const body = S.proj.skas.find(s => !s.camera && s.bones > 100) || { id: '0x0', frames: 90 };
  const ik = role === 'vocalist' ? K.slave : K.guitar;
  const m = [['qbkey', 'name', K[role]], ['string', 'startnode', role + '_start'], ['qbkey', 'anim', parseInt(body.id, 16)],
    ['int', 'startframe', 0], ['int', 'endframe', body.frames], ['int', 'timefactor', 1], ['qbkey', 'ik_targetl', ik], ['qbkey', 'ik_targetr', ik]];
  if (role !== 'drummer') m.push(['qbkey', 'strum', K.true], ['qbkey', 'fret', K.true], ['qbkey', 'chord', K.true]);
  return mkStruct(m);
}

// ---------- varios elementos a la vez ----------
// los que estan en pistas bloqueadas no se mueven ni se borran
const lockedOf = sel => {
  const o = objOf(sel); if (o?.track && trackById(o.track)?.lock) return true;
  if (sel.type === 'cam') return !!trackById(sel.track)?.lock;
  return false;
};
export const movable = () => selList().filter(x => !lockedOf(x));

// borra todos los elegidos (los de indice, de mayor a menor para no desplazar los demas)
export function deleteSelection() {
  const P = S.proj, list = movable(); if (!P || !list.length) return;
  pushUndo();
  const ids = type => new Set(list.filter(x => x.type === type).map(x => x.id));
  const sh = ids('shot'), fx = ids('fx'), pr = ids('prop'), po = ids('pose');
  if (sh.size) P.shots = P.shots.filter(x => !sh.has(x.id));
  if (fx.size) P.effects = P.effects.filter(x => !fx.has(x.id));
  if (pr.size) P.props = P.props.filter(x => !pr.has(x.id));
  if (po.size) P.poseLayers = poseLayers().filter(x => !po.has(x.id));
  const byIdx = list.filter(x => ['cam', 'note', 'event'].includes(x.type)).sort((a, b) => b.i - a.i);
  for (const x of byIdx) {
    if (x.type === 'cam') P.cameras[x.track].splice(x.i, 1);
    else if (x.type === 'note') P.notes[x.track].splice(x.i, 1);
    else P.events.splice(x.i, 1);
  }
  S.sel = null; S.multi = null; changed();
}

// portapapeles: copias de los elementos con su tiempo relativo al primero
let CLIP = null;
export function copySelection() {
  const P = S.proj, list = selList(); if (!P || !list.length) return 0;
  const t0 = Math.min(...list.map(getT));
  CLIP = list.map(x => {
    const dt = getT(x) - t0, o = objOf(x);
    if (o) return { type: x.type, dt, data: JSON.parse(JSON.stringify(o)) };
    if (x.type === 'cam') return { type: 'cam', track: x.track, dt, data: [...P.cameras[x.track][x.i]] };
    if (x.type === 'note') return { type: 'note', track: x.track, dt, data: [...P.notes[x.track][x.i]] };
    if (x.type === 'event') return { type: 'event', dt, data: JSON.parse(JSON.stringify(P.events[x.i])) };
    return null;
  }).filter(Boolean);
  return CLIP.length;
}
export const hasClipboard = () => !!CLIP?.length;
export function pasteAt(t) {
  const P = S.proj; if (!P || !CLIP?.length) return;
  pushUndo();
  const out = [], nid = list => Math.max(0, ...(list || []).map(x => x.id)) + 1;
  for (const c of CLIP) {
    const at = Math.max(0, Math.round(t + c.dt)), d = JSON.parse(JSON.stringify(c.data));
    if (c.type === 'shot') { d.id = nid(P.shots); d.t = at; (P.shots ||= []).push(d); out.push({ type: 'shot', id: d.id }); }
    else if (c.type === 'fx') { d.id = nid(P.effects); d.t = at; (P.effects ||= []).push(d); out.push({ type: 'fx', id: d.id }); }
    else if (c.type === 'prop') { d.id = nid(P.props); d.t = at; (P.props ||= []).push(d); out.push({ type: 'prop', id: d.id }); }
    else if (c.type === 'pose') { d.id = nid(P.poseLayers); d.t = at; poseLayers().push(d); out.push({ type: 'pose', id: d.id }); }
    else if (c.type === 'cam') { d[0] = at; P.cameras[c.track].push(d); out.push({ type: 'cam', track: c.track, i: P.cameras[c.track].length - 1 }); }
    else if (c.type === 'note') { d[0] = at; P.notes[c.track].push(d); out.push({ type: 'note', track: c.track, i: P.notes[c.track].length - 1 }); }
    else if (c.type === 'event') { setVal(d, 'time', 'int', at); P.events.push(d); out.push({ type: 'event', i: P.events.length - 1 }); }
  }
  changed();
  setSelection(out);
}
export function duplicateSelection() {
  const list = selList(); if (!list.length) return;
  const t1 = Math.max(...list.map(x => getT(x) + (getLen(x) || 0)));
  if (copySelection()) pasteAt(t1);
}
// mueve todos los elegidos dt ms (con su paso de deshacer)
export function nudgeSelection(dt) {
  const list = movable(); if (!list.length) return;
  pushUndo();
  for (const x of list) setT(x, Math.max(0, Math.round(getT(x) + dt)));
  changed();
}
export { same };
