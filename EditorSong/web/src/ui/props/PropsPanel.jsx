// Pestana "Objetos 3D": anadir y editar objetos para planificar la escena (solo vista previa).
import { api, emit, S } from '../../core/state.js';
import { addProp, NATIVE_PROPS, PROP_KINDS, setGizmoMode } from '../../viewport/props.js';
import { GAME_MODELS, HOUSINGS, PFX_ALL, PFX_FAV } from '../../camera/native_catalog.js';
import { deleteSel } from '../../timeline/edit.js';
import { Icon, useRef, useState } from '../react.jsx';
import { Card, Check, edit, Field, Note, Num, Text } from '../components/fields.jsx';
import { toast } from '../overlay.js';

const ICON = { box: 'box', sphere: 'sphere', cylinder: 'cylinder', plane: 'plane', screen: 'screen', text: 'text', spot: 'spotlight', pyro: 'fire', glb: 'package', particles: 'particles', gamemodel: 'model' };
const favIds = new Set(PFX_FAV.map(x => x.id));
const Vec = ({ label, v, onCommit, step = 0.1 }) => (<Field label={label}><div className="vec">
  {[0, 1, 2].map(i => (<Num key={i} value={v[i]} step={step} onCommit={x => { const n = [...v]; n[i] = x; onCommit(n); }} />))}</div></Field>);

function PropEditor({ p }) {
  const [mode, setMode] = useState('translate');
  const m = k => { setMode(k); setGizmoMode(k); };
  return (<div className="fade">
    <h3 className="withIco"><Icon n={ICON[p.kind]} s={18} />{p.name}</h3>
    <div className="seg">{[['translate', 'Mover'], ['rotate', 'Girar'], ['scale', 'Escalar']].map(([k, l]) =>
      (<button key={k} className={mode === k ? 'on' : ''} onClick={() => m(k)}>{l}</button>))}</div>
    <p className="muted small">Arrastra las flechas del objeto en la vista 3D, o escribe los valores.</p>
    <Field label="Nombre"><Text value={p.name} onCommit={edit(v => { p.name = v; })} /></Field>
    {NATIVE_PROPS.has(p.kind) && (<p className="okc small">Nativo: lo crea el propio motor del juego.</p>)}
    {p.kind === 'text' && (<Field label="Texto"><Text value={p.text} onCommit={edit(v => { p.text = v; })} /></Field>)}
    {p.kind === 'particles' && (<Field label="Efecto"><select value={p.pfx} onChange={edit(e => { p.pfx = e.target.value; })}>
      <optgroup label="Destacados">{PFX_FAV.map(x => (<option key={x.id} value={x.id}>{x.label}</option>))}</optgroup>
      <optgroup label="Todos los del juego">{PFX_ALL.filter(x => !favIds.has(x)).map(x => (<option key={x} value={x}>{x.replace(/^gp_(fx_)?/, '')}</option>))}</optgroup>
    </select></Field>)}
    {p.kind === 'spot' && (<>
      <Field label="Tipo de foco"><select value={p.housing ?? 0} onChange={edit(e => { p.housing = +e.target.value; })}>
        {HOUSINGS.map((l, i) => (<option key={i} value={i}>{l}</option>))}</select></Field>
      <Field label="Alcance (m)"><Num value={p.range ?? 8} step={0.5} onCommit={edit(v => { p.range = v; })} /></Field>
      <Field label="Apertura del haz (m)"><Num value={p.radius ?? 1.2} step={0.1} onCommit={edit(v => { p.radius = v; })} /></Field>
    </>)}
    {p.kind === 'gamemodel' && (<>
      <Field label="Modelo"><select value={GAME_MODELS.some(m => m.path === p.model) ? p.model : ''} onChange={edit(e => { if (e.target.value) p.model = e.target.value; })}>
        <option value="">(ruta propia)</option>{GAME_MODELS.map(m => (<option key={m.path} value={m.path}>{m.label}</option>))}</select></Field>
      <Field label="Ruta"><Text value={p.model || ''} onCommit={edit(v => { p.model = v; })} /></Field>
      <p className="muted small">Ruta dentro de data/models. Para un modelo de un escenario escribe <span className="mono">zona:ruta</span>
        (p. ej. <span className="mono">z_barbarian:Venues/Barbarian/Skin_Flesh_Pieces.skin</span>): solo aparece si ese escenario esta cargado.</p>
    </>)}
    {p.kind !== 'glb' && (<Field label="Color"><input type="color" value={p.color} onChange={edit(e => { p.color = e.target.value; })} /></Field>)}
    <Vec label="Posicion (m)" v={p.pos} onCommit={edit(v => { p.pos = v; })} />
    <Vec label="Giro (grados)" v={p.rot} step={5} onCommit={edit(v => { p.rot = v; })} />
    <Vec label="Escala" v={p.scale} onCommit={edit(v => { p.scale = v; })} />
    <Check checked={p.visible !== false} onChange={edit(v => { p.visible = v; })}>Visible</Check>
    <div className="row"><button onClick={() => addProp(p.kind, { ...JSON.parse(JSON.stringify(p)), id: undefined, name: p.name + ' (copia)', pos: [p.pos[0] + 1, p.pos[1], p.pos[2]] })}>Duplicar</button>
      <span className="sep"></span><button className="danger" onClick={deleteSel}>Borrar</button></div>
  </div>);
}

export function PropsPanel() {
  const file = useRef();
  if (!S.proj) return (<div className="empty"><p>Abre una cancion para anadir objetos.</p></div>);
  const list = S.proj.props || [];
  const sel = S.sel?.type === 'prop' ? list.find(p => p.id === S.sel.id) : null;
  const upload = async f => {
    if (!f) return;
    const name = f.name.replace(/[^A-Za-z0-9_.-]/g, '_').replace(/(\.glb)?$/i, '.glb');
    try {
      const r = await api('/api/props/upload?name=' + encodeURIComponent(name), { method: 'POST', body: await f.arrayBuffer() });
      addProp('glb', { url: r.url, name: f.name.replace(/\.glb$/i, '') });
    } catch (e) { toast(e.message, 'err', 6000); }
  };
  return (<div className="fade">
    {sel && (<Card><PropEditor key={sel.id} p={sel} /></Card>)}
    <Card title="Anadir objeto">
      <div className="presetGrid">{Object.entries(PROP_KINDS).map(([k, l]) => (<button key={k} className="preset"
        onClick={() => (k === 'glb' ? file.current.click() : addProp(k))}><b><Icon n={ICON[k]} s={20} /></b><span>{l}</span></button>))}</div>
      <input ref={file} type="file" accept=".glb" hidden onChange={e => { const f = e.target.files[0]; e.target.value = ''; upload(f); }} />
      <Note><b>Nativos</b> (los crea el motor del juego): foco de luz con haz volumetrico, columna de fuego, particulas del juego
        (292: chispas, fuego, humo, confeti, fuegos artificiales, electricidad...) y modelos del juego. El resto (cajas, textos,
        .glb...) se dibuja encima de la imagen sin tapar nunca el traste.</Note>
    </Card>
    <Card title={`Objetos (${list.length})`}>
      {list.length ? (<ul className="list">{list.map(p => (<li key={p.id} className={sel?.id === p.id ? 'on' : ''}
          onClick={() => { S.sel = { type: 'prop', id: p.id }; emit('inspector'); }}>
          <b><Icon n={ICON[p.kind]} s={16} /></b><span>{p.name}</span>{p.visible === false && (<span className="muted">oculto</span>)}</li>))}</ul>)
        : (<p className="muted small">Sin objetos todavia.</p>)}
    </Card>
  </div>);
}
