// Pestana "Camaras": galeria de presets para crear tomas, lista de tomas y editor de la toma seleccionada.
import { changed, emit, fmt, pushUndo, S } from '../../core/state.js';
import { ROLE_COLOR, ROLE_ES, ROLES } from '../../core/roles.js';
import { EFFECTS, FRAMINGS, PRESETS } from '../../camera/presets.js';
import { newShot } from '../../camera/shots.js';
import { firstTrack, shotTrack, tracksOf } from '../../timeline/layout.js';
import { deleteSel, shotOf, snapT } from '../../timeline/edit.js';
import { seek } from '../playback.js';
import { Card, Check, edit, Field, Note, Select, Slider } from '../components/fields.jsx';
import { kfOpen } from '../../keyframes/kf.js';
import { addFx } from './FxPanel.jsx';
import { VCAM, vcamArm, vcamConnect, vcamLive } from '../../vcam/vcam.js';

// Camara del movil: conexion y como usarla
function PhoneCam() {
  const I = VCAM.info, sel = S.sel?.type === 'shot' ? S.sel.id : null;
  return <Card title="📱 Camara del movil">
    {!VCAM.editor ? <><p className="muted small">{VCAM.error || 'Conectando con el puente del movil...'}</p>
      <button onClick={vcamConnect}>Reintentar</button></>
      : <>
        <p className="small">En la app <b>EditorSong Camara</b> del telefono (misma red wifi que este PC) escribe:</p>
        <div className="vcamCreds"><span>IP <b className="mono">{I?.ips?.join('  o  ') || '?'}</b></span><span>Puerto <b className="mono">{I?.port}</b></span>
          <span>PIN <b className="mono big">{I?.pin}</b></span></div>
        <p className={VCAM.phone ? 'ok small' : 'muted small'}>{VCAM.phone ? '● Telefono conectado' : '○ Esperando al telefono...'}</p>
        {VCAM.phone && <div className="row">
          <button className={VCAM.live ? 'primary' : ''} onClick={() => vcamLive(!VCAM.live)}>{VCAM.live ? '■ Dejar de mover el monitor' : '▶ Mover el monitor con el movil'}</button>
          <button className={VCAM.target && VCAM.target === sel ? 'primary' : ''} disabled={!sel} onClick={() => vcamArm(VCAM.target === sel ? null : sel)}>
            {sel ? (VCAM.target === sel ? `Toma ${sel} lista para grabar` : `Grabar en la toma ${sel}`) : 'Selecciona una toma para grabar en ella'}</button>
        </div>}
        {VCAM.rec && <p className="err small">● GRABANDO toma {VCAM.shot?.id}: {VCAM.shot?.keys.length} claves</p>}
      </>}
    <Note>Selecciona una toma en la linea de tiempo y pulsa <b>GRABAR</b> en el telefono: la cancion suena desde el inicio de la toma y
      todo lo que hagas con el movil (girarlo, acercarte con el mando, pellizcar para zoom, el pulso de la mano) queda grabado como
      animacion de esa toma. Sin toma elegida se crea una nueva en el cursor que dura hasta que pares. Luego puedes retocarla con
      <b> Editar animacion</b>. Windows puede pedir permiso de red para Python la primera vez: acepta en redes privadas.</Note>
  </Card>;
}

// Multicamara (pantalla dividida): vive aqui, con las camaras
function MultiCam() {
  const list = (S.proj.effects || []).filter(e => e.type === 'split').sort((a, b) => a.t - b.t);
  return <Card title="▤ Multicamara (pantalla dividida)">
    <p className="muted small">De 2 a 4 franjas, cada una con su camara (propia, del escenario o la de la cancion) que aparecen al ritmo.</p>
    <div className="row"><button className="primary" onClick={() => addFx('split')}>+ Multicamara en el cursor</button></div>
    {list.length > 0 && <ul className="list">{list.map(x => <li key={x.id} onClick={() => { S.sel = { type: 'fx', id: x.id }; seek(x.t + 1); emit('inspector'); emit('timeline'); }}>
      <b>▤</b><span className="mono">{fmt(x.t).slice(0, -4)}</span><span>{(x.params?.n ?? 4)} franjas</span><span className="muted">{(x.dur / 1000).toFixed(1)} s</span></li>)}</ul>}
  </Card>;
}

