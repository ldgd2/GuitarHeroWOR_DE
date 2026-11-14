// Pestana "Efectos": galeria de efectos por categorias, presets (grupos de efectos ya ajustados, editables juntos o
// por separado) y editor del efecto seleccionado con TODOS sus ajustes. Llegan al juego por el .edfx: casi todos los
// hace el propio motor (post-proceso nativo del escenario o la camara del juego, ver re-wor/src/de/native_fx.cpp).
import { changed, emit, fmt, pushUndo, S } from '../../core/state.js';
import { COMMON_PARAMS, EFFECTS, FRAMINGS, FX_CATS, FX_COMBOS, fxDefaults, PRESETS, ROLE_AIM } from '../../camera/presets.js';
import { ROLE_ES, ROLES } from '../../core/roles.js';
import { revealTimes, splitParams, STRIP_SOURCES, stripCfg } from '../../camera/split.js';
import { stageCamChoices } from '../../viewport/stagecams.js';
import { firstTrack, ownTrack, tracksOf } from '../../timeline/layout.js';
import { deleteSel, fxOf, snapT } from '../../timeline/edit.js';
import { seek } from '../playback.js';
import { Card, edit, Field, Note, Select, Slider } from '../components/fields.jsx';

const ICON = { letterbox: '▬', vignette: '◉', flash: '✦', fadein: '◐', fadeout: '◑', bw: '◧', sepia: '☕', saturate: '🌈', blur: '≈',
  shake: '≋', glow: '✺', contrast: '◪', chroma: '▤', strobe: '⚡', redflash: '❗', dream: '☁', night: '☾', cold: '❄', warm: '🔥', blackout: '■',
  scanlines: '☰', grain: '░', vhs: '📼', iris: '◯', spotlight: '🔦', heartbeat: '💓', confetti: '🎊', sparks: '✧', snow: '❅',
  rain: '☂', lightning: 'ϟ', rainbow: '🌈', smoke: '☁', lasers: '⟋', bloom: '☀', motionblur: '⇶', godrays: '☼', tiltshift: '⊟',
  rackfocus: '◎', sharpen: '◆', emboss: '▩', edges: '▢', comic: '💥', bleach: '◫', teal: '◑', matrix: '⌨', hueshift: '🎨',
  flicker: '💡', danger: '⚠', fisheye: '◕', wobble: '〰', shockwave: '◌', dutch: '⟀', zoompunch: '⊕', vertigo: '🌀', handheld: '✋',
  split: '▤', grade: '🎚', focus: '🎯', vaporwave: '🌴', cyberpunk: '🌃', noir: '🕵', sunset: '🌅', lomo: '📷', technicolor: '🎞',
  infrared: '🔴', pastel: '🍬', film: '🎬', exposure: '☀', tint: '🎨', fadewhite: '○', blockbuster: '🎥', dramatic: '🎭', kodak: '🟨',
  polaroid: '🖼', retro70: '🕺', miami: '🌴', golden: '🌇', desert: '🏜', ice: '🧊', toxic: '☣', blood: '🩸' };

const visibleTypes = cur => Object.entries(EFFECTS).filter(([k, v]) => !v.hidden || k === cur);
const groupedOptions = cur => Object.entries(FX_CATS).flatMap(([c, name]) =>
  visibleTypes(cur).filter(([, v]) => v.cat === c).map(([k, v]) => [k, `${name} › ${v.label}`]));

// Un control por ajuste: deslizador, lista o color
function ParamControl({ k, spec, obj, onSet }) {
  const v = obj?.[k] ?? spec.def;
  if (spec.color) return <Field label={spec.label}><input type="color" value={v} onChange={edit(ev => onSet(ev.target.value))} /></Field>;
  if (spec.options) return <Field label={spec.label}><Select value={v} options={spec.options} onChange={edit(x => onSet(+x))} /></Field>;
  return <Slider label={spec.label} value={v} min={spec.min} max={spec.max} step={spec.step} digits={spec.step >= 1 ? 0 : 2} onChange={onSet} />;
}
function FxParams({ e, common }) {
  const spec = common ? COMMON_PARAMS : EFFECTS[e.type]?.params; if (!spec) return null;
  return <>{Object.entries(spec).map(([k, p]) => <ParamControl key={k} k={k} spec={p} obj={e.params} onSet={x => { (e.params ||= {})[k] = x; }} />)}</>;
}

