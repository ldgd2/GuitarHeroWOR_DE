// Paneles del editor de interfaces: Propiedades (transformar, texto, forma, imagen, estilos de capa, interaccion),
// Capas (arbol con miniaturas y arrastrar para ordenar o meter en grupos), Pantallas y Recursos (imagenes del juego,
// archivos propios: PNG, JPG, SVG y fuentes).
import React, { useEffect, useMemo, useRef, useState } from 'react';
import { Icon } from '../ui/icons.jsx';
import { EASES } from '../keyframes/easing.js';
import { AssetBrowser, fileToBase64 } from '../studio/AssetBrowser.jsx';
import { Color, KeyToggle, Num } from '../studio/fields.jsx';
import { api } from '../studio/files.js';
import { LayerCanvas, cachedRaster, useRasterVersion } from '../studio/LayerCanvas.jsx';
import { SYSTEM_FONTS } from '../studio/raster.js';
import { ACCEPT, FontPicker, uploadFiles } from './resources.jsx';
import { ImageEditor } from './ImageEditor.jsx';
import { SelFx } from './SelFx.jsx';
import {
  ACTIONS, CAM25, SCRIPT_PRESETS, SHAPES, SHAPE_ICON, TYPES, TYPE_ICON, ancestors, cam25At, childrenOf, clean, clipOf, descendants,
  fmt, focusables, keyAtClip, propsFor, removeKey, setProp, stateAt,
} from './model.js';
import { assetUrl, fitText, layerUrl, layout } from './view2d.js';

const WEIGHTS = [[300, 'Fina'], [400, 'Normal'], [500, 'Media'], [600, 'Seminegrita'], [700, 'Negrita'], [800, 'Extra'], [900, 'Black']];
export const fontList = proj => [...(proj.fonts || []).map(f => f.family), ...SYSTEM_FONTS];

function Section({ title, icon, children, open: open0 = true, right }) {
  const [open, setOpen] = useState(open0);
  useEffect(() => { if (open0) setOpen(true); }, [open0]);   // se abre sola cuando pasa a tener algo (p. ej. al hacerla boton)
  return (
    <div className={'ue-sec' + (open ? ' open' : '')}>
      <div className="ue-sechead" onClick={() => setOpen(!open)}>
        <Icon n={open ? 'chevdown' : 'chev'} s={12} />{icon && <Icon n={icon} s={14} />}<span>{title}</span>
        <span className="st-grow" />{right && <span onClick={e => e.stopPropagation()}>{right}</span>}
      </div>
      {open && <div className="ue-secbody">{children}</div>}
    </div>
  );
}
const Row = ({ label, children, title }) => <label className="ue-row" title={title}><span>{label}</span><div>{children}</div></label>;
const Toggle = ({ on, onChange, children, title }) =>
  <button className={'ue-tog' + (on ? ' on' : '')} onClick={() => onChange(!on)} title={title}>{children}</button>;
const Check = ({ v, onChange, label }) =>
  <label className="ue-check"><input type="checkbox" checked={!!v} onChange={e => onChange(e.target.checked)} />{label}</label>;