const select = s => { S.sel = { type: 'shot', id: s.id }; if (S.t < s.t || S.t >= s.t + s.dur) seek(s.t + 1); emit('inspector'); emit('timeline'); };

function addShot(preset) {
  if (!S.proj) return;
  pushUndo();
  const s = newShot(snapT(S.t), S.shotRole || 'guitarist', preset, S.shotFraming || 'medio');
  (S.proj.shots ||= []).push(s);
  S.shotPreset = preset;
  changed(); select(s);
}

function RolePick() {
  const cur = S.shotRole || 'guitarist';
  return (<div className="chips">{ROLES.map(r => (<button key={r} className={'chip' + (cur === r ? ' on' : '')} style={{ '--c': ROLE_COLOR[r] }}
    onClick={() => { S.shotRole = r; emit('inspector'); }}>{ROLE_ES[r]}</button>))}</div>);
}

export function ShotEditor({ sel }) {
  const s = shotOf(sel); if (!s) return null;
  const P = s.p, set = k => v => { P[k] = v; };
  const pr = PRESETS[s.preset] || PRESETS.fija;
  const setFraming = edit(f => { const F = FRAMINGS[f]; s.framing = f; Object.assign(P, { dist: F.dist, fov: F.fov, height: F.height }); });
  return (<div className="fade shotEd">
    <header className="shotHead" style={{ '--c': ROLE_COLOR[s.role] }}>
      <b className="big">{pr.icon}</b><div><h3>Toma {s.id} · {pr.label}</h3><p className="muted small">{pr.desc}</p></div></header>
    <div className="row"><button className="primary" onClick={() => kfOpen(s)}
      title="Abre el editor de animacion: pistas, curvas e inspector de claves (los cambios se aplican al pulsar Aplicar)">✎ Editar animacion (keyframes)</button>
      {s.preset === 'keys' && <span className="muted small">{s.keys?.length || 0} claves</span>}</div>
    <Field label="Movimiento"><Select value={s.preset} options={Object.entries(PRESETS).map(([k, v]) => [k, v.icon + '  ' + v.label])}
      onChange={edit(v => { s.preset = v; })} /></Field>
    <Field label="Musico"><Select value={s.role} options={ROLES.map(r => [r, ROLE_ES[r]])} onChange={edit(v => { s.role = v; })} /></Field>
    <Field label="Encuadre"><Select value={s.framing || 'medio'} options={Object.entries(FRAMINGS).map(([k, v]) => [k, v.label])} onChange={setFraming} /></Field>
    <Slider label="Empieza" value={s.t / 1000} min={0} max={S.proj.length / 1000} step={0.05} unit=" s" digits={2} onChange={v => { s.t = Math.round(v * 1000); }} />
    <Slider label="Duracion" value={s.dur / 1000} min={0.5} max={65} step={0.1} unit=" s" digits={1} onChange={v => { s.dur = Math.round(v * 1000); }} />
    <h4>Camara</h4>
    <Slider label="Distancia" value={P.dist} min={0.4} max={20} step={0.05} unit=" m" onChange={set('dist')} hint="Distancia al musico" />
    <Slider label="Angulo" value={P.azim} min={-180} max={180} step={1} unit="°" digits={0} onChange={set('azim')} hint="0 = de frente; positivo = hacia su izquierda" />
    <Slider label="Altura" value={P.height} min={0.1} max={10} step={0.05} unit=" m" onChange={set('height')} />
    <Slider label="Mira a" value={P.aim} min={0} max={2.5} step={0.05} unit=" m" onChange={set('aim')} hint="Altura del punto al que apunta (1.5 = cara, 1 = cintura)" />
    <Slider label="Lente (FOV)" value={P.fov} min={10} max={110} step={1} unit="°" digits={0} onChange={set('fov')} hint="Pequeno = teleobjetivo; grande = gran angular" />
    <Slider label="Intensidad" value={P.amount} min={0} max={2} step={0.05} onChange={set('amount')} hint="Cuanto movimiento / temblor / zoom aplica el preset" />
    {s.preset === 'inclinada' && (<Slider label="Inclinacion" value={P.roll} min={-45} max={45} step={1} unit="°" digits={0} onChange={set('roll')} />)}
    <Field label="Escena (pista)"><Select value={shotTrack(s)} options={tracksOf('shots').map(t => [t.id, t.name])} onChange={edit(v => { s.track = v; })} /></Field>
    <h4>Efectos de imagen en esta toma <span className="tag">solo vista previa</span></h4>
    <div className="chips">{['letterbox', 'vignette', 'flash', 'shake', 'bw', 'fadein', 'fadeout'].map(k => <button key={k} className="chip" title={EFFECTS[k].desc}
      onClick={edit(() => { const id = Math.max(0, ...(S.proj.effects || []).map(e => e.id)) + 1; (S.proj.effects ||= []).push({ id, t: s.t, dur: s.dur, type: k, amount: 1, track: firstTrack('fx')?.id }); })}>+ {EFFECTS[k].label}</button>)}</div>
    {Object.values(s.fx || {}).some(Boolean) && <div className="fxGrid">{Object.entries(EFFECTS).filter(([k]) => s.fx?.[k]).map(([k, v]) => (<Check key={k} checked
      onChange={edit(on => { s.fx = { ...(s.fx || {}), [k]: on }; })}><span>{v.label}</span></Check>))}</div>}
    <Note>La camara y su movimiento <b>si</b> llegan al juego (se genera una camara .ska de verdad al compilar). Los efectos van a la
      pista <b>Efectos de imagen</b> y solo se ven aqui. Si dos escenas se solapan, en la vista previa manda la de mas arriba.</Note>
    <div className="row">
      <button onClick={() => { seek(s.t); S.playing = true; emit('play'); }}>▶ Ver la toma</button>
      <button onClick={edit(() => { const c = { ...JSON.parse(JSON.stringify(s)), id: newShot(0).id, t: s.t + s.dur }; S.proj.shots.push(c); S.sel = { type: 'shot', id: c.id }; })}>Duplicar despues</button>
      <span className="sep"></span><button className="danger" onClick={deleteSel}>Borrar</button></div>
  </div>);
}