// ---- pantalla dividida: camara de cada franja ----
function StripEditor({ e, i, at }) {
  const c = stripCfg(e, i);
  const setFraming = f => { c.framing = f; const F = FRAMINGS[f]; Object.assign(c.p, { dist: F.dist, height: F.height, fov: F.fov }); };
  return <div className="stripBox">
    <b>Franja {i + 1}</b> <span className="muted small">aparece en {fmt(at).slice(0, -4)}</span>
    <Field label="Camara"><Select value={c.cam} options={STRIP_SOURCES} onChange={edit(v => { c.cam = v; })} /></Field>
    {c.cam === 'perf' && (stageCamChoices().length
      ? <Field label="Camara del escenario"><Select value={c.perf ?? stageCamChoices()[0][0]} options={stageCamChoices().map(([k, nm]) => [k, `${k} · ${nm}`])}
        onChange={edit(v => { c.perf = +v; })} /></Field>
      : <Note kind="warn">Este escenario aun no tiene camaras cargadas (abre la vista 3D con el escenario).</Note>)}
    {c.cam === 'own' && <>
      <Field label="Musico"><Select value={c.role} options={ROLES.map(r => [r, ROLE_ES[r]])} onChange={edit(v => { c.role = v; c.p.aim = ROLE_AIM[v]; })} /></Field>
      <Field label="Encuadre"><Select value={c.framing} options={Object.entries(FRAMINGS).map(([k, v]) => [k, v.label])} onChange={edit(setFraming)} /></Field>
      <Field label="Animacion de entrada"><Select value={c.preset} options={Object.entries(PRESETS).filter(([k]) => k !== 'keys').map(([k, v]) => [k, `${v.icon || ''} ${v.label}`])}
        onChange={edit(v => { c.preset = v; })} /></Field>
      <Slider label="Duracion de la animacion" value={c.anim ?? 1.5} min={0.1} max={10} step={0.1} unit=" s" digits={1} onChange={v => { c.anim = v; }}
        hint="Al acabar, la camara se queda quieta en su ultimo encuadre" />
      <Slider label="Angulo (azimut)" value={c.p.azim} min={-180} max={180} step={1} digits={0} unit="°" onChange={v => { c.p.azim = v; }} />
      <Slider label="Altura" value={c.p.height} min={0.1} max={8} step={0.05} unit=" m" onChange={v => { c.p.height = v; }} />
      <Slider label="Distancia" value={c.p.dist} min={0.5} max={20} step={0.1} unit=" m" digits={1} onChange={v => { c.p.dist = v; }} />
      <Slider label="Angulo de vision" value={c.p.fov} min={10} max={110} step={1} digits={0} unit="°" onChange={v => { c.p.fov = v; }} />
      <Slider label="Intensidad del movimiento" value={c.p.amount ?? 1} min={0} max={2} step={0.05} onChange={v => { c.p.amount = v; }} />
    </>}
  </div>;
}
function SplitEditor({ e }) {
  const p = splitParams(e), n = Math.max(2, Math.min(4, p.n | 0)), at = revealTimes(S.proj, e);
  return <>{Array.from({ length: n }, (_, i) => <StripEditor key={i} e={e} i={i} at={at[i]} />)}
    <Note>Cada franja es un viewport real del motor con su propia camara. La animacion empieza cuando aparece la franja y,
      al terminar, la camara se queda quieta. En el monitor cada franja se ve en vivo.</Note></>;
}

