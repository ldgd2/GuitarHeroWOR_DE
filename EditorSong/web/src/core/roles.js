// Musicos, nodos de actuacion y numeros de camara validos.
import { K } from './qb.js';

export const ROLES = ['guitarist', 'bassist', 'vocalist', 'drummer'];
export const ROLE_ES = { guitarist: 'Guitarrista', bassist: 'Bajista', vocalist: 'Cantante', drummer: 'Bateria' };
export const ROLE_COLOR = { guitarist: '#ff7a45', bassist: '#4aa3ff', vocalist: '#c77dff', drummer: '#46c27a' };
export const roleOfKey = k => ROLES.find(r => K[r] === (k >>> 0));
export const NODE_OF = { vocalist: 'TRG_Geo_Camera_Performance_SING01', guitarist: 'TRG_Geo_Camera_Performance_GUIT01',
  bassist: 'TRG_Geo_Camera_Performance_BASS01', drummer: 'TRG_Geo_Camera_Performance_DRUM01' };
export const roleOfNode = s => ({ SING: 'vocalist', GUIT: 'guitarist', BASS: 'bassist', DRUM: 'drummer' })[(s || '').slice(-6, -2).toUpperCase()] || 'vocalist';
export const roleOfStart = s => ROLES.find(r => (s || '').toLowerCase().startsWith(r)) || 'vocalist';

// Numeros de camara que existen en cada pista (medido en las canciones vanilla de WoR).
export const AUTO_OK = new Set([8, 9, 10, 11, 13, 14, 15, 16, 18, 19, 20, 21, 23, 24, 25, 26, 28, 29, 30, 31, 38, 43, 45, 46, 48, 57, 58, 60, 61, 62, 63, 68, 69, 74, 75]);
export const MOMENT_OK = new Set([3, 4, 5, 6, 8, 9, 13, 14, 18, 19, 23, 24, 26, 28, 29, 31, 33, 35, 36, 43, 45, 57, 60, 62, 63, 64, 65, 68, 69, 75, 94, 95, 96, 97, 98]);
export const clipSlotOf = cam => (cam >= 3 && cam <= 6) ? cam - 3 : (cam >= 90 && cam <= 99) ? cam - 89 : null;
