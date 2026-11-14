// Ventanas modales y avisos (los pinta ui/components/Overlays.js). Sin DOM aqui: solo estado + eventos.
//   dialog(html, botones)  -> Promise<valor del boton>     (html: texto ya escapado)
//   ask(etiqueta, valor)   -> Promise<texto | null>
//   toast(texto, tipo)     -> aviso que desaparece solo (tipo: ok | warn | err)
import { emit, S } from '../core/state.js';

S.modal = null;
S.toasts = [];
let nextId = 1;

export function dialog(html, buttons = [['ok', 'Cerrar']], opts = {}) {
  return new Promise(res => { S.modal = { html, buttons, res, ...opts }; emit('modal'); });
}
export function closeModal(value, input) {
  const m = S.modal; S.modal = null; emit('modal');
  m?.res(m.input !== undefined ? (value === 'ok' ? input : null) : value);
}
export const ask = (label, value = '') => dialog(`<p>${label}</p>`, [['cancel', 'Cancelar'], ['ok', 'Aceptar', 'primary']], { input: value });
export const confirmBox = async (text, yes = 'Continuar', cls = 'primary') => (await dialog(`<p>${text}</p>`, [['no', 'Cancelar'], ['yes', yes, cls]])) === 'yes';

export function toast(text, kind = 'ok', ms = 3500) {
  const t = { id: nextId++, text, kind };
  S.toasts.push(t); emit('toast');
  setTimeout(() => { S.toasts = S.toasts.filter(x => x !== t); emit('toast'); }, ms);
}