// ---- grupos (presets anadidos juntos) ----
const groupOf = e => e.group ? (S.proj.effects || []).filter(x => x.group === e.group) : [];
function GroupEditor({ e }) {
  const g = groupOf(e); if (g.length < 2) return null;
  const t0 = Math.min(...g.map(x => x.t)), t1 = Math.max(...g.map(x => x.t + x.dur));
  const master = e.groupGain ?? 1;
  const saveAsPreset = () => {
    const name = prompt('Nombre del preset:', e.groupLabel || 'Mi preset'); if (!name) return;
    pushUndo();
    (S.proj.userCombos ||= {})['u' + Date.now().toString(36)] = { label: name, dur: t1 - t0,
      fx: g.map(x => [x.type, x.amount ?? 1, { ...(x.params || {}) }, x.t - t0, x.dur]) };
    changed();
  };
  return <div className="groupBox">
    <b>Preset «{e.groupLabel || 'grupo'}»</b> <span className="muted small">{g.length} efectos: {g.map(x => EFFECTS[x.type]?.label).join(', ')}</span>
    <Slider label="Mover todo (empieza)" value={t0 / 1000} min={0} max={S.proj.length / 1000} step={0.05} unit=" s"
      onChange={v => { const d = Math.round(v * 1000) - t0; for (const x of g) x.t += d; }} />
    <Slider label="Duracion de todo" value={(t1 - t0) / 1000} min={0.2} max={120} step={0.1} unit=" s" digits={1}
      onChange={v => { const k = (v * 1000) / Math.max(1, t1 - t0); for (const x of g) { x.t = Math.round(t0 + (x.t - t0) * k); x.dur = Math.max(50, Math.round(x.dur * k)); } }} />
    <Slider label="Intensidad de todo" value={master} min={0} max={1.5} step={0.01}
      onChange={v => { for (const x of g) { x.baseAmount ??= x.amount ?? 1; x.amount = Math.min(1, x.baseAmount * v); x.groupGain = v; } }} />
    <div className="row"><button onClick={saveAsPreset}>Guardar como preset propio</button>
      <button onClick={edit(() => { for (const x of g) { delete x.group; delete x.groupLabel; delete x.groupGain; delete x.baseAmount; } })}>Desagrupar</button></div>
  </div>;
}

function addFx(type, combo) {
  if (!S.proj) return;
  const tr = S.fxTrack && tracksOf('fx').some(t => t.id === S.fxTrack) ? S.fxTrack : firstTrack('fx')?.id;
  if (!tr) return;
  pushUndo();
  let id = Math.max(0, ...(S.proj.effects || []).map(e => e.id));
  const t0 = snapT(S.t);
  if (combo) {
    const group = 'g' + Date.now().toString(36);
    for (const it of combo.fx) {
      const [ty, amount = 1, params = {}, off = 0, dur = combo.dur || 8000] = [].concat(it);
      id++;
      (S.proj.effects ||= []).push({ id, t: t0 + off, dur, type: ty, amount, params: { ...params }, track: tr, group, groupLabel: combo.label });
    }
  } else {
    id++;
    (S.proj.effects ||= []).push({ id, t: t0, dur: type === 'flash' ? 600 : type === 'split' ? 8000 : 3000, type, amount: 1, track: tr });
  }
  S.fxType = type; S.sel = { type: 'fx', id }; changed();
}

function FxEditor({ e }) {
  const tracks = tracksOf('fx'), def = EFFECTS[e.type];
  return <div className="fade">
    <header className="shotHead" style={{ '--c': '#e66bd1' }}><b className="big">{ICON[e.type]}</b>
      <div><h3>{def?.label}</h3><p className="muted small">{def?.desc || 'Efecto de imagen'}</p></div></header>
    <GroupEditor e={e} />
    <Field label="Efecto"><Select value={e.type} options={groupedOptions(e.type)} onChange={edit(v => { e.type = v; })} /></Field>
    <Field label="Pista"><Select value={ownTrack(e, 'fx')} options={tracks.map(t => [t.id, t.name])} onChange={edit(v => { e.track = v; })} /></Field>
    <Slider label="Empieza" value={e.t / 1000} min={0} max={S.proj.length / 1000} step={0.05} unit=" s" onChange={v => { e.t = Math.round(v * 1000); }} />
    <Slider label="Duracion" value={e.dur / 1000} min={0.1} max={120} step={0.1} unit=" s" digits={1} onChange={v => { e.dur = Math.round(v * 1000); }} />
    <Slider label="Intensidad" value={e.amount ?? 1} min={0} max={1} step={0.01} onChange={v => { e.amount = v; delete e.baseAmount; }} />
    <FxParams e={e} />
    <FxParams e={e} common />
    {e.type === 'split' && <SplitEditor e={e} />}
    <div className="row"><button onClick={() => { seek(e.t); S.playing = true; emit('play'); }}>▶ Ver</button>
      <button onClick={edit(() => { e.params = {}; e.amount = 1; })} title="Vuelve a los valores por defecto">Restablecer</button>
      <button onClick={edit(() => { const id = Math.max(0, ...S.proj.effects.map(x => x.id)) + 1;
        S.proj.effects.push({ ...structuredClone(e), id, t: e.t + e.dur }); S.sel = { type: 'fx', id }; })}>Duplicar despues</button>
      <span className="sep" /><button className="danger" onClick={deleteSel}>Borrar</button></div>
  </div>;
}

