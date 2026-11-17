// Marco del editor de interfaces al estilo de un programa de diseno: barra de menus, barra de herramientas
// vertical y barra de opciones de la herramienta (o de la capa elegida).
import React, { useEffect, useRef, useState } from 'react';
import { Icon } from '../ui/icons.jsx';
import { Color, Num } from '../studio/fields.jsx';
import { MODES } from './model.js';
import { AlignBar } from './panels.jsx';
import { FontPicker } from './resources.jsx';

// ===================================================================== menus
export function MenuBar({ menus, right }) {
  const [open, setOpen] = useState(null);
  const ref = useRef(null);
  useEffect(() => {
    if (open === null) return;
    const close = e => { if (!ref.current?.contains(e.target)) setOpen(null); };
    const esc = e => { if (e.key === 'Escape') setOpen(null); };
    window.addEventListener('pointerdown', close); window.addEventListener('keydown', esc);
    return () => { window.removeEventListener('pointerdown', close); window.removeEventListener('keydown', esc); };
  }, [open]);
  return (
    <header className="ue-menubar" ref={ref}>
      <a className="ue-brand" href="index.html" title="Inicio de DE Studio"><Icon n="screen" s={16} /><b>DE Studio</b></a>
      {menus.map((m, i) => (
        <div key={m.label} className="ue-menu">
          <button className={open === i ? 'on' : ''} onClick={() => setOpen(open === i ? null : i)} onMouseEnter={() => open !== null && setOpen(i)}>{m.label}</button>
          {open === i && <MenuList items={m.items} close={() => setOpen(null)} />}
        </div>))}
      <span className="st-grow" />
      {right}
    </header>
  );
}
function MenuList({ items, close }) {
  const [sub, setSub] = useState(null);
  return (
    <div className="ue-dropdown">
      {items.filter(Boolean).map((it, i) => it.sep ? <div key={i} className="ue-msep" /> : (
        <div key={i} className="ue-mitem-wrap" onMouseEnter={() => setSub(it.sub ? i : null)}>
          <button className="ue-mitem" disabled={it.disabled} onClick={() => { if (it.sub) return; close(); it.act?.(); }}>
            <span className="ue-mcheck">{it.checked ? <Icon n="check" s={13} /> : it.icon ? <Icon n={it.icon} s={14} /> : null}</span>
            <span className="ue-mlabel">{it.label}</span>
            {it.kbd && <kbd>{it.kbd}</kbd>}
            {it.sub && <Icon n="chev" s={12} />}
          </button>
          {it.sub && sub === i && <div className="ue-submenu"><MenuList items={it.sub} close={close} /></div>}
        </div>))}
    </div>
  );
}

// ===================================================================== herramientas
export const TOOLS_2D = [
  ['move', 'pointer', 'Mover y seleccionar', 'V'], ['hand', 'hand', 'Mano (o barra espaciadora)', 'H'], ['zoom', 'zoomin', 'Zoom (Alt: alejar)', 'Z'], null,
  ['text', 'text', 'Texto (clic: punto, arrastrar: parrafo)', 'T'], ['rect', 'rect', 'Rectangulo (Mayus: cuadrado, Alt: desde el centro)', 'U'],
  ['ellipse', 'ellipse', 'Elipse', 'E'], ['polygon', 'polygon', 'Poligono', 'Y'], ['star', 'star', 'Estrella', 'K'], ['line', 'line', 'Linea (Mayus: 45 grados)', 'L'],
  ['pen', 'pen', 'Pluma: clic = esquina, arrastrar = curva, clic en el primero = cerrar, Intro = terminar', 'P'],
];
export const TOOL_KEYS = Object.fromEntries(TOOLS_2D.filter(Boolean).map(t => [t[3].toLowerCase(), t[0]]));

export function ToolBar({ mode, tool, setTool, onCreate3d, fill, stroke, setOpts }) {
  if (mode === '3d') {
    return (
      <nav className="ue-toolbar">
        <div className="ue-tooltip-note" title="En 3D: G mover, R girar, S escalar en la vista">3D</div>
        {[['text', 'text', 'Texto'], ['rect', 'rect', 'Rectangulo'], ['ellipse', 'ellipse', 'Elipse'], ['polygon', 'polygon', 'Poligono'], ['star', 'star', 'Estrella'],
          ['image', 'tex', 'Imagen (elige en Recursos)'], ['group', 'folder', 'Grupo'], ['camera', 'cam', 'Camara']].map(([k, ic, tt]) =>
          <button key={k} title={`Anadir ${tt.toLowerCase()} en el centro`} onClick={() => onCreate3d(k)}><Icon n={ic} s={18} /></button>)}
      </nav>);
  }
  return (
    <nav className="ue-toolbar">
      {TOOLS_2D.map((t, i) => t ? (
        <button key={t[0]} className={tool === t[0] ? 'on' : ''} onClick={() => setTool(t[0])} title={`${t[2]}  (${t[3]})`}><Icon n={t[1]} s={18} /></button>
      ) : <div key={i} className="ue-tsep" />)}
      <div className="ue-tsep" />
      <div className="ue-swatches" title="Relleno y trazo de las formas nuevas">
        <label className="ue-sw fill" style={{ background: `rgb(${fill.join(',')})` }}><input type="color" value={hex(fill)} onChange={e => setOpts(o => { o.shape.fill.color = unhex(e.target.value); })} /></label>
        <label className="ue-sw stroke" style={{ borderColor: `rgb(${stroke.join(',')})` }}><input type="color" value={hex(stroke)} onChange={e => setOpts(o => { o.shape.stroke.color = unhex(e.target.value); })} /></label>
      </div>
    </nav>
  );
}
const hex = c => '#' + c.map(v => Math.round(v).toString(16).padStart(2, '0')).join('');
const unhex = h => [1, 3, 5].map(i => parseInt(h.slice(i, i + 2), 16));