// ===================================================================== propiedades
export function PropsPanel({ proj, doc, mode, sel, clip, t, edit, editProj, selKey, setSelKey, selCut, setSelCut, setSid, setTool, onScript, onDropScript, onImport }) {
  const c = clipOf(doc, clip);
  if (selCut != null && c?.cuts?.[selCut]) {
    const cut = c.cuts[selCut];
    return (
      <div className="ue-props">
        <Section title="Corte de camara" icon="cam">
          <Row label="En el segundo"><Num v={cut.t} step={0.05} min={0} max={c.duration} onChange={v => edit(d => { d.clips[clip].cuts[selCut].t = v; })} /></Row>
          <Row label="Pasa a"><select value={cut.cam} onChange={e => edit(d => { d.clips[clip].cuts[selCut].cam = e.target.value; })}>
            {doc.layers.filter(l => l.type === 'camera').map(l => <option key={l.id} value={l.id}>{l.name}</option>)}</select></Row>
          <button onClick={() => { edit(d => { d.clips[clip].cuts.splice(selCut, 1); }); setSelCut(null); }}><Icon n="trash" />Borrar corte</button>
        </Section>
      </div>);
  }
  const layers = sel.map(id => id === CAM25 ? { id: CAM25, type: CAM25, name: 'Camara 2.5D', base: doc.cam25 } : doc.layers.find(l => l.id === id)).filter(Boolean);
  if (!layers.length) return <ScreenProps proj={proj} doc={doc} mode={mode} edit={edit} editProj={editProj} setSid={setSid} onScript={onScript} onImport={onImport} />;
  if (layers.length > 1) return <MultiProps layers={layers} doc={doc} clip={clip} t={t} edit={edit} />;
  const layer = layers[0];
  const isCam25 = layer.id === CAM25;
  const s = isCam25 ? cam25At(doc, clip, t) : stateAt(doc, layer, clip, t);
  const find = d => (isCam25 ? { id: CAM25 } : d.layers.find(l => l.id === layer.id));
  const set = (prop, v) => edit(d => {
    const L = find(d);
    setProp(d, clip, L, prop, v, t);
    if (L.type === 'text' && (prop === 'w') && clip === 'base') { L.text.box = 'fixed'; fitText(L); }
  });
  const setL = (fn, record = true) => edit(d => { const L = d.layers.find(l => l.id === layer.id); fn(L, d); if (L.type === 'text') fitText(L); }, record);
  const toggleKey = prop => {
    if (clip === 'base') return;
    if (keyAtClip(c, layer.id, prop, t)) edit(d => removeKey(d, clip, layer.id, prop, t));
    else set(prop, s[prop]);
  };
  const key = selKey && selKey.lid === layer.id ? keyAtClip(c, layer.id, selKey.prop, selKey.t) : null;
  const props = propsFor(mode, isCam25 ? CAM25 : layer.type);
  const prop = ([p, label, step, min, max]) => {
    const has = clip !== 'base' && !!c?.tracks?.[layer.id]?.[p];
    const on = clip !== 'base' && !!keyAtClip(c, layer.id, p, t);
    return (
      <div key={p} className="ue-prop">
        <KeyToggle state={on ? 'on' : has ? 'has' : ''} disabled={clip === 'base'} onClick={() => toggleKey(p)}
          title={clip === 'base' ? 'Elige Entrada, Bucle o Salida para animar' : on ? 'Quitar la clave' : 'Poner una clave aqui'} />
        <span>{label}</span>
        {p === 'color' ? <Color v={s.color} onChange={v => set('color', v)} />
          : <Num v={p === 'alpha' ? s[p] : s[p]} step={step} min={min} max={max} onChange={v => set(p, v)} />}
      </div>);
  };
  return (
    <div className="ue-props">
      <div className="ue-proptitle">
        <Icon n={isCam25 ? 'cam' : layer.type === 'shape' ? SHAPE_ICON[layer.shape?.kind] || 'rect' : TYPE_ICON[layer.type]} />
        {isCam25 ? <b>Camara 2.5D</b> : <input value={layer.name} onChange={e => setL(L => { L.name = e.target.value; }, false)} />}
        <span className="st-muted">{isCam25 ? 'mueve toda la pantalla' : layer.type === 'shape' ? SHAPES[layer.shape?.kind] : TYPES[layer.type]}</span>
      </div>
      <Section title={clip === 'base' ? 'Transformar' : `Transformar (claves en ${fmt(t)} s)`} icon="pointer">
        <div className="ue-propgrid">{props.map(prop)}</div>
        {!isCam25 && layer.type !== 'camera' && layer.type !== 'group' && <div className="ue-prop"><span className="st-kfgap" /><span>Ancla</span>
          <div className="st-anchors">{[0, 0.5, 1].flatMap(ay => [0, 0.5, 1].map(ax => (
            <button key={`${ax}${ay}`} className={layer.base.ax === ax && layer.base.ay === ay ? 'on' : ''}
              onClick={() => setL(L => { // cambia el ancla sin mover la capa
                const dx = (ax - L.base.ax) * L.base.w * (L.base.scale || 1), dy = (ay - L.base.ay) * L.base.h * (L.base.scale || 1);
                const r = (L.base.rot || 0) * Math.PI / 180;
                if (mode !== '3d') { L.base.x += dx * Math.cos(r) - dy * Math.sin(r); L.base.y += dx * Math.sin(r) + dy * Math.cos(r); }
                L.base.ax = ax; L.base.ay = ay;
              })} title="Punto de la capa que va en la posicion (centro del giro y la escala)" />)))}
          </div></div>}
        {layer.type === 'camera' && <button disabled={doc.activeCam === layer.id} onClick={() => edit(d => { d.activeCam = layer.id; })}>
          <Icon n="cam" />{doc.activeCam === layer.id ? 'Es la camara inicial' : 'Usar como camara inicial'}</button>}
        {mode === '3d' && !isCam25 && layer.type !== 'camera' && <Row label="Orden de dibujo"><Num v={layer.base.order} onChange={v => setL(L => { L.base.order = v; })} /></Row>}
      </Section>
      {key && <Section title={`Clave en ${fmt(selKey.t)} s`} icon="keyframe">
        <Row label="Curva desde la anterior">
          <select value={key.ease || 'linear'} onChange={e => edit(d => { keyAtClip(d.clips[clip], layer.id, selKey.prop, selKey.t).ease = e.target.value; })}>
            {EASES.filter(([k]) => k !== 'auto' && k !== 'smooth').map(([k, v]) => <option key={k} value={k}>{v}</option>)}
          </select></Row>
        <button onClick={() => { edit(d => removeKey(d, clip, layer.id, selKey.prop, selKey.t)); setSelKey(null); }}><Icon n="trash" />Borrar clave</button>
      </Section>}
      {layer.type === 'text' && <TextProps proj={proj} l={layer} setL={setL} />}
      {layer.type === 'shape' && <ShapeProps l={layer} setL={setL} />}
      {layer.type === 'image' && <Section title="Imagen" icon="tex">
        <div className="st-muted">{layer.image?.custom ? `${layer.image.svg ? 'SVG (vectorial, nitido a cualquier tamano)' : 'Imagen propia'}: ${layer.image.file}`
          : layer.image?.key ? `${layer.image.name}  (${layer.image.pak})` : 'Elige una imagen en Recursos'}</div>
        {layer.image?.w > 0 && <button onClick={() => setL(L => { L.base.w = L.image.w; L.base.h = L.image.h; })}><Icon n="fit" />Tamano original ({layer.image.w} x {layer.image.h})</button>}
        {layer.image?.custom && <button onClick={() => setImgEd(layer.id)}><Icon n="edit" />Editar imagen{layer.image.ops?.length ? ` (${layer.image.ops.length} paso${layer.image.ops.length > 1 ? 's' : ''})` : ''}</button>}
        {!layer.image?.custom && layer.image?.key && <div className="st-muted">Las imagenes del juego no se editan aqui: exportala o usa una propia.</div>}
      </Section>}
      {imgEd === layer.id && layer.image?.custom && <ImageEditor proj={proj} layer={layer} close={() => setImgEd(null)}
        onApply={im => setL(L => { const k = L.image.w ? L.base.w / L.image.w : 1; L.image = im; if (im.w) { L.base.w = Math.round(im.w * k); L.base.h = Math.round(im.h * k); } })} />}
      {!isCam25 && ['image', 'text', 'shape'].includes(layer.type) && <FxProps l={layer} setL={setL} />}
      {!isCam25 && ['image', 'text', 'shape'].includes(layer.type) && <UiProps proj={proj} doc={doc} l={layer} setL={setL} edit={edit} setSid={setSid} />}
      {!isCam25 && ['image', 'text', 'shape'].includes(layer.type) && onScript && <ScriptProps l={layer} onScript={onScript} onDropScript={onDropScript} />}
    </div>
  );
}

