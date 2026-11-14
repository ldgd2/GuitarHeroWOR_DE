// Conversion tiempo <-> pixeles de la linea de tiempo y seguimiento del cursor.
import { S } from '../core/state.js';
import { HEAD } from './lanes.js';

export const xOf = ms => HEAD + (ms - S.scroll) * S.zoom;
export const msOf = x => (x - HEAD) / S.zoom + S.scroll;
export const tlWidth = () => document.getElementById('tlScroll')?.clientWidth || 1000;

// sigue el cursor de reproduccion
export function follow() {
  const W = tlWidth();
  if (xOf(S.t) > W - 60 || xOf(S.t) < HEAD) S.scroll = Math.max(0, S.t - 40 / S.zoom);
}
