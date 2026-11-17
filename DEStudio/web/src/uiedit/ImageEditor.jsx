// Editor de imagen no destructivo: el original (image.src) se conserva y se aplica una lista de pasos (image.ops)
// en el servidor (editor/ui/imgedit.py). Al aplicar se guarda un PNG nuevo y la capa lo usa: vale tal cual en el juego.
// Herramientas con clic en la imagen: cubeta (rellena desde el punto), cuentagotas (borrar un color), recorte (arrastrar).
import React, { useEffect, useMemo, useRef, useState } from 'react';
import { Icon } from '../ui/icons.jsx';
import { Num } from '../studio/fields.jsx';
import { api } from '../studio/files.js';

const ICON = { crop: 'frame', trim: 'fit', bucket: 'droplet', removebg: 'eyeoff', erase_color: 'eyedrop', adjust: 'contrast',
  hue: 'hue', tint: 'palette', gray: 'contrast', invert: 'contrast', posterize: 'grain', blur: 'blur', sharpen: 'sharpen',
  glow: 'glow', chroma: 'chroma', outline: 'edges', shadow: 'drop', pixelate: 'grid', vignette: 'vignette', noise: 'grain',
  flip: 'flipx', rotate: 'rotate', pad: 'max', round: 'rect', opacity: 'fade' };
const GROUPS = [
  ['Herramientas', ['bucket', 'removebg', 'erase_color', 'crop', 'trim']],
  ['Efectos', ['glow', 'blur', 'chroma', 'outline', 'shadow', 'sharpen', 'pixelate', 'vignette', 'noise']],
  ['Color', ['adjust', 'hue', 'tint', 'gray', 'invert', 'posterize', 'opacity']],
  ['Forma', ['flip', 'rotate', 'pad', 'round']],
];
const CLICK_TOOLS = { bucket: 1, erase_color: 1, crop: 1 };
const hex = c => (typeof c === 'string' ? c : '#ffffff');