function MultiProps({ layers, doc, clip, t, edit }) {
  const ids = layers.map(l => l.id);
  return (
    <div className="ue-props">
      <div className="ue-proptitle"><Icon n="layers" /><b>{layers.length} capas</b></div>
      <Section title="Alinear y distribuir" icon="alignl">
        <AlignBar ids={ids} doc={doc} clip={clip} t={t} edit={edit} />
        <div className="st-muted">Con una sola capa se alinea al lienzo; con varias, entre ellas.</div>
      </Section>
      <Section title="Comun" icon="sliders">
        <Row label="Opacidad"><Num v={stateAt(doc, layers[0], clip, t).alpha ?? 1} step={0.05} min={0} max={1}
          onChange={v => edit(d => { for (const id of ids) { const L = d.layers.find(l => l.id === id); if (L?.base) setProp(d, clip, L, 'alpha', v, t); } })} /></Row>
        <Check v={layers.every(l => l.ui?.focus)} label="Son botones (reciben el foco)"
          onChange={v => edit(d => { for (const id of ids) { const L = d.layers.find(l => l.id === id); if (L?.ui && L.type !== 'group') L.ui.focus = v; } })} />
      </Section>
    </div>);
}

/** alinear y distribuir (2D/2.5D: en pantalla; usa las cajas de layout) */
export function AlignBar({ ids, doc, clip, t, edit, mode = '2d' }) {
  const go = how => edit(d => {
    const items = layout(d, mode, clip, t).filter(it => ids.includes(it.l.id) && !it.group);
    if (!items.length) return;
    const box = it => { const { s } = it; const w = s.w * it.k, h = s.h * it.k; return { x0: it.x - s.ax * w, y0: it.y - s.ay * h, x1: it.x + (1 - s.ax) * w, y1: it.y + (1 - s.ay) * h }; };
    const B = items.map(box);
    const all = items.length === 1 ? { x0: 0, y0: 0, x1: 1280, y1: 720 } : { x0: Math.min(...B.map(b => b.x0)), y0: Math.min(...B.map(b => b.y0)), x1: Math.max(...B.map(b => b.x1)), y1: Math.max(...B.map(b => b.y1)) };
    const mv = (it, dx, dy) => { const L = d.layers.find(l => l.id === it.l.id), s = stateAt(d, L, clip, t); if (dx) setProp(d, clip, L, 'x', Math.round(s.x + dx), t); if (dy) setProp(d, clip, L, 'y', Math.round(s.y + dy), t); };
    if (how.startsWith('dist')) {
      if (items.length < 3) return;
      const hz = how === 'disth', idx = items.map((it, i) => i).sort((a, b) => hz ? B[a].x0 - B[b].x0 : B[a].y0 - B[b].y0);
      const sizes = idx.map(i => hz ? B[i].x1 - B[i].x0 : B[i].y1 - B[i].y0), span = hz ? all.x1 - all.x0 : all.y1 - all.y0;
      const gap = (span - sizes.reduce((a, b) => a + b, 0)) / (items.length - 1);
      let at = hz ? all.x0 : all.y0;
      idx.forEach((i, k) => { const cur = hz ? B[i].x0 : B[i].y0; mv(items[i], hz ? at - cur : 0, hz ? 0 : at - cur); at += sizes[k] + gap; });
      return;
    }
    items.forEach((it, i) => {
      const b = B[i];
      const dx = how === 'l' ? all.x0 - b.x0 : how === 'r' ? all.x1 - b.x1 : how === 'cx' ? (all.x0 + all.x1) / 2 - (b.x0 + b.x1) / 2 : 0;
      const dy = how === 't' ? all.y0 - b.y0 : how === 'b' ? all.y1 - b.y1 : how === 'cy' ? (all.y0 + all.y1) / 2 - (b.y0 + b.y1) / 2 : 0;
      mv(it, dx, dy);
    });
  });
  return (
    <div className="ue-align">
      {[['l', 'alignl', 'Alinear a la izquierda'], ['cx', 'aligncx', 'Centrar en horizontal'], ['r', 'alignr', 'Alinear a la derecha'],
        ['t', 'alignt', 'Alinear arriba'], ['cy', 'aligncy', 'Centrar en vertical'], ['b', 'alignb', 'Alinear abajo'],
        ['disth', 'disth', 'Repartir en horizontal (3 o mas)'], ['distv', 'distv', 'Repartir en vertical (3 o mas)']].map(([k, ic, tt]) =>
        <button key={k} className="st-icon" title={tt} onClick={() => go(k)}><Icon n={ic} /></button>)}
    </div>);
}

function TextProps({ proj, l, setL }) {
  const t = l.text;
  return (
    <Section title="Texto" icon="text">
      <textarea rows="3" value={t.value} onChange={e => setL(L => { L.text.value = e.target.value; })} />
      <Row label="Fuente"><FontPicker proj={proj} value={t.font} onChange={f => setL(L => { L.text.font = f; })} /></Row>
      <div className="ue-pair">
        <Row label="Peso"><select value={t.weight} onChange={e => setL(L => { L.text.weight = +e.target.value; })}>{WEIGHTS.map(([v, n]) => <option key={v} value={v}>{n}</option>)}</select></Row>
        <Row label="Tamano"><Num v={t.px} step={1} min={4} onChange={v => setL(L => { L.text.px = v; })} /></Row>
      </div>
      <div className="ue-pair">
        <Row label="Color"><Color v={t.color} onChange={v => setL(L => { L.text.color = v; })} /></Row>
        <Row label="Interletra"><Num v={t.spacing} step={0.5} onChange={v => setL(L => { L.text.spacing = v; })} /></Row>
      </div>
      <div className="ue-pair">
        <Row label="Interlinea"><Num v={t.line} step={0.05} min={0.5} onChange={v => setL(L => { L.text.line = v; })} /></Row>
        <Row label="Caja"><select value={t.box} onChange={e => setL(L => { L.text.box = e.target.value; })}>
          <option value="auto">Punto (se ajusta)</option><option value="fixed">Parrafo (ancho fijo)</option></select></Row>
      </div>
      <div className="ue-tools-row">
        {[['left', 'alignl', 'Izquierda'], ['center', 'aligncx', 'Centro'], ['right', 'alignr', 'Derecha']].map(([v, ic, tt]) =>
          <Toggle key={v} on={t.align === v} onChange={() => setL(L => { L.text.align = v; })} title={tt}><Icon n={ic} /></Toggle>)}
        <span className="st-sep" />
        <Toggle on={t.italic} onChange={v => setL(L => { L.text.italic = v; })} title="Cursiva"><i>I</i></Toggle>
        <Toggle on={t.upper} onChange={v => setL(L => { L.text.upper = v; })} title="Mayusculas"><b>AA</b></Toggle>
      </div>
    </Section>);
}

