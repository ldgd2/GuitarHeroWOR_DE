// Seleccion multiple de elementos de la linea de tiempo (tomas, efectos, objetos, retoques, cortes, notas, eventos).
//   S.sel    elemento principal (el que muestra el inspector y el que se redimensiona)
//   S.multi  todos los elegidos, incluido el principal, cuando hay mas de uno
// Asignar S.sel directamente a algo que no esta en S.multi equivale a elegir solo eso (el resto del editor sigue
// funcionando con S.sel como antes).
import { emit, S } from './state.js';

export const same = (a, b) => !!a && !!b && a.type === b.type && a.track === b.track && a.i === b.i && a.key === b.key && a.id === b.id;
export function selList() {
  if (!S.sel) return [];
  if (S.multi?.length > 1 && S.multi.some(x => same(x, S.sel))) return S.multi;
  return [S.sel];
}
export const isSel = sel => selList().some(x => same(x, sel));
export function setSelection(list, primary) {
  const uniq = [];
  for (const x of list) if (x && !uniq.some(y => same(x, y))) uniq.push(x);
  S.multi = uniq.length > 1 ? uniq : null;
  S.sel = primary && uniq.some(y => same(primary, y)) ? primary : uniq.at(-1) || null;
  if (uniq.length > 1) S.tab = 'insp';   // el inspector muestra el grupo
  emit('inspector'); emit('timeline');
}
export function toggleSel(sel) {
  const cur = selList();
  setSelection(cur.some(x => same(x, sel)) ? cur.filter(x => !same(x, sel)) : [...cur, sel], isSel(sel) ? null : sel);
}
export const clearSelection = () => setSelection([]);