export function ImageEditor({ proj, layer, onApply, close }) {
  const img = layer.image;
  const src = img.src || img.file;
  const [defs, setDefs] = useState(null);
  const [ops, setOps] = useState(() => JSON.parse(JSON.stringify(img.ops || [])));
  const [sel, setSel] = useState((img.ops || []).length - 1);
  const [prev, setPrev] = useState(null);
  const [err, setErr] = useState('');
  const [busy, setBusy] = useState(false);
  const [drag, setDrag] = useState(null);
  const [bg, setBg] = useState('check');
  const imgRef = useRef(null);
  useEffect(() => { api('/api/ui/imgedit/ops').then(r => setDefs(Object.fromEntries(r.ops.map(o => [o.op, o])))).catch(e => setErr(e.message)); }, []);

  const cur = ops[sel];
  // lo que se ve: hasta el paso elegido (el recorte se ve sin el, para poder dibujarlo encima)
  const shownOps = useMemo(() => ops.slice(0, sel + 1 - (cur?.op === 'crop' ? 1 : 0)), [ops, sel, cur?.op]);
  const key = JSON.stringify(shownOps);
  useEffect(() => {
    let dead = false;
    const t = setTimeout(async () => {
      try {
        const r = await api('/api/ui/imgedit', { pid: proj.id, src, ops: shownOps, preview: true });
        if (!dead) { setPrev(r); setErr(''); }
      } catch (e) { if (!dead) setErr(e.message); }
    }, 180);
    return () => { dead = true; clearTimeout(t); };
  }, [key]);

  const add = op => {
    const d = defs[op];
    const o = { op, ...Object.fromEntries(d.params.map(p => [p.name, p.default])) };
    if (op === 'crop' && prev) { o.w = prev.w; o.h = prev.h; }
    const at = sel + 1;
    setOps(v => [...v.slice(0, at), o, ...v.slice(at)]);
    setSel(at);
  };
  const setP = (k, v) => setOps(list => list.map((o, i) => (i === sel ? { ...o, [k]: v } : o)));
  const move = d => {
    const j = sel + d;
    if (j < 0 || j >= ops.length) return;
    setOps(v => { const n = [...v]; [n[sel], n[j]] = [n[j], n[sel]]; return n; });
    setSel(j);
  };
  const del = i => { setOps(v => v.filter((_, k) => k !== i)); setSel(s => Math.min(s, ops.length - 2)); };

  // coordenadas de la imagen bajo el raton
  const at = e => {
    const b = imgRef.current.getBoundingClientRect();
    return [Math.round((e.clientX - b.left) / b.width * prev.w), Math.round((e.clientY - b.top) / b.height * prev.h)];
  };
  const pickColorAt = ([x, y]) => {
    const c = document.createElement('canvas'), im = imgRef.current;
    c.width = im.naturalWidth; c.height = im.naturalHeight;
    const g = c.getContext('2d'); g.drawImage(im, 0, 0);
    const k = prev.k || 1, p = g.getImageData(Math.min(c.width - 1, x * k), Math.min(c.height - 1, y * k), 1, 1).data;
    return '#' + [p[0], p[1], p[2]].map(v => v.toString(16).padStart(2, '0')).join('');
  };
  const down = e => {
    if (!cur || !CLICK_TOOLS[cur.op] || !prev) return;
    e.preventDefault();
    const p = at(e);
    if (cur.op === 'bucket') { setOps(l => l.map((o, i) => (i === sel ? { ...o, x: p[0], y: p[1] } : o))); }
    else if (cur.op === 'erase_color') setP('color', pickColorAt(p));
    else if (cur.op === 'crop') setDrag({ a: p, b: p });
  };
  const moveM = e => { if (drag) setDrag(d => ({ ...d, b: at(e) })); };
  const up = () => {
    if (!drag) return;
    const x = Math.max(0, Math.min(drag.a[0], drag.b[0])), y = Math.max(0, Math.min(drag.a[1], drag.b[1]));
    const w = Math.abs(drag.a[0] - drag.b[0]), h = Math.abs(drag.a[1] - drag.b[1]);
    if (w > 2 && h > 2) setOps(l => l.map((o, i) => (i === sel ? { ...o, x, y, w, h } : o)));
    setDrag(null);
  };

  const apply = async () => {
    setBusy(true);
    try {
      if (!ops.length) {
        const r = await api('/api/ui/imgedit', { pid: proj.id, src, ops: [], preview: true });
        onApply({ ...img, custom: true, file: src, name: src, svg: /\.svg$/i.test(src), src: undefined, ops: undefined, w: r.w, h: r.h });
      } else {
        const r = await api('/api/ui/imgedit', { pid: proj.id, src, ops });
        onApply({ ...img, custom: true, file: r.file, name: r.file, svg: false, src, ops, w: r.w, h: r.h });
      }
      close();
    } catch (e) { setErr(e.message); } finally { setBusy(false); }
  };

  // rectangulo de recorte sobre la vista
  const box = cur?.op === 'crop' && prev ? (drag
    ? { x: Math.min(drag.a[0], drag.b[0]), y: Math.min(drag.a[1], drag.b[1]), w: Math.abs(drag.a[0] - drag.b[0]), h: Math.abs(drag.a[1] - drag.b[1]) }
    : { x: cur.x, y: cur.y, w: cur.w || prev.w, h: cur.h || prev.h }) : null;
  const pct = (v, t) => `${(v / t) * 100}%`;

  return (
    <div className="st-modal" onMouseDown={e => e.target === e.currentTarget && close()}>
      <div className="st-modalbox ue-imged">
        <h3><Icon n="edit" />Editar imagen<span className="st-muted">{src}</span>
          <span className="st-grow" />
          <select value={bg} onChange={e => setBg(e.target.value)} title="Fondo de la vista">
            <option value="check">Cuadros</option><option value="dark">Oscuro</option><option value="light">Claro</option></select>
          <button className="st-icon" onClick={close} title="Cerrar"><Icon n="x" /></button></h3>
        <div className="ue-imgedbody">
          <aside className="ue-imgedtools">
            {!defs && <div className="st-muted">Cargando...</div>}
            {defs && GROUPS.map(([g, list]) => <div key={g}>
              <h4>{g}</h4>
              <div className="ue-imgedgrid">{list.filter(k => defs[k]).map(k =>
                <button key={k} onClick={() => add(k)} title={'Anadir: ' + defs[k].title}><Icon n={ICON[k] || 'fx'} />{defs[k].title}</button>)}</div>
            </div>)}
          </aside>
          <div className={'ue-imgedview bg-' + bg} onMouseMove={moveM} onMouseUp={up} onMouseLeave={up}>
            {prev && <div className="ue-imgedframe" style={{ aspectRatio: `${prev.w} / ${prev.h}` }}>
              <img ref={imgRef} src={'data:image/png;base64,' + prev.png} alt="" draggable={false} onMouseDown={down}
                style={{ cursor: cur && CLICK_TOOLS[cur.op] ? 'crosshair' : 'default' }} />
              {box && <div className="ue-imgedcrop" style={{ left: pct(box.x, prev.w), top: pct(box.y, prev.h), width: pct(box.w, prev.w), height: pct(box.h, prev.h) }} />}
              {cur?.op === 'bucket' && <div className="ue-imgedpin" style={{ left: pct(cur.x, prev.w), top: pct(cur.y, prev.h) }} />}
            </div>}
            {prev && <div className="ue-imgedsize">{prev.w} x {prev.h} px</div>}
            {cur && CLICK_TOOLS[cur.op] && <div className="ue-imgedhint">
              {cur.op === 'bucket' ? 'Haz clic donde quieras rellenar' : cur.op === 'erase_color' ? 'Haz clic en el color que quieres borrar' : 'Arrastra para marcar el recorte'}</div>}
          </div>
          <aside className="ue-imgedsteps">
            <h4>Pasos<span className="st-grow" />
              <button className="st-icon" title="Subir" disabled={sel <= 0} onClick={() => move(-1)}><Icon n="up" /></button>
              <button className="st-icon" title="Bajar" disabled={sel < 0 || sel >= ops.length - 1} onClick={() => move(1)}><Icon n="down" /></button></h4>
            <div className={'ue-imgedstep' + (sel === -1 ? ' on' : '')} onClick={() => setSel(-1)}><Icon n="tex" />Original</div>
            {ops.map((o, i) => <div key={i} className={'ue-imgedstep' + (i === sel ? ' on' : '') + (o.off ? ' off' : '')} onClick={() => setSel(i)}>
              <Icon n={ICON[o.op] || 'fx'} /><span>{defs?.[o.op]?.title || o.op}</span>
              <button className="st-icon" title={o.off ? 'Activar' : 'Desactivar'} onClick={e => { e.stopPropagation(); setOps(l => l.map((x, k) => (k === i ? { ...x, off: !x.off } : x))); }}>
                <Icon n={o.off ? 'eyeoff' : 'eye'} /></button>
              <button className="st-icon" title="Quitar" onClick={e => { e.stopPropagation(); del(i); }}><Icon n="trash" /></button>
            </div>)}
            {!ops.length && <div className="st-muted">Anade herramientas o efectos desde la izquierda. Nada se pierde: el original se guarda.</div>}
            {cur && defs?.[cur.op] && <div className="ue-imgedparams">
              <h4>{defs[cur.op].title}</h4>
              {defs[cur.op].params.map(p => <label key={p.name} className="ue-row"><span>{LABEL[p.name] || p.name}</span><div>
                {p.type === 'num' && <Num v={cur[p.name]} min={p.min} max={p.max} step={p.max - p.min <= 5 ? 0.05 : 1} onChange={v => setP(p.name, v)} />}
                {p.type === 'color' && <input type="color" value={hex(cur[p.name])} onChange={e => setP(p.name, e.target.value)} />}
                {p.type === 'bool' && <input type="checkbox" checked={!!cur[p.name]} onChange={e => setP(p.name, e.target.checked)} />}
                {p.type.startsWith('choice:') && <select value={cur[p.name]} onChange={e => setP(p.name, e.target.value)}>
                  {p.type.slice(7).split('|').map(c => <option key={c} value={c}>{CHOICE[c] || c}</option>)}</select>}
              </div></label>)}
            </div>}
          </aside>
        </div>
        {err && <div className="ue-imgederr"><Icon n="warn" />{err}</div>}
        <footer className="ue-imgedfoot">
          {img.src && <button onClick={() => { setOps([]); setSel(-1); }}><Icon n="undo" />Volver al original</button>}
          <span className="st-grow" />
          <button onClick={close}>Cancelar</button>
          <button className="primary" disabled={busy || !defs} onClick={apply}><Icon n="check" />{busy ? 'Aplicando...' : 'Aplicar'}</button>
        </footer>
      </div>
    </div>
  );
}

const LABEL = { x: 'X', y: 'Y', w: 'Ancho', h: 'Alto', pad: 'Margen', color: 'Color', alpha: 'Opacidad', tolerance: 'Tolerancia',
  contiguous: 'Solo lo conectado', feather: 'Suavizado', mode: 'Metodo', brightness: 'Brillo', contrast: 'Contraste',
  saturation: 'Saturacion', gamma: 'Gamma', shift: 'Giro de tono', amount: 'Cantidad', levels: 'Niveles', radius: 'Radio',
  strength: 'Intensidad', inner: 'Hacia dentro', offset: 'Separacion', angle: 'Angulo', width: 'Grosor', dx: 'Desplazar X',
  dy: 'Desplazar Y', blur: 'Desenfoque', size: 'Tamano', axis: 'Eje' };
const CHOICE = { bordes: 'Desde los bordes', ia: 'Automatico (IA, si esta instalado)', horizontal: 'Horizontal', vertical: 'Vertical' };