function FillEditor({ f, onChange }) {
  return <>
    <Row label="Relleno"><select value={f.type} onChange={e => onChange({ ...f, type: e.target.value })}>
      <option value="none">Ninguno</option><option value="solid">Color</option><option value="linear">Degradado lineal</option><option value="radial">Degradado radial</option></select></Row>
    {f.type !== 'none' && <div className="ue-pair">
      <Row label="Color"><Color v={f.color} onChange={v => onChange({ ...f, color: v })} /></Row>
      <Row label="Opacidad"><Num v={f.alpha ?? 1} step={0.05} min={0} max={1} onChange={v => onChange({ ...f, alpha: v })} /></Row>
    </div>}
    {(f.type === 'linear' || f.type === 'radial') && <div className="ue-pair">
      <Row label="Color 2"><Color v={f.color2} onChange={v => onChange({ ...f, color2: v })} /></Row>
      <Row label="Opacidad 2"><Num v={f.alpha2 ?? 1} step={0.05} min={0} max={1} onChange={v => onChange({ ...f, alpha2: v })} /></Row>
    </div>}
    {f.type === 'linear' && <Row label="Angulo"><Num v={f.angle ?? 90} step={5} onChange={v => onChange({ ...f, angle: v })} /></Row>}
  </>;
}

function ShapeProps({ l, setL }) {
  const sh = l.shape;
  return (
    <Section title="Forma" icon="vector">
      <Row label="Tipo"><select value={sh.kind} onChange={e => setL(L => { L.shape.kind = e.target.value; })}>
        {Object.entries(SHAPES).filter(([k]) => k !== 'path' || sh.kind === 'path').map(([k, v]) => <option key={k} value={k}>{v}</option>)}</select></Row>
      {sh.kind === 'rect' && <Row label="Esquinas"><Num v={sh.radius} step={1} min={0} onChange={v => setL(L => { L.shape.radius = v; })} /></Row>}
      {(sh.kind === 'polygon' || sh.kind === 'star') && <div className="ue-pair">
        <Row label="Lados"><Num v={sh.sides} step={1} min={3} max={64} onChange={v => setL(L => { L.shape.sides = Math.round(v); })} /></Row>
        {sh.kind === 'star' && <Row label="Interior"><Num v={sh.inner} step={0.05} min={0.05} max={1} onChange={v => setL(L => { L.shape.inner = v; })} /></Row>}
      </div>}
      {sh.kind === 'path' && <Check v={sh.closed} label="Trazado cerrado" onChange={v => setL(L => { L.shape.closed = v; })} />}
      {sh.kind !== 'line' && <FillEditor f={sh.fill} onChange={f => setL(L => { L.shape.fill = f; })} />}
      <div className="ue-pair">
        <Row label="Trazo"><Num v={sh.stroke.width} step={1} min={0} onChange={v => setL(L => { L.shape.stroke.width = v; })} /></Row>
        <Row label="Color"><Color v={sh.stroke.color} onChange={v => setL(L => { L.shape.stroke.color = v; })} /></Row>
      </div>
      <div className="ue-pair">
        <Row label="Opacidad trazo"><Num v={sh.stroke.alpha ?? 1} step={0.05} min={0} max={1} onChange={v => setL(L => { L.shape.stroke.alpha = v; })} /></Row>
        <Row label="Discontinuo"><Num v={sh.stroke.dash || 0} step={1} min={0} onChange={v => setL(L => { L.shape.stroke.dash = v; })} /></Row>
      </div>
    </Section>);
}

function FxProps({ l, setL }) {
  const fx = l.fx;
  const on = !!(fx.shadow.on || fx.glow.on || fx.stroke.on || fx.blur);
  const sub = (k, fn) => setL(L => { fn(L.fx[k]); });
  return (
    <Section title="Estilos de capa" icon="fx" open={on}>
      <div className="ue-fx">
        <Check v={fx.shadow.on} label="Sombra" onChange={v => sub('shadow', o => { o.on = v; })} />
        {fx.shadow.on && <>
          <div className="ue-pair"><Row label="X"><Num v={fx.shadow.dx} onChange={v => sub('shadow', o => { o.dx = v; })} /></Row><Row label="Y"><Num v={fx.shadow.dy} onChange={v => sub('shadow', o => { o.dy = v; })} /></Row></div>
          <div className="ue-pair"><Row label="Desenfoque"><Num v={fx.shadow.blur} min={0} onChange={v => sub('shadow', o => { o.blur = v; })} /></Row><Row label="Color"><Color v={fx.shadow.color} onChange={v => sub('shadow', o => { o.color = v; })} /></Row></div>
          <Row label="Opacidad"><Num v={fx.shadow.alpha} step={0.05} min={0} max={1} onChange={v => sub('shadow', o => { o.alpha = v; })} /></Row>
        </>}
        <Check v={fx.glow.on} label="Resplandor exterior" onChange={v => sub('glow', o => { o.on = v; })} />
        {fx.glow.on && <div className="ue-pair"><Row label="Tamano"><Num v={fx.glow.size} min={0} onChange={v => sub('glow', o => { o.size = v; })} /></Row><Row label="Color"><Color v={fx.glow.color} onChange={v => sub('glow', o => { o.color = v; })} /></Row></div>}
        {fx.glow.on && <Row label="Opacidad"><Num v={fx.glow.alpha} step={0.05} min={0} max={1} onChange={v => sub('glow', o => { o.alpha = v; })} /></Row>}
        <Check v={fx.stroke.on} label="Contorno" onChange={v => sub('stroke', o => { o.on = v; })} />
        {fx.stroke.on && <div className="ue-pair"><Row label="Grosor"><Num v={fx.stroke.width} min={0} onChange={v => sub('stroke', o => { o.width = v; })} /></Row><Row label="Color"><Color v={fx.stroke.color} onChange={v => sub('stroke', o => { o.color = v; })} /></Row></div>}
        <Row label="Desenfoque"><Num v={fx.blur || 0} step={0.5} min={0} onChange={v => setL(L => { L.fx.blur = v; })} /></Row>
      </div>
    </Section>);
}

