// Configuracion de la banda de la vista previa: personaje, piezas e instrumento por musico (se guarda en el navegador).
// No forma parte de la cancion: solo decide que modelos del juego se ven en el editor.
import { emit, S, store } from '../core/state.js';

const KEY = 'band:v1';
export const band = store.get(KEY, {});
const DEFAULT_PRESET = { guitarist: 'Axel Steel', bassist: 'Clive Winston', vocalist: 'Judy Nails', drummer: 'Eddie Knox' };
export const NONE = 'Ninguno';

const pieces = kind => S.catalog.pieces.filter(p => p.kind === kind);
export const pieceById = id => S.catalog.pieces.find(p => p.id === id);

export function instrumentOptions(role) {
  const none = { name: NONE, pieces: [] };
  if (role === 'guitarist' || role === 'bassist') {
    const kind = role === 'guitarist' ? 'guitar' : 'bass';
    return [none, ...S.catalog.instruments.filter(s => s.kind === kind)];
  }
  if (role === 'vocalist') {
    const stands = pieces('mic_stand');
    return [none, ...pieces('mic').map(m => {
      const style = m.name.replace(/^mic_/, ''), stand = stands.find(s => s.name === 'mic_stand_' + style);
      return { name: `Micro ${style}${stand ? ' con pie' : ''}`, pieces: [m.id, stand?.id].filter(Boolean) };
    })];
  }
  const sticks = pieces('sticks'), kits = pieces('drums');
  return [none, ...sticks.map(s => ({ name: 'Baquetas ' + s.name, pieces: [s.id] })),
    ...kits.map(k => ({ name: 'Baquetas + ' + k.name, pieces: [sticks[0]?.id, k.id].filter(Boolean) }))];
}

export function applyPreset(role, name) {
  const p = S.catalog.presets.find(x => x.name === name); if (!p) return;
  const b = band[role] ||= {};
  b.preset = p.name; b.gender = p.gender; b.pieces = [...p.pieces];
}

export function ensure(role) {
  if (band[role]?.pieces) return band[role];
  applyPreset(role, DEFAULT_PRESET[role]);
  const b = band[role] ||= { gender: 'm', pieces: [] };
  const opts = instrumentOptions(role);
  b.instrument = (opts.find(o => /rock/.test(o.name)) || opts[1] || opts[0]).name;
  b.preview = '';
  return b;
}

export function specOf(role) {
  const b = ensure(role);
  const inst = instrumentOptions(role).find(o => o.name === b.instrument);
  return { gender: b.gender, pieces: [...b.pieces, ...(inst ? inst.pieces : [])] };
}

export function saveBand(role, reload = true) { store.set(KEY, band); if (reload) emit('band', role); }
