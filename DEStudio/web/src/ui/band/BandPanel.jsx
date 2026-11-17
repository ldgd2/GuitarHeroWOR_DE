// Pestana "Banda": personaje, piezas, instrumento y animacion de prueba de cada musico (solo vista previa).
import { emit, S } from '../../core/state.js';
import { hx, keyOf } from '../../core/qb.js';
import { ROLE_COLOR, ROLE_ES, ROLES } from '../../core/roles.js';
import { applyPreset, band, ensure, instrumentOptions, pieceById, saveBand } from '../../band/config.js';
import { shotRole } from '../../viewport/controls.js';
import { Icon, useBus, useState  } from '../react.jsx';
import { SKIN_TONES, DEFAULT_SKIN } from '../../viewport/skin.js';
import { AnimIn, Card, Field, Note, Select } from '../components/fields.jsx';

function Musician({ role }) {
  const b = ensure(role);
  const [cat, setCat] = useState('torso');
  const [open, setOpen] = useState(S.bandFocus ? S.bandFocus === role : role === 'guitarist');
  const update = (fn, reload = true) => { fn(b); saveBand(role, reload); };
  const cats = Object.entries(S.catalog.categories).filter(([k]) => k !== 'other');
  const choices = S.catalog.pieces.filter(p => p.kind === 'body' && p.gender === b.gender && p.cat === cat);
  const [pick, setPick] = useState('');
  const put = () => {
    const id = pick || choices[0]?.id; if (!id) return;
    update(x => {
      if (cat !== 'acc') x.pieces = x.pieces.filter(y => pieceById(y)?.cat !== cat);
      if (cat === 'hair') x.pieces = x.pieces.filter(y => pieceById(y)?.cat !== 'hathair');
      x.pieces.push(id); x.preset = '';
    });
  };
  return (<Card title={ROLE_ES[role]} color={ROLE_COLOR[role]} actions={(<><button className="ghost small" title="Encuadrar en la vista 3D" onClick={() => shotRole(role)}><Icon n="eye" /></button>
      <button className="ghost small" onClick={() => setOpen(!open)}>{open ? 'Ocultar' : 'Editar'}</button></>)}>
    <div className="row muted small">{b.preset || 'Personalizado'} · {b.gender === 'f' ? 'mujer' : 'hombre'} · {b.instrument}</div>
    <div className={'collapse' + (open ? ' open' : '')}><div>
      <Field label="Personaje"><Select value={b.preset || ''} options={[...S.catalog.presets.map(p => [p.name, p.name]), ['', 'Personalizado']]}
        onChange={v => v && update(() => applyPreset(role, v))} /></Field>
      <Field label="Cuerpo"><Select value={b.gender} options={[['m', 'Hombre'], ['f', 'Mujer']]}
        onChange={v => update(x => { x.gender = v; x.preset = ''; x.pieces = x.pieces.filter(id => pieceById(id)?.gender === v); })} /></Field>
      <Field label="Piel" hint="Tono que el juego pone en las zonas de piel (amarillas en las texturas)">
        <div className="row">{SKIN_TONES.map(([hex, nm]) => <button key={hex} title={nm} className={'swatch' + ((b.skin || DEFAULT_SKIN) === hex ? ' on' : '')}
          style={{ background: hex }} onClick={() => { update(x => { x.skin = hex; }, false); emit('bandSkin', role); emit('inspector'); }} />)}
          <input type="color" value={b.skin || DEFAULT_SKIN} onChange={e => { update(x => { x.skin = e.target.value; }, false); emit('bandSkin', role); }} /></div></Field>
      <Field label="Instrumento"><Select value={b.instrument} options={instrumentOptions(role).map(o => [o.name, o.name])}
        onChange={v => update(x => { x.instrument = v; })} /></Field>
      <div className="chips">{b.pieces.map((id, i) => { const p = pieceById(id); return (<span key={id + i} className="chip" title={p?.path || id}>
        {p ? p.name : id}<button onClick={() => update(x => { x.pieces.splice(i, 1); x.preset = ''; })}><Icon n="x" s={11} /></button></span>); })}</div>
      <div className="row2"><Select value={cat} options={cats} onChange={v => { setCat(v); setPick(''); }} />
        <Select value={pick || choices[0]?.id || ''} options={choices.map(p => [p.id, p.name])} onChange={setPick} />
        <button onClick={put}>Poner</button></div>
      <Field label="Anim. de prueba" hint="Vacio = sigue la cancion"><AnimIn value={b.preview ? keyOf(b.preview) : 0} placeholder="sigue la cancion"
        onCommit={v => update(x => { x.preview = v ? hx(v) : ''; }, false)} /></Field>
    </div></div>
  </Card>);
}

export function BandPanel() {
  useBus('band', 'catalog', 'band-focus');
  if (!S.catalog) return (<p className="muted">Cargando catalogo de modelos del juego...</p>);
  return (<div className="fade">
    <Note>Modelos reales del juego para la vista previa (no cambia la cancion). Cada cambio vuelve a montar el personaje.</Note>
    {ROLES.map(r => (<Musician key={r + (S.bandFocus || '')} role={r} />))}
  </div>);
}