// Script de la capa (como un componente de Unity): sin editar es el codigo de su panel; editado, codigo propio
function ScriptProps({ l, onScript, onDropScript }) {
  const has = l.script?.source != null;
  return (
    <Section title="Script" icon="code" open={has}>
      <div className="st-muted ue-scripthelp">{has
        ? 'Esta capa tiene codigo propio: lo que hace al enfocarla y elegirla lo decide su script.'
        : l.ui?.focus
          ? 'Su codigo ya existe: es lo que tiene en este panel (accion y efecto de foco). Editalo para cambiarlo en Python.'
          : 'Su codigo ya existe y esta vacio: editalo para darle eventos, efectos y animaciones.'}</div>
      <div className="ue-inline">
        <button onClick={() => onScript('l:' + l.id)}><Icon n="code" />{has ? 'Editar el codigo' : 'Ver y editar el codigo'}</button>
        {has && <button onClick={() => onDropScript(l.id)} title="Borra su codigo y el panel vuelve a hacer lo que hacia"><Icon n="undo" />Volver al panel</button>}
      </div>
    </Section>);
}

function UiProps({ proj, doc, l, setL, setSid }) {
  const ui = l.ui;
  const a = ui.action || {};
  const F = focusables(doc);
  const setA = fn => setL(L => { fn(L.ui.action); });
  return (
    <Section title="Interaccion" icon="gamepad" open={!!ui.focus || !!l.showOnFocus || !!l.showIf}>
      <Check v={ui.focus} label="Es un boton (recibe el foco con el mando o el raton)" onChange={v => setL(L => { L.ui.focus = v; })} />
      {ui.focus && <>
        <Row label="Al pulsar"><select value={a.type} onChange={e => setA(o => { o.type = e.target.value; })}>{ACTIONS.map(([k, v]) => <option key={k} value={k}>{v}</option>)}</select></Row>
        {a.type === 'goto' && <Row label="Pantalla"><div className="ue-inline">
          <select value={a.screen || ''} onChange={e => setA(o => { o.screen = e.target.value; })}>
            <option value="">(elige)</option>{proj.screens.filter(s => s.id !== doc.id).map(s => <option key={s.id} value={s.id}>{s.name}</option>)}</select>
          {a.screen && <button className="st-icon" title="Ir a esa pantalla en el editor" onClick={() => setSid(a.screen)}><Icon n="chev" /></button>}
        </div></Row>}
        {a.type === 'script' && <>
          <Row label="Script"><input value={a.script || ''} placeholder="nombre param = valor" onChange={e => setA(o => { o.script = e.target.value; })} /></Row>
          <Row label="Habituales"><select value="" onChange={e => e.target.value && setA(o => { o.script = e.target.value; })}>
            <option value="">(elige uno)</option>{SCRIPT_PRESETS.map(([v, n]) => <option key={v} value={v}>{n}</option>)}</select></Row>
        </>}
        {a.type === 'set' && <div className="ue-pair">
          <Row label="Variable"><input value={a.global || ''} placeholder="mi_variable" onChange={e => setA(o => { o.global = clean(e.target.value); })} /></Row>
          <Row label="Valor"><Num v={a.value ?? 1} onChange={v => setA(o => { o.value = Math.round(v); })} /></Row>
        </div>}
        <div className="ue-pair">
          <Row label="Orden"><Num v={ui.order || 0} onChange={v => setL(L => { L.ui.order = v; })} /></Row>
          <Check v={ui.sound} label="Sonidos del menu" onChange={v => setL(L => { L.ui.sound = v; })} />
        </div>
        <div className="ue-subhead">Con el foco</div>
        <div className="ue-pair">
          <Row label="Escala"><Num v={ui.hover.scale} step={0.01} min={0.1} onChange={v => setL(L => { L.ui.hover.scale = v; })} /></Row>
          <Row label="Tinte"><Color v={ui.hover.color} onChange={v => setL(L => { L.ui.hover.color = v; })} /></Row>
        </div>
        <div className="ue-pair">
          <Row label="Mover X"><Num v={ui.hover.dx} onChange={v => setL(L => { L.ui.hover.dx = v; })} /></Row>
          <Row label="Mover Y"><Num v={ui.hover.dy} onChange={v => setL(L => { L.ui.hover.dy = v; })} /></Row>
        </div>
        <Row label="Opacidad"><Num v={ui.hover.alpha} step={0.05} min={0} max={1} onChange={v => setL(L => { L.ui.hover.alpha = v; })} /></Row>
        <Check v={doc.focusStart === l.id} label="Boton con el foco al entrar en la pantalla" onChange={v => setL((L, d) => { d.focusStart = v ? L.id : null; })} />
      </>}
      {!ui.focus && <Row label="Visible solo con el foco de"><select value={l.showOnFocus || ''} onChange={e => setL(L => { L.showOnFocus = e.target.value || null; })}>
        <option value="">(siempre visible)</option>{F.map(b => <option key={b.id} value={b.id}>{b.name}</option>)}</select></Row>}
      <Check v={!!l.showIf} label="Mostrar segun una variable del juego" onChange={v => setL(L => { L.showIf = v ? { global: '', op: '=', value: 1 } : null; })} />
      {l.showIf && <div className="ue-pair3">
        <input value={l.showIf.global} placeholder="variable" onChange={e => setL(L => { L.showIf.global = clean(e.target.value); })} />
        <select value={l.showIf.op} onChange={e => setL(L => { L.showIf.op = e.target.value; })}>{['=', '!=', '>', '<', '>=', '<='].map(o => <option key={o}>{o}</option>)}</select>
        <Num v={l.showIf.value} onChange={v => setL(L => { L.showIf.value = Math.round(v); })} />
      </div>}
    </Section>);
}

