// Opciones de la vista 3D (persisten en el navegador). Cambiar una emite 'view' con su nombre.
import { emit, store } from './state.js';

export const VIEW = Object.assign({
  geometry: true, fx: false, crowd: false, textures: true, flat: true, texSize: 1024, shadows: false, markers: true, grid: false,
  band: true, ik: true, skeleton: false, through: false, axis: 'flip', fov: 50, flySpeed: 4, maxView: false,
  post: true, bloom: 0.6, exposure: 1.1, fog: true, postEdit: false,
}, store.get('view:v2', {}));
export const setView = (k, v) => { VIEW[k] = v; store.set('view:v2', VIEW); emit('view', k); };
