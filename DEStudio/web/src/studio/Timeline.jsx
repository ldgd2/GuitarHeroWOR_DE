// Linea de tiempo de claves compartida: pestanas de clip, transporte, regla, una fila por objeto (resumen de sus
// claves) que se despliega en una fila por propiedad, y carriles de marcas opcionales (p. ej. cortes de camara).
// No sabe nada del proyecto: recibe filas y avisa con callbacks.
//
//   rows    [{id, name, icon, props: [[clave, etiqueta]], tracks: {prop: [claves]}}]
//   lanes   [{id, name, icon, marks: [{t, label}], onAdd(t), onMove(i, t, first), onPick(i), sel}]
//   onMoveKeys(id, props, from, to, first)   first = primer movimiento del arrastre (para el deshacer)
//   onEase(id, prop, t, ease, scope)          scope: 'key' | 'track' (toda la propiedad) | 'row' (todo el objeto)
//   onDeleteKey(id, prop, t)
//
// Altura ajustable (arrastrando el borde de arriba), buscador, solo filas con claves, desplegar todo, zoom con
// Ctrl + rueda, ajustar al ancho, y la curva de la clave elegida (todas las de easing.js) con su dibujo.
import React, { useEffect, useMemo, useRef, useState } from 'react';
import { Icon } from '../ui/icons.jsx';
import { Num } from './fields.jsx';
import { fmt } from './keys.js';
import { EASES, easeValue } from '../keyframes/easing.js';

const near = (a, b) => Math.abs(a - b) < 1e-3;
const store = {
  get: (k, d) => { try { const v = localStorage.getItem(k); return v == null ? d : JSON.parse(v); } catch { return d; } },
  set: (k, v) => { try { localStorage.setItem(k, JSON.stringify(v)); } catch { /* sin almacenamiento */ } },
};
// curvas elegibles por grupos (sin las automaticas ni la bezier, que piden mas datos)
const CURVES = EASES.filter(([k]) => !['auto', 'smooth', 'bezier'].includes(k));
const GROUPS = [...new Set(CURVES.map(e => e[2] || 'Otras'))];
const QUICK = ['linear', 'hold', 'easeInOut', 'easeIn', 'easeOut', 'sineInOut', 'backOut', 'elasticOut', 'bounceOut'];

function CurveIcon({ ease, w = 64, h = 30 }) {
  const pts = [];
  for (let i = 0; i <= 32; i++) {
    const x = i / 32;
    let y;
    try { y = easeValue(ease || 'linear', {}, x); } catch { y = x; }
    pts.push(`${(x * (w - 4) + 2).toFixed(1)},${(h - 4 - y * (h - 10) + 1).toFixed(1)}`);
  }
  return <svg className="st-curve" width={w} height={h} viewBox={`0 0 ${w} ${h}`}><polyline points={pts.join(' ')} /></svg>;
}
const EaseSelect = ({ v, onChange }) => (
  <select value={v || 'linear'} onChange={e => onChange(e.target.value)}>
    {GROUPS.map(g => <optgroup key={g} label={g}>{CURVES.filter(e => (e[2] || 'Otras') === g).map(([k, l]) => <option key={k} value={k}>{l}</option>)}</optgroup>)}
  </select>);

