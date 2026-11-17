// Controles de formulario. Los numericos y de texto confirman al salir del campo o con Enter (un paso de
// deshacer por cambio, no por tecla).
import { changed, pushUndo, S } from '../../core/state.js';
import { hx, keyOf } from '../../core/qb.js';
import { Icon, useEffect, useState } from '../react.jsx';

// aplica una edicion del proyecto con su paso de deshacer
export const edit = fn => (...a) => { pushUndo(); fn(...a); changed(); };

export const Field = ({ label, children, hint }) => (<div className="field" title={hint || ''}><span>{label}</span><div>{children}</div></div>);

function Committed({ value, onCommit, type = 'text', parse = v => v, ...rest }) {
  const [v, setV] = useState(String(value ?? ''));
  useEffect(() => setV(String(value ?? '')), [value]);
  const commit = () => { if (v !== String(value ?? '')) onCommit(parse(v)); };
  return (<input type={type} value={v} onChange={e => setV(e.target.value)} onBlur={commit}
    onKeyDown={e => { if (e.key === 'Enter') e.target.blur(); if (e.key === 'Escape') { setV(String(value ?? '')); } }} {...rest} />);
}
export const Num = ({ value, onCommit, step = 1, min, max }) =>
  (<Committed type="number" value={value} step={step} min={min} max={max} parse={Number} onCommit={onCommit} />);
export const Text = ({ value, onCommit, placeholder }) => (<Committed value={value} placeholder={placeholder} onCommit={onCommit} />);

// nombre de animacion con autocompletado (datalist #animNames con las 2000+ animaciones del juego)
export const AnimIn = ({ value, onCommit, placeholder }) => (<Committed list="animNames" placeholder={placeholder}
  value={value ? (S.animById.get(hx(value)) || hx(value)) : ''} parse={v => (v.trim() ? keyOf(v) : 0)} onCommit={onCommit} />);

export const Select = ({ value, options, onChange }) => (<select value={value} onChange={e => onChange(e.target.value)}>
  {options.map(([v, l]) => (<option key={v} value={v}>{l}</option>))}</select>);

export const Check = ({ checked, onChange, children }) => (<label className="check">
  <input type="checkbox" checked={!!checked} onChange={e => onChange(e.target.checked)} /><span className="box"></span>{children}</label>);

export const Card = ({ title, icon, color, actions, children }) => (<section className="card">
  {title && (<header>{icon && <Icon n={icon} s={15} />}<b style={{ color }}>{title}</b><span className="sep"></span>{actions}</header>)}{children}</section>);

export const Note = ({ kind, children }) => (<p className={'note ' + (kind || '')}>{children}</p>);

// Deslizador con valor visible. Un solo paso de deshacer por arrastre; actualiza la vista mientras se mueve.
let sliding = false;
export const Slider = ({ label, value, min, max, step = 0.01, unit = '', onChange, hint, digits = 2 }) => (<div className="field slider" title={hint || ''}><span>{label}</span><div>
    <input type="range" min={min} max={max} step={step} value={value}
      onPointerDown={() => { if (!sliding) { pushUndo(); sliding = true; } }}
      onPointerUp={() => { sliding = false; }}
      onKeyDown={() => { if (!sliding) { pushUndo(); sliding = true; setTimeout(() => { sliding = false; }, 600); } }}
      onInput={e => { onChange(+e.target.value); changed(); }} />
    <output>{String(+(+value).toFixed(digits))}{unit}</output></div></div>);