function ScreenProps({ proj, doc, mode, edit, editProj, onScript, onImport }) {
  const F = focusables(doc);
  return (
    <div className="ue-props">
      <div className="ue-proptitle"><Icon n="screens" /><input value={doc.name} onChange={e => edit(d => { d.name = e.target.value; }, false)} /><span className="st-muted">pantalla</span></div>
      <Section title="Pantalla" icon="screen">
        <Row label="Boton Atras"><select value={doc.onBack || 'auto'} onChange={e => edit(d => { d.onBack = e.target.value; })}>
          <option value="auto">Volver (en la primera: cerrar)</option><option value="close">Cerrar la interfaz</option><option value="none">No hace nada</option></select></Row>
        <Row label="Foco al entrar"><select value={doc.focusStart || ''} onChange={e => edit(d => { d.focusStart = e.target.value || null; })}>
          <option value="">(el primero)</option>{F.map(b => <option key={b.id} value={b.id}>{b.name}</option>)}</select></Row>
        <button disabled={proj.start === doc.id} onClick={() => editProj(p => { p.start = doc.id; })}><Icon n="play" />{proj.start === doc.id ? 'Es la pantalla inicial' : 'Usar como pantalla inicial'}</button>
        <div className="st-muted">{F.length} boton(es). Elige una capa para editarla; con nada elegido ves la pantalla y el proyecto.</div>
      </Section>
      {onImport && mode !== '3d' && <Section title="Importar diseno" icon="upload">
        <div className="st-muted ue-scripthelp">Disena la pantalla en Photoshop (PSD) o Illustrator (AI) y traela: cada capa del archivo llega como una capa aqui, en su sitio, con sus grupos, opacidad y visibilidad. Tambien puedes soltar el archivo encima del editor.</div>
        <div className="ue-inline">
          <button onClick={() => onImport(false)}><Icon n="upload" />En esta pantalla</button>
          <button onClick={() => onImport(true)}><Icon n="screens" />En una pantalla nueva</button>
        </div>
      </Section>}
      {onScript && <Section title="Scripts" icon="code">
        <div className="st-muted ue-scripthelp">La pantalla tiene su script y el proyecto uno general, que apunta a la pantalla principal.</div>
        <div className="ue-inline">
          <button onClick={() => onScript('s:' + doc.id)}><Icon n="screen" />Script de la pantalla</button>
          <button onClick={() => onScript('p')}><Icon n="package" />Script del proyecto</button>
        </div>
      </Section>}
      <Section title="Proyecto" icon="sliders">
        <Row label="Calidad de imagen" title="Resolucion de las imagenes que se generan para el juego (1 = 720p, 2 = 1440p, 3 = 4K)">
          <select value={proj.settings.quality} onChange={e => editProj(p => { p.settings.quality = +e.target.value; })}>
            <option value={1}>1x (720p)</option><option value={2}>2x (1440p)</option><option value={3}>3x (4K)</option></select></Row>
        <Check v={proj.settings.capture} label="Capturar el mando mientras esta abierta (si tiene botones)" onChange={v => editProj(p => { p.settings.capture = v; })} />
        <div className="st-muted">Desde la logica del juego: <code>re_ui_{clean(proj.name)}_show</code>, <code>_hide</code>, <code>_goto screen = n</code> y <code>re_ui_{clean(proj.name)}_active</code>.</div>
      </Section>
    </div>);
}