export function CamerasPanel() {
  if (!S.proj) return (<div className="empty"><p>Abre una cancion para crear tomas de camara.</p></div>);
  const list = [...(S.proj.shots || [])].sort((a, b) => a.t - b.t);
  const sel = S.sel?.type === 'shot' ? S.sel : null;
  return (<div className="fade">
    {sel && (<Card><ShotEditor key={sel.id} sel={sel} /></Card>)}
    <PhoneCam />
    <MultiCam />
    <Card title="Nueva toma en el cursor">
      <p className="muted small">Elige el musico y el tipo de plano. La toma empieza en <b>{fmt(S.t).slice(0, -4)}</b> y dura 3 s (luego la ajustas).</p>
      <RolePick />
      <div className="presetGrid">{Object.entries(PRESETS).map(([k, v]) => (<button key={k} className="preset" title={v.desc} onClick={() => addShot(k)}>
        <b>{v.icon}</b><span>{v.label}</span></button>))}</div>
    </Card>
    <Card title={`Tomas de esta cancion (${list.length})`}>
      {list.length ? (<ul className="list">{list.map(s => (<li key={s.id} className={sel?.id === s.id ? 'on' : ''} onClick={() => select(s)}>
          <i className="dot" style={{ background: ROLE_COLOR[s.role] }}></i><span className="mono">{fmt(s.t).slice(0, -4)}</span>
          <span>{PRESETS[s.preset]?.icon} {PRESETS[s.preset]?.label}</span><span className="muted">{ROLE_ES[s.role]} · {(s.dur / 1000).toFixed(1)} s</span></li>))}</ul>)
        : (<p className="muted small">Aun no hay tomas. Tambien puedes hacer doble clic en la pista <b>Tomas del editor</b>.</p>)}
    </Card>
  </div>);
}
