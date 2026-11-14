// QBKeys y acceso a las estructuras QB serializadas en JSON por el servidor (editor/core/qbjson.py).
const CRC = new Uint32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
export function qbkey(s) { let c = 0xFFFFFFFF; for (const ch of s.toLowerCase()) c = CRC[(c ^ ch.charCodeAt(0)) & 0xFF] ^ (c >>> 8); return c >>> 0; }
export const hx = n => '0x' + (n >>> 0).toString(16).padStart(8, '0');

const WORDS = `Band_PlayClip Band_PlayLoop Band_PlayFacialAnim Band_ChangeFacialAnims Band_PlayIdle Band_ForceAllToIdle
Band_ShowMic Band_HideMic Band_ShowMic_Stand Band_HideMic_stand Band_ShowMicStand Band_HideMicStand Band_ShowMic_microphone
Band_HideMic_microphone Band_SetAnimTempo Band_ClearAnimTempo Band_EnableAutoChords Band_DisableAutoChords Band_EnableAutoFret
Band_DisableAutoFret Band_EnableAutoStrums Band_DisableAutoStrums Band_SetIKChainTarget Band_MoveToNode Band_PlayRockinFacialAnim
CameraCutsEffect_FOVPulse SetSongHandCamParams scr time params clip startframe endframe timefactor name anim characters cameras
startnode slot ik_targetl ik_targetr strum fret chord male female guitarist bassist vocalist drummer guitar slave true false none`.split(/\s+/);
export const NAMES = new Map(WORDS.map(w => [qbkey(w), w]));
export const K = Object.fromEntries(WORDS.map(w => [w, qbkey(w)]));
export const nameOf = k => NAMES.get(k >>> 0) || hx(k);
export const registerName = (key, name) => NAMES.set(key >>> 0, name);
// texto del usuario -> clave: acepta 0x... o un nombre
export const keyOf = s => (s = String(s).trim()).startsWith('0x') ? parseInt(s, 16) >>> 0 : qbkey(s);

const keyHex = key => hx(typeof key === 'number' ? key : qbkey(key));
export const mem = (st, key) => st?.struct?.find(m => m.n === keyHex(key));
export const val = (st, key, d) => { const m = mem(st, key); return m ? m.v : d; };
export function setVal(st, key, kind, v) {
  const m = mem(st, key);
  if (m) { m.v = v; if (kind) m.k = kind; return; }
  const n = typeof key === 'number' ? key : qbkey(key);
  st.struct.push({ k: kind, n: hx(n), label: nameOf(n), v });
}
export const arr = (st, key) => { const v = val(st, key); return v && v.values ? v : null; };
export const evTime = ev => val(ev, 'time', 0);
export const evScr = ev => val(ev, 'scr', 0) >>> 0;
export const evParams = ev => val(ev, 'params');
export const mkStruct = members => ({ struct: members.map(([k, name, v]) => {
  const n = typeof name === 'number' ? name : qbkey(name);
  return { k, n: hx(n), label: nameOf(n), v };
}) });