// ===================================================================== capas
export function LayersPanel({ proj, doc, mode, sel, setSel, edit, onAdd, onRemove }) {
  const [open, setOpen] = useState({});
  const [over, setOver] = useState(null);
  const [renaming, setRenaming] = useState(null);
  const urlOf = l => layerUrl(proj, l);
  const ver = useRasterVersion();
  const rows = [];
  const walk = (parent, depth) => {
    for (const l of childrenOf(doc, parent).sort((a, b) => (b.base.order || 0) - (a.base.order || 0))) {
      rows.push([l, depth]);
      if (l.type === 'group' && open[l.id] !== false) walk(l.id, depth + 1);
    }
  };
  walk(null, 0);
  const click = (e, id) => {
    if (e.ctrlKey || e.metaKey) setSel(sel.includes(id) ? sel.filter(x => x !== id) : [...sel, id]);
    else if (e.shiftKey && sel.length) {
      const ids = rows.map(r => r[0].id), a = ids.indexOf(sel[sel.length - 1]), b = ids.indexOf(id);
      setSel(ids.slice(Math.min(a, b), Math.max(a, b) + 1));
    } else setSel([id]);
  };
  const flip = (id, key) => edit(d => { const q = d.layers.find(x => x.id === id); q[key] = key === 'visible' ? q.visible === false : !q[key]; });
  const drop = (target, where) => {
    const ids = sel.length ? sel : [];
    if (!ids.length) return;
    edit(d => moveLayers(d, ids, target, where, mode));
  };
  return (
    <div className="ue-layers">
      <div className="ue-list">
        {mode === '25d' && <div className={'ue-lrow' + (sel.includes(CAM25) ? ' on' : '')} onClick={() => setSel([CAM25])}>
          <span className="ue-eye" /><span className="ue-thumb"><Icon n="cam" s={14} /></span><span className="ue-lname">Camara 2.5D</span></div>}
        {!rows.length && <div className="st-muted ue-pad">Sin capas. Dibuja con las herramientas de la izquierda o elige una imagen en Recursos.</div>}
        {rows.map(([l, depth]) => {
          const tw = 34, th = 22, s = l.base;
          const scale = l.type !== 'group' && l.type !== 'camera' && s.w > 0 && s.h > 0 ? Math.min(tw / s.w, th / s.h) * 0.9 : 0;
          return (
            <div key={l.id} draggable className={'ue-lrow' + (sel.includes(l.id) ? ' on' : '') + (l.visible === false ? ' hidden' : '') + (over?.id === l.id ? ' drop-' + over.where : '')}
              style={{ paddingLeft: 4 + depth * 14 }} onClick={e => click(e, l.id)}
              onDragStart={e => { if (!sel.includes(l.id)) setSel([l.id]); e.dataTransfer.effectAllowed = 'move'; }}
              onDragOver={e => {
                e.preventDefault();
                const r = e.currentTarget.getBoundingClientRect(), f = (e.clientY - r.top) / r.height;
                const where = l.type === 'group' && f > 0.3 && f < 0.7 ? 'inside' : f < 0.5 ? 'above' : 'below';
                if (over?.id !== l.id || over?.where !== where) setOver({ id: l.id, where });
              }}
              onDragLeave={() => setOver(null)}
              onDrop={e => { e.preventDefault(); setOver(null); drop(l.id, over?.where || 'above'); }}>
              <button className="st-icon ue-eye" title={l.visible === false ? 'Mostrar' : 'Ocultar (no se exporta)'} onClick={e => { e.stopPropagation(); flip(l.id, 'visible'); }}><Icon n={l.visible === false ? 'eyeoff' : 'eye'} s={14} /></button>
              {l.type === 'group'
                ? <button className="st-icon" onClick={e => { e.stopPropagation(); setOpen(o => ({ ...o, [l.id]: o[l.id] === false })); }}><Icon n={open[l.id] === false ? 'chev' : 'chevdown'} s={12} /></button>
                : null}
              <span className="ue-thumb" key={ver}>
                {scale > 0 ? <span style={{ position: 'relative', width: s.w * scale, height: s.h * scale }}><LayerCanvas l={l} w={s.w} h={s.h} scale={scale} urlOf={urlOf} /></span>
                  : <Icon n={TYPE_ICON[l.type]} s={14} />}
              </span>
              {renaming === l.id
                ? <input className="ue-lname" autoFocus defaultValue={l.name} onClick={e => e.stopPropagation()}
                    onBlur={e => { const v = e.target.value.trim(); setRenaming(null); if (v) edit(d => { d.layers.find(q => q.id === l.id).name = v; }); }}
                    onKeyDown={e => { e.stopPropagation(); if (e.key === 'Enter') e.target.blur(); if (e.key === 'Escape') setRenaming(null); }} />
                : <span className="ue-lname" onDoubleClick={e => { e.stopPropagation(); setRenaming(l.id); }} title="Doble clic para renombrar">{l.name}</span>}
              {l.ui?.focus && <Icon n="target" s={13} className="ue-badge" />}
              {l.fx && (l.fx.shadow?.on || l.fx.glow?.on || l.fx.stroke?.on || l.fx.blur) ? <Icon n="fx" s={13} className="ue-badge" /> : null}
              {l.type === 'camera' && doc.activeCam === l.id && <span className="st-tag">INICIAL</span>}
              <button className="st-icon" title={l.locked ? 'Desbloquear' : 'Bloquear'} onClick={e => { e.stopPropagation(); flip(l.id, 'locked'); }}><Icon n={l.locked ? 'lock' : 'unlock'} s={13} /></button>
            </div>);
        })}
      </div>
      <div className="ue-lbar">
        <button className="st-icon" title="Grupo nuevo (con las capas elegidas: Ctrl+G)" onClick={() => onAdd('group')}><Icon n="folder" /></button>
        <button className="st-icon" title="Rectangulo nuevo" onClick={() => onAdd('shape')}><Icon n="rect" /></button>
        <button className="st-icon" title="Texto nuevo" onClick={() => onAdd('text')}><Icon n="text" /></button>
        {mode === '3d' && <button className="st-icon" title="Camara nueva" onClick={() => onAdd('camera')}><Icon n="cam" /></button>}
        <span className="st-grow" />
        <button className="st-icon" title="Borrar (Supr)" disabled={!sel.length} onClick={() => onRemove(sel)}><Icon n="trash" /></button>
      </div>
    </div>);
}

/** mueve capas encima/debajo de otra o dentro de un grupo (mantiene su sitio en pantalla en 2D) */
export function moveLayers(d, ids, targetId, where, mode) {
  const target = d.layers.find(l => l.id === targetId);
  if (!target) return;
  const moving = ids.map(id => d.layers.find(l => l.id === id)).filter(l => l && l.id !== targetId);
  if (moving.some(m => descendants(d, m.id).has(targetId))) return;     // un grupo no puede entrar en si mismo
  const parent = where === 'inside' ? target.id : target.parent || null;
  const offset = l => { let x = 0, y = 0; for (const a of ancestors(d, l)) { x += a.base.x; y += a.base.y; } return [x, y]; };
  for (const m of moving) {
    if ((m.parent || null) === parent) continue;
    if (mode !== '3d') {
      const [ox, oy] = offset(m);
      m.parent = parent;
      const [nx, ny] = offset(m);
      m.base.x += ox - nx; m.base.y += oy - ny;
    } else m.parent = parent;
  }
  const sibs = d.layers.filter(l => (l.parent || null) === parent && !moving.includes(l)).sort((a, b) => (a.base.order || 0) - (b.base.order || 0));
  let at = where === 'inside' ? sibs.length : sibs.indexOf(target) + (where === 'above' ? 1 : 0);
  sibs.splice(Math.max(0, at), 0, ...moving.sort((a, b) => (a.base.order || 0) - (b.base.order || 0)));
  sibs.forEach((l, i) => { l.base.order = i + 1; });
}