// ===================================================================== opciones de la herramienta
export function OptionsBar({ proj, mode, tool, opts, setOpts, selLayers, editSel, doc, clip, t, edit, view, zoomTo }) {
  const textSel = selLayers.filter(l => l.type === 'text');
  const shapeSel = selLayers.filter(l => l.type === 'shape');
  // con una capa del tipo elegida, la barra edita la capa; si no, lo que tendran las nuevas
  const T = textSel.length ? textSel[0].text : opts.text;
  const setT = fn => { if (textSel.length) editSel(L => { if (L.type === 'text') fn(L.text); }); else setOpts(o => fn(o.text)); };
  const SH = shapeSel.length ? shapeSel[0].shape : opts.shape;
  const setSH = fn => { if (shapeSel.length) editSel(L => { if (L.type === 'shape') fn(L.shape); }); else setOpts(o => fn(o.shape)); };
  const isShapeTool = ['rect', 'ellipse', 'polygon', 'star', 'line', 'pen'].includes(tool);
  const showText = tool === 'text' || (tool === 'move' && textSel.length > 0);
  const showShape = isShapeTool || (tool === 'move' && shapeSel.length > 0);
  return (
    <div className="ue-options">
      <span className="ue-optlabel">{showText ? 'Texto' : showShape ? 'Forma' : tool === 'zoom' ? 'Zoom' : tool === 'hand' ? 'Mano' : mode === '3d' ? 'Vista 3D' : 'Mover'}</span>
      {showText && <>
        <FontPicker proj={proj} value={T.font} compact onChange={f => setT(x => { x.font = f; })} />
        <select value={T.weight} onChange={e => setT(x => { x.weight = +e.target.value; })}>
          {[300, 400, 500, 600, 700, 800, 900].map(w => <option key={w} value={w}>{w}</option>)}</select>
        <span className="ue-optf"><Icon n="font" s={14} /><Num v={T.px} min={4} onChange={v => setT(x => { x.px = v; })} /></span>
        <Color v={T.color} onChange={v => setT(x => { x.color = v; })} />
        {[['left', 'alignl'], ['center', 'aligncx'], ['right', 'alignr']].map(([a, ic]) =>
          <button key={a} className={'st-icon' + (T.align === a ? ' on' : '')} onClick={() => setT(x => { x.align = a; })}><Icon n={ic} /></button>)}
      </>}
      {showShape && <>
        <span className="ue-optf">Relleno
          <select value={SH.fill.type} onChange={e => setSH(x => { x.fill.type = e.target.value; })}>
            <option value="none">Ninguno</option><option value="solid">Color</option><option value="linear">Lineal</option><option value="radial">Radial</option></select>
          {SH.fill.type !== 'none' && <Color v={SH.fill.color} onChange={v => setSH(x => { x.fill.color = v; })} />}
          {(SH.fill.type === 'linear' || SH.fill.type === 'radial') && <Color v={SH.fill.color2} onChange={v => setSH(x => { x.fill.color2 = v; })} />}
        </span>
        <span className="ue-optf">Trazo<Num v={SH.stroke.width} min={0} onChange={v => setSH(x => { x.stroke.width = v; })} /><Color v={SH.stroke.color} onChange={v => setSH(x => { x.stroke.color = v; })} /></span>
        {(tool === 'rect' || SH.kind === 'rect') && <span className="ue-optf">Esquinas<Num v={SH.radius} min={0} onChange={v => setSH(x => { x.radius = v; })} /></span>}
        {(['polygon', 'star'].includes(tool) || ['polygon', 'star'].includes(SH.kind)) && <span className="ue-optf">Lados<Num v={SH.sides} min={3} max={64} onChange={v => setSH(x => { x.sides = Math.round(v); })} /></span>}
      </>}
      {tool === 'zoom' && <>
        <button onClick={() => zoomTo('out')}><Icon n="minus" />Alejar</button>
        <button onClick={() => zoomTo('in')}><Icon n="plus" />Acercar</button>
        <button onClick={() => zoomTo('fit')}><Icon n="fit" />Encajar</button>
        <button onClick={() => zoomTo(1)}>100%</button>
      </>}
      {tool === 'move' && mode !== '3d' && !showText && !showShape && selLayers.length > 0 && <AlignBar ids={selLayers.map(l => l.id)} doc={doc} clip={clip} t={t} edit={edit} mode={mode} />}
      <span className="st-grow" />
      {mode !== '3d' && <>
        <button className={'st-icon' + (opts.snap ? ' on' : '')} title="Ajuste magnetico (Alt lo desactiva al arrastrar)" onClick={() => setOpts(o => { o.snap = !o.snap; })}><Icon n="magnet" /></button>
        <button className={'st-icon' + (opts.grid ? ' on' : '')} title="Cuadricula" onClick={() => setOpts(o => { o.grid = !o.grid; })}><Icon n="grid" /></button>
        <button className={'st-icon' + (opts.showButtons ? ' on' : '')} title="Marcar los botones en el lienzo" onClick={() => setOpts(o => { o.showButtons = !o.showButtons; })}><Icon n="target" /></button>
        <span className="ue-zoom" title="Zoom (Ctrl + rueda)">{Math.round(view.z * 100)}%</span>
      </>}
      <span className="ue-badge-mode">{MODES[mode].label}</span>
    </div>
  );
}
