// Linea de tiempo de claves compartida: pestanas de clip, transporte, regla, una fila por objeto (resumen de sus
// claves) que se despliega en una fila por propiedad, y carriles de marcas opcionales (p. ej. cortes de camara).
// No sabe nada del proyecto: recibe filas y avisa con callbacks.
//
//   rows    [{id, name, icon, props: [[clave, etiqueta]], tracks: {prop: [claves]}}]
//   lanes   [{id, name, icon, marks: [{t, label}], onAdd(t), onMove(i, t, first), onPick(i), sel}]
//   onMoveKeys(id, props, from, to, first)   first = primer movimiento del arrastre (para el deshacer)
import React, { useState } from 'react';
import { Icon } from '../ui/icons.jsx';
import { Num } from './fields.jsx';
import { fmt } from './keys.js';

const near = (a, b) => Math.abs(a - b) < 1e-3;

export function Timeline({ clips, clip, setClip, dur, setDur, t, setT, playing, setPlaying, rows, lanes = [],
  sel, setSel, selKey, setSelKey, onMoveKeys, baseText }) {
  const [open, setOpen] = useState({});
  const [pps, setPps] = useState(220);
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

  const ticks = [];
  const stepT = dur > 8 ? 1 : dur > 4 ? 0.5 : 0.1;
  for (let s = 0; s <= dur + 1e-6; s += stepT) ticks.push(Math.round(s * 10) / 10);
  const isBase = clip === 'base';
  return (
    <section className="st-tl">
      <div className="st-tlbar">
        <div className="st-tabs">{clips.map(([k, v, d]) => <button key={k} className={clip === k ? 'on' : ''} onClick={() => setClip(k)} title={d}>{v}</button>)}</div>
        {!isBase && <>
          <button className="st-icon" onClick={() => setT(0)} title="Al principio"><Icon n="start" /></button>
          <button className="st-icon primary" onClick={() => { if (t >= dur) setT(0); setPlaying(v => !v); }} title="Reproducir (Espacio)"><Icon n={playing ? 'pause' : 'play'} /></button>
          <span className="st-time">{t.toFixed(2)} /</span>
          <label className="st-dur" title="Duracion del clip en segundos"><Num v={dur} step={0.1} min={0.1} onChange={setDur} />s</label>
          <span className="st-grow" />
          <label className="st-zoom" title="Zoom"><Icon n="zoomin" s={14} /><input type="range" min="60" max="600" value={pps} onChange={e => setPps(+e.target.value)} /></label>
        </>}
      </div>
      {isBase
        ? <div className="st-muted st-tlbase">{baseText}</div>
        : <div className="st-tlbody">
          <div className="st-tlnames">
            <div className="st-tlruler-pad" />
            {lanes.map(L => <div key={L.id} className="st-tlname lane"><Icon n={L.icon} s={13} />{L.name}</div>)}
            {rows.map(r => <React.Fragment key={r.id}>
              <div className={'st-tlname' + (r.id === sel ? ' on' : '')} onClick={() => setSel?.(r.id)}>
                <button className="st-icon" onClick={e => { e.stopPropagation(); setOpen(o => ({ ...o, [r.id]: !o[r.id] })); }}><Icon n={open[r.id] ? 'chevdown' : 'chev'} s={13} /></button>
                <Icon n={r.icon} s={13} /><span>{r.name}</span>
              </div>
              {open[r.id] && r.props.map(([p, label]) => <div key={p} className="st-tlname sub">{label}</div>)}
            </React.Fragment>)}
          </div>
          <div className="st-tlscroll">
            <div style={{ width: dur * pps + 40, position: 'relative' }}>
              <div className="st-ruler" onMouseDown={e => scrub(e, e.currentTarget)}>
                {ticks.map(s => { const major = near(s, Math.round(s)); return <span key={s} style={{ left: s * pps }} className={major ? 'major' : ''}>{major ? s + 's' : ''}</span>; })}
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
              {rows.map(r => {
                const tr = r.tracks || {};
                const times = [...new Set(Object.values(tr).flat().map(k => k.t))];
                return <React.Fragment key={r.id}>
                  <div className={'st-tlrow' + (r.id === sel ? ' on' : '')} onMouseDown={e => scrub(e, e.currentTarget)}>
                    <div className="st-tlend" style={{ left: dur * pps }} />
                    {times.map(tt => <span key={tt} className="st-key sum" style={{ left: tt * pps }}
                      onMouseDown={e => dragKey(e, r.id, Object.keys(tr).filter(p => tr[p].some(k => near(k.t, tt))), tt)} />)}
                  </div>
                  {open[r.id] && r.props.map(([p]) => (
                    <div key={p} className="st-tlrow sub" onMouseDown={e => scrub(e, e.currentTarget)}>
                      {(tr[p] || []).map(k => <span key={k.t} title={`${fmt(k.t)} s  ${k.ease || 'linear'}`}
                        className={'st-key' + (selKey && selKey.lid === r.id && selKey.prop === p && near(selKey.t, k.t) ? ' on' : '') + (k.ease && k.ease !== 'linear' ? ' eased' : '')}
                        style={{ left: k.t * pps }} onMouseDown={e => dragKey(e, r.id, [p], k.t)} />)}
                    </div>
                  ))}
                </React.Fragment>;
              })}
              <div className="st-cursor" style={{ left: t * pps }} />
            </div>
          </div>
        </div>}
    </section>
  );
}