// ===================================================================== pantallas
export function ScreensPanel({ proj, sid, setSid, editProj, addScreen, dupScreen, removeScreen, onImport }) {
  return (
    <div className="ue-screens">
      <div className="ue-list">
        {proj.screens.map(s => {
          const links = [...new Set(s.layers.filter(l => l.ui?.focus && l.ui.action?.type === 'goto').map(l => proj.screens.find(q => q.id === l.ui.action.screen)?.name).filter(Boolean))];
          return (
            <div key={s.id} className={'ue-scr' + (s.id === sid ? ' on' : '')} onClick={() => setSid(s.id)}>
              <Thumb proj={proj} doc={s} />
              <div className="ue-scrinfo">
                <input value={s.name} onClick={e => e.stopPropagation()} onChange={e => editProj(p => { p.screens.find(q => q.id === s.id).name = e.target.value; }, false)} />
                <span className="st-muted">{s.layers.length} capas, {focusables(s).length} botones{proj.start === s.id ? ' . PRINCIPAL' : ''}</span>
                {links.length > 0 && <span className="st-muted ue-links"><Icon n="flow" s={12} />{links.join(', ')}</span>}
              </div>
            </div>);
        })}
      </div>
      <div className="ue-lbar">
        <button className="st-icon" title="Pantalla nueva" onClick={addScreen}><Icon n="plus" /></button>
        <button className="st-icon" title="Duplicar la pantalla" onClick={dupScreen}><Icon n="duplicate" /></button>
        {onImport && <button className="st-icon" title="Pantalla nueva desde un PSD (Photoshop) o AI (Illustrator)" onClick={() => onImport(true)}><Icon n="upload" /></button>}
        <button className="st-icon" title="Usar como pantalla principal" onClick={() => editProj(p => { p.start = sid; })}><Icon n="play" /></button>
        <button className="st-icon" title="Subir la pantalla en la lista" onClick={() => editProj(p => { const i = p.screens.findIndex(x => x.id === sid); if (i > 0) [p.screens[i - 1], p.screens[i]] = [p.screens[i], p.screens[i - 1]]; })}><Icon n="up" /></button>
        <button className="st-icon" title="Bajar la pantalla en la lista" onClick={() => editProj(p => { const i = p.screens.findIndex(x => x.id === sid); if (i >= 0 && i < p.screens.length - 1) [p.screens[i + 1], p.screens[i]] = [p.screens[i], p.screens[i + 1]]; })}><Icon n="down" /></button>
        <span className="st-grow" />
        <button className="st-icon" title="Borrar la pantalla" disabled={proj.screens.length < 2} onClick={removeScreen}><Icon n="trash" /></button>
      </div>
    </div>);
}

/** miniatura de una pantalla (2D y 2.5D: las capas dibujadas en pequeno) */
export function Thumb({ proj, doc, w = 112 }) {
  const ref = useRef(null);
  const ver = useRasterVersion();
  const h = w * 9 / 16;
  useEffect(() => {
    const c = ref.current, dpr = window.devicePixelRatio || 1;
    c.width = w * dpr; c.height = h * dpr;
    const g = c.getContext('2d');
    g.scale(dpr, dpr);
    g.fillStyle = '#000'; g.fillRect(0, 0, w, h);
    if (proj.mode === '3d') { g.fillStyle = '#556'; g.font = '11px Inter Variable'; g.fillText('3D', w / 2 - 7, h / 2 + 4); return; }
    const z = w / 1280;
    for (const it of layout(doc, proj.mode, 'base', 0)) {
      if (it.group) continue;
      const { l, s } = it, K = it.k * z;
      const r = cachedRaster(l, s.w, s.h, K * dpr, x => layerUrl(proj, x));
      g.save();
      g.globalAlpha = Math.max(0, Math.min(1, it.alpha));
      g.translate(it.x * z, it.y * z); g.rotate(it.rot * Math.PI / 180);
      g.drawImage(r.canvas, (-s.ax * s.w - r.pad) * K, (-s.ay * s.h - r.pad) * K, (s.w + 2 * r.pad) * K, (s.h + 2 * r.pad) * K);
      g.restore();
    }
  }, [doc, ver, proj.mode]);
  return <canvas ref={ref} className="ue-sthumb" style={{ width: w, height: h }} />;
}

// ===================================================================== recursos
export function AssetsPanel({ proj, editProj, onPickGame, onPickAsset, onPickFont, toast, onOpenWindow }) {
  const [tab, setTab] = useState('own');
  const [list, setList] = useState(null);
  const reload = () => api(`/api/ui/assets?pid=${encodeURIComponent(proj.id)}`).then(r => setList(r.assets)).catch(e => toast(e.message, 'err'));
  useEffect(() => { reload(); }, [proj.id]);
  const upload = async files => {
    const r = await uploadFiles(proj, files, editProj, toast);
    for (const a of r) if (a.kind === 'font') toast(`Fuente ${a.family} lista: eligela en un texto`);
    reload();
  };
  return (
    <div className="ue-assets">
      <div className="ue-subtabs">
        <button className={tab === 'own' ? 'on' : ''} onClick={() => setTab('own')}>Del proyecto</button>
        <button className={tab === 'game' ? 'on' : ''} onClick={() => setTab('game')}>Del juego</button>
        <span className="st-grow" />
        <button className="st-icon" onClick={onOpenWindow} title="Abrir la ventana grande de recursos (Ctrl+Mayus+R)"><Icon n="max" s={14} /></button>
      </div>
      {tab === 'game' && <AssetBrowser title="" warnNotLoaded={false} onError={m => toast(m, 'err')} onPick={onPickGame} />}
      {tab === 'own' && <div className="ue-own">
        <label className="st-file ue-upload"><Icon n="upload" />Importar PNG, JPG, SVG o fuente
          <input type="file" multiple accept={ACCEPT} onChange={e => { const f = [...e.target.files]; e.target.value = ''; upload(f); }} /></label>
        <div className="st-muted ue-pad">Tambien puedes soltar archivos en cualquier parte del editor. El SVG es vectorial: se ve nitido a cualquier tamano y el juego recibe la imagen a la calidad del proyecto.</div>
        <div className="st-grid">
          {list === null && <div className="st-muted">Leyendo...</div>}
          {list?.length === 0 && <div className="st-muted">Aun no hay archivos propios.</div>}
          {list?.map(a => (
            <button key={a.file} className="st-thumb" title={`${a.file}  ${a.w ? a.w + ' x ' + a.h : ''}`}
              onClick={() => a.kind === 'font' ? onPickFont(a.file.replace(/\.[a-z0-9]+$/, '')) : onPickAsset(a)}>
              {a.kind === 'font' ? <span className="ue-fontsample" style={{ fontFamily: `"${a.file.replace(/\.[a-z0-9]+$/, '')}"` }}>Aa</span>
                : <img loading="lazy" src={assetUrl(proj, a.file)} alt="" />}
              <span>{a.kind === 'svg' ? 'SVG  ' : ''}{a.file}</span>
            </button>))}
        </div>
      </div>}
    </div>);
}