export { addFx, FxEditor };

export function FxPanel() {
  if (!S.proj) return <div className="empty"><p>Abre una cancion para anadir efectos.</p></div>;
  const e = S.sel?.type === 'fx' ? fxOf(S.sel) : null;
  const list = [...(S.proj.effects || [])].sort((a, b) => a.t - b.t);
  const tracks = tracksOf('fx');
  const q = (S.fxSearch || '').toLowerCase();
  const match = v => !q || v.label.toLowerCase().includes(q) || (v.desc || '').toLowerCase().includes(q);
  const combos = { ...FX_COMBOS, ...(S.proj.userCombos || {}) };
  return <div className="fade">
    {e && <Card><FxEditor key={e.id} e={e} /></Card>}
    <Card title="Presets (efectos ya ajustados que se anaden juntos)">
      <div className="presetGrid">{Object.entries(combos).filter(([, v]) => match({ label: v.label })).map(([k, v]) => (
        <button key={k} className="preset" title={v.fx.map(x => EFFECTS[[].concat(x)[0]]?.label).join(' + ')} onClick={() => addFx([].concat(v.fx[0])[0], v)}>
          <b>{v.fx.slice(0, 3).map(x => ICON[[].concat(x)[0]]).join('')}</b><span>{v.label}{k.startsWith('u') ? ' ★' : ''}</span></button>))}</div>
      <Note>Un preset se anade como grupo: al seleccionar cualquiera de sus efectos puedes mover, alargar o bajar la intensidad
        de todo el grupo, o ajustar cada efecto por separado. "Guardar como preset propio" lo deja aqui (marcado con ★).</Note>
    </Card>
    <Card title="Anadir efecto en el cursor">
      <Field label="En la pista"><Select value={S.fxTrack || tracks[0]?.id} options={tracks.map(t => [t.id, t.name])} onChange={v => { S.fxTrack = v; emit('inspector'); }} /></Field>
      <Field label="Buscar"><input type="search" value={S.fxSearch || ''} placeholder="glow, cine, vhs..." onChange={ev => { S.fxSearch = ev.target.value; emit('inspector'); }} /></Field>
      {Object.entries(FX_CATS).filter(([c]) => c !== 'experimental').map(([c, name]) => {
        const items = visibleTypes().filter(([k, v]) => v.cat === c && k !== 'split' && match(v));
        return items.length ? <div key={c}><h4 className="fxCat">{name}</h4>
          <div className="presetGrid">{items.map(([k, v]) => <button key={k} className="preset" title={v.desc} onClick={() => addFx(k)}>
            <b>{ICON[k]}</b><span>{v.label}</span></button>)}</div></div> : null;
      })}
      <Note>Todo llega al juego por el .edfx: color, luz, enfoque, vineta, franjas y camara los hace el propio motor del juego;
        texturas y particulas se dibujan encima sin tapar el traste ni las notas.</Note>
    </Card>
    <Card title={`Efectos (${list.length})`}>
      {list.length ? <ul className="list">{list.map(x => <li key={x.id} className={e?.id === x.id ? 'on' : ''}
        onClick={() => { S.sel = { type: 'fx', id: x.id }; if (S.t < x.t || S.t >= x.t + x.dur) seek(x.t + 1); emit('inspector'); emit('timeline'); }}>
        <b>{ICON[x.type]}</b><span className="mono">{fmt(x.t).slice(0, -4)}</span><span>{EFFECTS[x.type]?.label}{x.group ? <i className="muted"> · {x.groupLabel}</i> : null}</span>
        <span className="muted">{(x.dur / 1000).toFixed(1)} s</span></li>)}</ul>
        : <p className="muted small">Sin efectos. Tambien puedes hacer doble clic en una pista de efectos.</p>}
    </Card>
  </div>;
}