export function Timeline({ clips, clip, setClip, dur, setDur, t, setT, playing, setPlaying, rows, lanes = [],
  sel, setSel, selKey, setSelKey, onMoveKeys, onEase, onDeleteKey, baseText, extra, side = false, rowMenu }) {
  const [open, setOpen] = useState({});
  const [pps, setPps] = useState(() => store.get('st-tl-pps', 220));
  const [height, setHeight] = useState(() => store.get('st-tl-h', 300));
  const [width, setWidth] = useState(() => store.get('st-tl-w', 620));
  useEffect(() => store.set('st-tl-w', width), [width]);
  const [q, setQ] = useState('');
  const [onlyKeys, setOnlyKeys] = useState(() => store.get('st-tl-only', false));
  const [menu, setMenu] = useState(null);   // {x, y, id, prop, t}
  const scrollRef = useRef(null);
  useEffect(() => store.set('st-tl-pps', pps), [pps]);
  useEffect(() => store.set('st-tl-h', height), [height]);
  useEffect(() => store.set('st-tl-only', onlyKeys), [onlyKeys]);
  useEffect(() => {
    if (!menu) return;
    const close = () => setMenu(null);
    window.addEventListener('mousedown', close);
    return () => window.removeEventListener('mousedown', close);
  }, [menu]);
  const clampT = v => Math.max(0, Math.min(dur, Math.round(v * 100) / 100));

  const scrub = (e, el) => {
    if (e.button !== 0) return;
    const r = el.getBoundingClientRect();
    const at = ev => setT(clampT((ev.clientX - r.left) / pps));
    at(e);
    const up = () => { window.removeEventListener('mousemove', at); window.removeEventListener('mouseup', up); };
    window.addEventListener('mousemove', at);
    window.addEventListener('mouseup', up);
  };
  // arrastre horizontal generico: move(nuevoT, primero)
  const dragT = (e, t0, move) => {
    e.stopPropagation();
    if (e.button !== 0) return;
    const x0 = e.clientX;
    let cur = t0, started = false;
    const mm = ev => {
      if (!started && Math.abs(ev.clientX - x0) < 3) return;
      const nt = clampT(t0 + (ev.clientX - x0) / pps);
      if (near(nt, cur)) return;
      move(cur, nt, !started);
      started = true; cur = nt;
    };
    const up = () => { window.removeEventListener('mousemove', mm); window.removeEventListener('mouseup', up); };
    window.addEventListener('mousemove', mm);
    window.addEventListener('mouseup', up);
  };
  const dragKey = (e, id, props, t0) => {
    setSel?.(id);
    if (props.length === 1) setSelKey?.({ lid: id, prop: props[0], t: t0 });
    dragT(e, t0, (from, to, first) => {
      onMoveKeys(id, props, from, to, first);
      if (props.length === 1) setSelKey?.({ lid: id, prop: props[0], t: to });
    });
  };
  // borde de arriba: altura
  const resize = e => {
    e.preventDefault();
    const y0 = e.clientY, h0 = height;
    const mm = ev => setHeight(Math.max(120, Math.min(window.innerHeight * 0.85, h0 + (y0 - ev.clientY))));
    const up = () => { window.removeEventListener('mousemove', mm); window.removeEventListener('mouseup', up); };
    window.addEventListener('mousemove', mm);
    window.addEventListener('mouseup', up);
  };
  // modo lado a lado: el borde izquierdo cambia el ancho
  const resizeW = e => {
    e.preventDefault();
    const x0 = e.clientX, w0 = width;
    const mm = ev => setWidth(Math.max(320, Math.min(window.innerWidth * 0.8, w0 + (x0 - ev.clientX))));
    const up = () => { window.removeEventListener('mousemove', mm); window.removeEventListener('mouseup', up); };
    window.addEventListener('mousemove', mm);
    window.addEventListener('mouseup', up);
  };
  // clic derecho en la fila de un objeto: menu del que lo usa (anadir claves, efectos, fusion...)
  const openRowMenu = (e, id, el) => {
    if (!rowMenu) return;
    e.preventDefault();
    const r = el.getBoundingClientRect();
    const at = clampT((e.clientX - r.left) / pps);
    setSel?.(id);
    setMenu({ x: e.clientX, y: e.clientY, row: id, t: at, items: rowMenu(id, at) });
  };
  const wheel = e => {
    if (!e.ctrlKey) return;
    e.preventDefault();
    setPps(p => Math.max(30, Math.min(1500, p * (e.deltaY < 0 ? 1.15 : 1 / 1.15))));
  };
  useEffect(() => {   // la rueda con Ctrl no debe hacer zoom a la pagina
    const el = scrollRef.current;
    if (!el) return;
    el.addEventListener('wheel', wheel, { passive: false });
    return () => el.removeEventListener('wheel', wheel);
  });
  const fit = () => { const w = scrollRef.current?.clientWidth || 600; setPps(Math.max(30, (w - 40) / Math.max(0.1, dur))); };

  const shown = useMemo(() => {
    const n = q.trim().toLowerCase();
    return rows.filter(r => (!n || r.name.toLowerCase().includes(n)) && (!onlyKeys || Object.values(r.tracks || {}).some(k => k?.length)));
  }, [rows, q, onlyKeys]);
  const allOpen = shown.length > 0 && shown.every(r => open[r.id]);
  const toggleAll = () => setOpen(o => { const n = { ...o }; for (const r of shown) n[r.id] = !allOpen; return n; });

  const ticks = [];
  const stepT = pps < 50 ? 2 : pps < 100 ? 1 : pps < 300 ? 0.5 : pps < 700 ? 0.1 : 0.05;
  for (let s = 0; s <= dur + 1e-6; s += stepT) ticks.push(Math.round(s * 100) / 100);
  const majorEvery = pps < 100 ? 1 : pps < 300 ? 1 : 0.5;
  const isBase = clip === 'base';
  const keyOf = sk => rows.find(r => r.id === sk?.lid)?.tracks?.[sk.prop]?.find(k => near(k.t, sk.t));
  const curKey = keyOf(selKey);
  const curRow = rows.find(r => r.id === selKey?.lid);
  const propLabel = curRow?.props.find(p => p[0] === selKey?.prop)?.[1] || selKey?.prop;

  return (
    <section className={'st-tl' + (side ? ' side' : '')} style={side ? { flexBasis: width, width } : { flexBasis: height, height }}>
      {side ? <div className="st-tlgrip v" onMouseDown={resizeW} title="Arrastra para cambiar el ancho" />
        : <div className="st-tlgrip" onMouseDown={resize} title="Arrastra para cambiar la altura" />}
      <div className="st-tlbar">
        <div className="st-tabs">{clips.map(([k, v, d]) => <button key={k} className={clip === k ? 'on' : ''} onClick={() => setClip(k)} title={d}>{v}</button>)}</div>
        {extra}
        {!isBase && <>
          <button className="st-icon" onClick={() => setT(0)} title="Al principio"><Icon n="start" /></button>
          <button className="st-icon primary" onClick={() => { if (t >= dur) setT(0); setPlaying(v => !v); }} title="Reproducir (Espacio)"><Icon n={playing ? 'pause' : 'play'} /></button>
          <button className="st-icon" onClick={() => setT(dur)} title="Al final"><Icon n="end" /></button>
          <span className="st-time">{t.toFixed(2)} /</span>
          <label className="st-dur" title="Duracion del clip en segundos"><Num v={dur} step={0.1} min={0.1} onChange={setDur} />s</label>
          <span className="st-grow" />
          <div className="st-tlsearch"><Icon n="search" s={13} /><input value={q} onChange={e => setQ(e.target.value)} placeholder="Buscar capa" /></div>
          <button className={'st-icon' + (onlyKeys ? ' on' : '')} onClick={() => setOnlyKeys(v => !v)} title="Solo las capas con claves"><Icon n="keyframe" /></button>
          <button className="st-icon" onClick={toggleAll} title={allOpen ? 'Plegar todo' : 'Desplegar todo'}><Icon n={allOpen ? 'up' : 'down'} /></button>
          <button className="st-icon" onClick={fit} title="Ajustar el clip al ancho"><Icon n="fit" /></button>
          <label className="st-zoom" title="Zoom (o Ctrl + rueda)"><Icon n="zoomin" s={14} />
            <input type="range" min="30" max="1500" value={Math.round(pps)} onChange={e => setPps(+e.target.value)} /></label>
        </>}
      </div>
      {!isBase && selKey && curKey && onEase && <div className="st-tlkey">
        <Icon n="keyframe" s={13} /><b>{curRow?.name}</b><span className="st-muted">{propLabel} en {fmt(selKey.t)} s</span>
        <span className="st-tlsep" />
        <span className="st-muted">Curva desde la anterior</span>
        <EaseSelect v={curKey.ease} onChange={v => onEase(selKey.lid, selKey.prop, selKey.t, v, 'key')} />
        <CurveIcon ease={curKey.ease} />
        <div className="st-tlquick">{QUICK.map(k => <button key={k} className={(curKey.ease || 'linear') === k ? 'on' : ''}
          title={CURVES.find(e => e[0] === k)?.[1] || k} onClick={() => onEase(selKey.lid, selKey.prop, selKey.t, k, 'key')}><CurveIcon ease={k} w={30} h={18} /></button>)}</div>
        <span className="st-grow" />
        <button onClick={() => onEase(selKey.lid, selKey.prop, selKey.t, curKey.ease || 'linear', 'track')} title="La misma curva en todas las claves de esta propiedad">A toda la propiedad</button>
        <button onClick={() => onEase(selKey.lid, selKey.prop, selKey.t, curKey.ease || 'linear', 'row')} title="La misma curva en todas las claves de esta capa">A toda la capa</button>
        {onDeleteKey && <button className="st-icon" title="Borrar la clave (Supr)" onClick={() => { onDeleteKey(selKey.lid, selKey.prop, selKey.t); setSelKey(null); }}><Icon n="trash" /></button>}
      </div>}
      {isBase
        ? <div className="st-muted st-tlbase">{baseText}</div>
        : <div className="st-tlbody">
          <div className="st-tlnames">
            <div className="st-tlruler-pad">{shown.length} de {rows.length}</div>
            {lanes.map(L => <div key={L.id} className="st-tlname lane"><Icon n={L.icon} s={13} />{L.name}</div>)}
            {shown.map(r => {
              const n = Object.values(r.tracks || {}).reduce((a, k) => a + (k?.length || 0), 0);
              return <React.Fragment key={r.id}>
                <div className={'st-tlname' + (r.id === sel ? ' on' : '')} onClick={() => setSel?.(r.id)}
                  onContextMenu={e => { if (!rowMenu) return; e.preventDefault(); setSel?.(r.id); setMenu({ x: e.clientX, y: e.clientY, row: r.id, t, items: rowMenu(r.id, t) }); }}>
                  <button className="st-icon" onClick={e => { e.stopPropagation(); setOpen(o => ({ ...o, [r.id]: !o[r.id] })); }}><Icon n={open[r.id] ? 'chevdown' : 'chev'} s={13} /></button>
                  <Icon n={r.icon} s={13} /><span>{r.name}</span>{n > 0 && <em>{n}</em>}
                </div>
                {open[r.id] && r.props.map(([p, label]) => <div key={p} className={'st-tlname sub' + (r.tracks?.[p]?.length ? ' has' : '')}>{label}</div>)}
              </React.Fragment>;
            })}
          </div>
          <div className="st-tlscroll" ref={scrollRef}>
            <div style={{ width: dur * pps + 40, position: 'relative' }}>
              <div className="st-ruler" onMouseDown={e => scrub(e, e.currentTarget)}>
                {ticks.map(s => { const major = Math.abs(s / majorEvery - Math.round(s / majorEvery)) < 1e-3; return <span key={s} style={{ left: s * pps }} className={major ? 'major' : ''}>{major ? s + 's' : ''}</span>; })}
              </div>
              {lanes.map(L => (
                <div key={L.id} className="st-tlrow lane" onMouseDown={e => scrub(e, e.currentTarget)}
                  onDoubleClick={e => { const r = e.currentTarget.getBoundingClientRect(); L.onAdd(clampT((e.clientX - r.left) / pps)); }}
                  title="Doble clic: anadir">
                  <div className="st-tlend" style={{ left: dur * pps }} />
                  {L.marks.map((m, i) => <span key={i} className={'st-mark' + (L.sel === i ? ' on' : '')} style={{ left: m.t * pps }}
                    onMouseDown={e => { L.onPick(i); dragT(e, m.t, (_f, to, first) => L.onMove(i, to, first)); }}>{m.label}</span>)}
                </div>
              ))}
              {shown.map(r => {
                const tr = r.tracks || {};
                const times = [...new Set(Object.values(tr).flat().map(k => k.t))].sort((a, b) => a - b);
                return <React.Fragment key={r.id}>
                  <div className={'st-tlrow' + (r.id === sel ? ' on' : '')} onMouseDown={e => scrub(e, e.currentTarget)}
                    onContextMenu={e => openRowMenu(e, r.id, e.currentTarget)}>
                    <div className="st-tlend" style={{ left: dur * pps }} />
                    {times.length > 1 && <div className="st-tlspan" style={{ left: times[0] * pps, width: (times[times.length - 1] - times[0]) * pps }} />}
                    {times.map(tt => <span key={tt} className="st-key sum" style={{ left: tt * pps }}
                      onMouseDown={e => dragKey(e, r.id, Object.keys(tr).filter(p => tr[p].some(k => near(k.t, tt))), tt)} />)}
                  </div>
                  {open[r.id] && r.props.map(([p]) => {
                    const ks = tr[p] || [];
                    return <div key={p} className="st-tlrow sub" onMouseDown={e => scrub(e, e.currentTarget)} onContextMenu={e => openRowMenu(e, r.id, e.currentTarget)}>
                      {ks.slice(1).map((k, i) => <div key={'s' + k.t} className={'st-tlseg' + (k.ease && k.ease !== 'linear' ? ' eased' : '') + (k.ease === 'hold' ? ' hold' : '')}
                        style={{ left: ks[i].t * pps, width: (k.t - ks[i].t) * pps }} title={CURVES.find(e => e[0] === (k.ease || 'linear'))?.[1] || k.ease} />)}
                      {ks.map(k => <span key={k.t} title={`${fmt(k.t)} s  ${CURVES.find(e => e[0] === (k.ease || 'linear'))?.[1] || k.ease}  (clic derecho: curva)`}
                        className={'st-key' + (selKey && selKey.lid === r.id && selKey.prop === p && near(selKey.t, k.t) ? ' on' : '') + (k.ease && k.ease !== 'linear' ? ' eased' : '')}
                        style={{ left: k.t * pps }} onMouseDown={e => dragKey(e, r.id, [p], k.t)}
                        onContextMenu={e => { e.preventDefault(); e.stopPropagation(); setSelKey?.({ lid: r.id, prop: p, t: k.t }); if (onEase) setMenu({ x: e.clientX, y: e.clientY, id: r.id, prop: p, t: k.t }); }} />)}
                    </div>;
                  })}
                </React.Fragment>;
              })}
              <div className="st-cursor" style={{ left: t * pps }} />
            </div>
          </div>
        </div>}
      {menu?.items && <div className="st-tlmenu rows" style={{ left: Math.min(menu.x, window.innerWidth - 260), top: Math.max(8, Math.min(menu.y, window.innerHeight - 460)) }}
        onMouseDown={e => e.stopPropagation()}>
        <div className="st-muted">En {fmt(menu.t)} s</div>
        {menu.items.map((it, i) => it.sep ? <div key={i} className="st-tlmenusep">{it.sep === true ? null : it.sep}</div>
          : <button key={i} className={it.on ? 'on' : ''} onClick={() => { it.act(); setMenu(null); }}>{it.icon && <Icon n={it.icon} s={13} />}{it.label}</button>)}
      </div>}
      {menu && !menu.items && <div className="st-tlmenu" style={{ left: menu.x, top: Math.max(8, menu.y - 300) }} onMouseDown={e => e.stopPropagation()}>
        <div className="st-muted">Curva de la clave</div>
        {QUICK.map(k => <button key={k} onClick={() => { onEase(menu.id, menu.prop, menu.t, k, 'key'); setMenu(null); }}>
          <CurveIcon ease={k} w={34} h={18} />{CURVES.find(e => e[0] === k)?.[1] || k}</button>)}
        {onDeleteKey && <button onClick={() => { onDeleteKey(menu.id, menu.prop, menu.t); setSelKey?.(null); setMenu(null); }}><Icon n="trash" s={13} />Borrar la clave</button>}
      </div>}
    </section>
  );
}
