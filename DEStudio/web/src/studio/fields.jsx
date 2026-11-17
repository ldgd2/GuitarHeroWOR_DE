// Campos de edicion compartidos del estudio (numero con flechas, color, rombo de clave).
import React, { useEffect, useState } from 'react';
import { Icon } from '../ui/icons.jsx';
import { fmt, hex, unhex } from './keys.js';

export function Num({ v, step = 1, min, max, onChange, className = 'st-num' }) {
  const [txt, setTxt] = useState(fmt(v ?? 0));
  useEffect(() => setTxt(fmt(v ?? 0)), [v]);
  const commit = s => {
    let n = parseFloat(s);
    if (Number.isNaN(n)) return setTxt(fmt(v ?? 0));
    if (min !== undefined) n = Math.max(min, n);
    if (max !== undefined) n = Math.min(max, n);
    onChange(n);
  };
  return <input className={className} value={txt} onChange={e => setTxt(e.target.value)} onBlur={e => commit(e.target.value)}
    onKeyDown={e => {
      if (e.key === 'Enter') commit(e.target.value);
      if (e.key === 'ArrowUp' || e.key === 'ArrowDown') {
        e.preventDefault();
        commit(String((v ?? 0) + (e.key === 'ArrowUp' ? 1 : -1) * step * (e.shiftKey ? 10 : 1)));
      }
    }} />;
}

export const Color = ({ v, onChange }) =>
  <input type="color" value={hex(v || [255, 255, 255])} onChange={e => onChange(unhex(e.target.value))} />;

/** rombo de clave: vacio = sin pista, medio = con pista, lleno = clave en el cursor */
export const KeyToggle = ({ state, disabled, onClick, title }) =>
  <button className={'st-kf' + (state ? ' ' + state : '')} disabled={disabled} onClick={onClick} title={title}>
    <Icon n="keyframe" s={13} /></button>;
