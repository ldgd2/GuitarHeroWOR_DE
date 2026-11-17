// Nombre legible de cada numero de corte de camara (tabla del Guitar Hero SDK, CamHandler.js).
// Los numeros por encima de 48 se usan en las canciones pero no estan documentados.
import { clipSlotOf } from './roles.js';

const N = {
  3: 'Camara del clip 1', 4: 'Camara del clip 2', 5: 'Camara del clip 3', 6: 'Camara del clip 4',
  8: 'Guitarrista · primer plano', 9: 'Guitarrista · escenario', 10: 'Guitarrista · movimiento 1', 11: 'Guitarrista · movimiento 2',
  13: 'Cantante · primer plano', 14: 'Cantante · escenario', 15: 'Cantante · movimiento 1', 16: 'Cantante · movimiento 2',
  18: 'Bajista · primer plano', 19: 'Bajista · escenario', 20: 'Bajista · movimiento 1', 21: 'Bajista · movimiento 2',
  23: 'Bateria · primer plano', 24: 'Bateria · escenario', 25: 'Bateria · movimiento 1', 26: 'Bateria · movimiento 2',
  28: 'Plano lejano', 29: 'Plano medio', 30: 'Plano medio del escenario', 31: 'Plano general del escenario',
  33: 'Solo de guitarra', 34: 'Solo de voz', 35: 'Solo de bateria', 36: 'Solo de bajo',
  38: 'Plano individual', 40: 'Especial 1', 41: 'Especial 2', 43: 'Publico', 45: 'Acercamiento (zoom in)',
  46: 'Alejamiento (zoom out)', 48: 'Foco (spotlight)',
};
export function cutName(n) {
  const s = clipSlotOf(n);
  if (n >= 90 && n <= 99 && s !== null) return `Camara del clip ${s + 1}`;
  return N[n] || `Corte ${n} (sin documentar)`;
}
export const CUT_OPTIONS = Object.keys(N).map(Number);
