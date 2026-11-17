// Elementos de cada pista de la linea de tiempo (con nombres legibles, sin hex). Ver layout.js.
import { S } from '../core/state.js';
import { evParams, evScr, evTime, hx, K, nameOf, val } from '../core/qb.js';
import { AUTO_OK, clipSlotOf, MOMENT_OK, ROLE_COLOR, ROLE_ES, roleOfKey } from '../core/roles.js';
import { cutName } from '../core/cutnames.js';
import { clipChars, clipDef, clipSpan } from '../song/clips.js';
import { EFFECTS, PRESETS } from '../camera/presets.js';
import { lightTrackOf, ownTrack, poseTrack, shotTrack } from './layout.js';

export const HEAD = 236;           // ancho de la columna de nombres
export const TRACK_H = { audio: 64, stem: 30, bars: 30, shots: 40, auto: 32, moment: 32, fx: 32, light: 30, clip: 34, loop: 30,
  events: 28, note: 28, bandmoment: 22, props: 32, pose: 32 };
export const GROUP_H = 30, GROUP_GAP = 10;
export const animName = k => S.animById.get(hx(k)) || 'animacion ' + hx(k).slice(2, 8);
const pretty = s => s.replace(/^Band_/, '').replace(/_/g, ' ').replace(/([a-z])([A-Z])/g, '$1 $2');

export function trackItems(tr) {
  const P = S.proj, out = [];
  switch (tr.kind) {
    case 'shots':
      (P.shots || []).forEach(s => {
        if (shotTrack(s) !== tr.id) return;
        const pr = PRESETS[s.preset];
        out.push({ t: s.t, len: s.dur, label: `${pr?.label || s.preset} · ${ROLE_ES[s.role]}`, color: ROLE_COLOR[s.role],
          sel: { type: 'shot', id: s.id }, resize: true, cls: 'shot', uid: 's' + s.id });
      });
      break;
    case 'fx':
      (P.effects || []).forEach(e => {
        if (ownTrack(e, 'fx') !== tr.id) return;
        out.push({ t: e.t, len: e.dur, label: EFFECTS[e.type]?.label || e.type, color: '#8f5ea6', sel: { type: 'fx', id: e.id }, resize: true, uid: 'f' + e.id });
      });
      break;
    case 'props':
      (P.props || []).forEach(p => {
        if (ownTrack(p, 'props') !== tr.id) return;
        const t0 = p.t ?? 0, len = p.dur ?? P.length;
        out.push({ t: t0, len, label: p.name, color: '#8c7352', sel: { type: 'prop', id: p.id }, resize: true, uid: 'p' + p.id });
      });
      break;
    case 'pose':
      (P.poseLayers || []).forEach(L => {
        if (poseTrack(L) !== tr.id) return;
        const n = L.keys?.length || 0;
        out.push({ t: L.t, len: L.dur, label: `${L.name}${n ? ` · ${n} claves` : ' · sin claves'}`, title: `${L.name}: ${n} claves. Doble clic para editar la animacion`,
          color: ROLE_COLOR[L.role], sel: { type: 'pose', id: L.id }, resize: true, cls: 'pose', uid: 'r' + L.id });
      });
      break;
    case 'auto': case 'moment':
      P.cameras[tr.kind].forEach((c, i) => {
        const slot = tr.kind === 'moment' ? clipSlotOf(c[2]) : null;
        const ok = (tr.kind === 'auto' ? AUTO_OK : MOMENT_OK).has(c[2]);
        out.push({ t: c[0], len: c[1], label: cutName(c[2]), title: `Corte ${c[2]}: ${cutName(c[2])}${ok ? '' : ' — no valido en esta pista'}`,
          color: !ok ? '#a8403f' : slot !== null ? '#6f62c9' : '#3f6f9e', sel: { type: 'cam', track: tr.kind, i }, resize: true });
      });
      break;
    case 'clip':
      P.events.forEach((ev, i) => {
        if (evScr(ev) !== K.Band_PlayClip) return;
        const def = clipDef(ev), me = clipChars(def).find(c => roleOfKey(val(c, 'name', 0)) === tr.role); if (!me) return;
        out.push({ t: evTime(ev), len: clipSpan(ev).ms, label: animName(val(me, 'anim', 0)), title: 'Clip ' + nameOf(val(evParams(ev), 'clip', 0)),
          color: def ? ROLE_COLOR[tr.role] : '#a8403f', sel: { type: 'event', i }, resize: true });
      });
      break;
    case 'loop': {
      const evs = P.events.map((ev, i) => ({ ev, i }))
        .filter(({ ev }) => evScr(ev) === K.Band_PlayLoop && roleOfKey(val(evParams(ev), 'name', 0)) === tr.role)
        .sort((a, b) => evTime(a.ev) - evTime(b.ev));
      evs.forEach(({ ev, i }, n) => {
        const t = evTime(ev), nx = n + 1 < evs.length ? evTime(evs[n + 1].ev) : P.length;
        out.push({ t, len: Math.max(200, nx - t), label: animName(val(evParams(ev), 'male', 0)), color: ROLE_COLOR[tr.role] + '99', sel: { type: 'event', i } });
      });
      break;
    }
    case 'events':
      P.events.forEach((ev, i) => {
        const s = evScr(ev); if (s === K.Band_PlayClip || s === K.Band_PlayLoop) return;
        out.push({ t: evTime(ev), len: 0, label: pretty(nameOf(s)), color: '#4f9c78', sel: { type: 'event', i } });
      });
      break;
    case 'light':
      P.notes.lightshow_notes.forEach((n, i) => {
        if (lightTrackOf(n[1]) !== tr.id) return;
        out.push({ t: n[0], len: n[2], label: String(n[1]), title: `Nota de luces ${n[1]} · ${n[2]} ms`,
          color: tr.notes === undefined ? '#888' : `hsl(${((n[1] - 50) * 23) % 360} 65% 48%)`, sel: { type: 'note', track: 'lightshow_notes', i }, resize: true });
      });
      break;
    case 'note':
      (P.notes[tr.track] || []).forEach((n, i) => out.push({ t: n[0], len: n[2], label: String(n[1]), title: `Nota ${n[1]} · ${n[2]} ms`,
        color: `hsl(${((n[1] - 50) * 23) % 360} 55% 45%)`, sel: { type: 'note', track: tr.track, i }, resize: true }));
      break;
    case 'bandmoment':
      (P.bandmoments || []).forEach(b => out.push({ t: b[0], len: b[1], label: 'momento', color: '#555' }));
      break;
    default:
  }
  return out;
}
