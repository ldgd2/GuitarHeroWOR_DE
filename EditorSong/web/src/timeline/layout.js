// Organizacion de la linea de tiempo en GRUPOS desplegables con SUBPISTAS (S.proj.layout, se guarda con el
// proyecto; el juego no lo ve). Cada pista tiene un tipo (kind) que dice de donde salen sus elementos:
//   audio / stem      audio de la cancion (mezcla y grupos: bateria, guitarra...)
//   bars              compases y notas de la guitarra
//   shots             tomas de camara del editor (varias pistas = varias escenas; la de arriba manda)
//   auto / moment     pistas de camara del juego
//   fx                efectos de imagen (solo vista previa)
//   light             notas de luces filtradas por numero (fuego, luces...); 'rest' = las que no entran en otra
//   clip / loop       animaciones por musico
//   events / note / bandmoment   eventos especiales, ordenes a la banda, publico, momentos
//   props             objetos 3D con tramo de tiempo (solo vista previa)
import { S } from '../core/state.js';
import { ROLE_COLOR, ROLE_ES } from '../core/roles.js';

const range = (a, b) => Array.from({ length: b - a + 1 }, (_, i) => a + i);
let uid = 1;
export const newId = p => `${p}${Date.now().toString(36)}${(uid++).toString(36)}`;

// Pista de luces "fuego": la nota 78 aparece en 19 de 20 canciones del juego, ~15 veces por cancion y con
// duracion corta (~0.1 s): es la candidata a pirotecnia (no confirmado en el codigo del juego).
export const FIRE_NOTES = [78];

export function defaultLayout() {
  const role = r => ({ id: 'g_' + r, name: ROLE_ES[r], icon: 'user', color: ROLE_COLOR[r], open: true, tracks: [
    { id: 'clip_' + r, kind: 'clip', role: r, name: 'Clips (animacion + camara)' },
    { id: 'loop_' + r, kind: 'loop', role: r, name: 'Bucles de animacion' }] });
  return { v: 2, groups: [
    { id: 'g_song', name: 'Cancion', icon: 'music', color: '#9aa4b8', open: true, tracks: [
      { id: 'audio', kind: 'audio', name: 'Audio (mezcla)' },
      ...[['drums', 'Bateria'], ['guitar', 'Guitarra'], ['bass', 'Bajo'], ['vocals', 'Voz'], ['song', 'Resto de la cancion'], ['crowd', 'Publico']]
        .map(([stem, name]) => ({ id: 'stem_' + stem, kind: 'stem', stem, name })),
      { id: 'bars', kind: 'bars', name: 'Compases y notas' }] },
    { id: 'g_cams', name: 'Camaras', icon: 'cam', color: '#5aa9ff', open: true, add: 'shots', tracks: [
      { id: 'shots_1', kind: 'shots', name: 'Escena 1' },
      { id: 'auto', kind: 'auto', name: 'Camaras automaticas (juego)' },
      { id: 'moment', kind: 'moment', name: 'Camaras de momento (juego)' }] },
    { id: 'g_fx', name: 'Efectos de imagen', icon: 'fx', color: '#e66bd1', open: true, add: 'fx', tracks: [
      { id: 'fx_1', kind: 'fx', name: 'Efectos 1' }] },
    { id: 'g_fire', name: 'Fuego', icon: 'fire', color: '#ff6a2a', open: true, add: 'light', tracks: [
      { id: 'fire_1', kind: 'light', name: 'Pirotecnia (nota 78)', notes: FIRE_NOTES }] },
    { id: 'g_lights', name: 'Luces', icon: 'light', color: '#ffd23a', open: false, add: 'light', tracks: [
      { id: 'lt_mood', kind: 'light', name: 'Ambiente (19-57)', notes: range(19, 57) },
      { id: 'lt_step', kind: 'light', name: 'Pasos de luz (58)', notes: [58] },
      { id: 'lt_spot', kind: 'light', name: 'Focos y estrobos (59-89)', notes: range(59, 89).filter(n => !FIRE_NOTES.includes(n)) },
      { id: 'lt_col', kind: 'light', name: 'Colores (90-107)', notes: range(90, 107) },
      { id: 'lt_rest', kind: 'light', name: 'Otras notas de luz', notes: 'rest' }] },
    { id: 'g_special', name: 'Especiales', icon: 'star', color: '#4fd18b', open: false, tracks: [
      { id: 'events', kind: 'events', name: 'Eventos de la banda' },
      { id: 'anim_notes', kind: 'note', track: 'anim_notes', name: 'Ordenes a la banda' },
      { id: 'crowd_notes', kind: 'note', track: 'crowd_notes', name: 'Publico' },
      { id: 'bandmoment', kind: 'bandmoment', name: 'Momentos (solo lectura)' }] },
    role('guitarist'), role('vocalist'), role('bassist'), role('drummer'),
    { id: 'g_props', name: 'Objetos 3D', icon: 'cube', color: '#c9a36b', open: false, add: 'props', tracks: [
      { id: 'props_1', kind: 'props', name: 'Objetos 1' }] },
  ] };
}

export function layout() {
  const P = S.proj;
  if (!P.layout || P.layout.v !== 2) P.layout = defaultLayout();
  return P.layout;
}
export const allTracks = () => layout().groups.flatMap(g => g.tracks.map(t => ({ ...t, group: g })));
export const trackById = id => { for (const g of layout().groups) { const t = g.tracks.find(x => x.id === id); if (t) return t; } return null; };
export const groupOf = id => layout().groups.find(g => g.tracks.some(t => t.id === id));
export const firstTrack = kind => allTracks().find(t => t.kind === kind);
export const tracksOf = kind => allTracks().filter(t => t.kind === kind);

// tipos que el usuario puede crear (y borrar) como subpistas
export const USER_KINDS = { shots: 'Escena', fx: 'Efectos', light: 'Luces', props: 'Objetos' };
export const isUserTrack = t => t.kind in USER_KINDS && !(t.kind === 'light' && t.notes === 'rest');

// pista a la que pertenece una nota de luces (la primera cuyo filtro la contiene; si no, la de "resto")
export function lightTrackOf(note) {
  const ls = tracksOf('light');
  return (ls.find(t => Array.isArray(t.notes) && t.notes.includes(note)) || ls.find(t => t.notes === 'rest') || ls[0])?.id;
}

// pista de un elemento del editor (toma, efecto, objeto): la suya si existe y es de ese tipo; si no, la primera
export const ownTrack = (it, kind) => (trackById(it.track)?.kind === kind ? it.track : firstTrack(kind)?.id);
export const shotTrack = s => ownTrack(s, 'shots');
// orden de prioridad de las pistas de tomas: la de mas arriba manda
export const shotPriority = () => tracksOf('shots').map(t => t.id);
export const trackActive = id => trackById(id)?.off !== true;
