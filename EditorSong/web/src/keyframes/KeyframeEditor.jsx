// Interfaz del editor de animacion de camara (ver kf.js). Dos piezas:
//   KfDock  (abajo, a todo lo ancho): barra de herramientas, regla, pistas o curvas, cursor de reproduccion
//   KfSide  (derecha): inspector de la clave elegida / de la toma
// Sus controles son propios (no usan los del panel normal) para que nada de aqui toque el deshacer del proyecto.
import { React, useBus, useEffect, useRef, useState } from '../ui/react.jsx';
import { on, S } from '../core/state.js';
import { onFrame } from '../viewport/scene.js';
import { setPlaying } from '../ui/playback.js';
import { confirmBox, toast } from '../ui/overlay.js';
import { EASES, keyEase } from '../camera/shots.js';
import { VCAM, vcamArm } from '../vcam/vcam.js';
import {
  addKey, CHANNELS, chanOf, copyKeys, delKeys, FRAME_MS, goKey, KF, kfClose, kfDirty, kfEdit, kfEmit, kfPush, kfRedo,
  kfSeek, kfSub, kfTime, kfUndo, pasteKeys, poseAt, poseChan, selectAll, setChan, setKfWidth, setPilot, simplifyKeys, snapT, viewPose,
  viewToKey,
} from './kf.js';

const LABEL_W = 132;
const GROUPS = { pos: ['px', 'py', 'pz'], rot: ['pan', 'tilt', 'roll'], fov: ['fov'] };
const ROWS = [['all', 'Camara', null], ['pos', 'Posicion', GROUPS.pos], ['rot', 'Rotacion', GROUPS.rot], ['fov', 'Zoom (FOV)', GROUPS.fov]];
const secs = ms => `${(ms / 1000).toFixed(2)} s`;

function useKf() {
  const [, force] = useState(0);
  useEffect(() => kfSub(() => force(n => n + 1)), []);
  useBus('play', 'kf');
}

// cursor y lectura de tiempo: se mueven con la reproduccion sin repintar React
onFrame(() => {
  if (!KF.on) return;
  const rt = kfTime(), W = Math.max(1, KF.v1 - KF.v0);
  for (const h of document.querySelectorAll('.kfHead')) h.style.left = `${LABEL_W + (rt - KF.v0) / W * (h.parentElement.clientWidth - LABEL_W)}px`;
  const tc = document.getElementById('kfTc'); if (tc) tc.textContent = `${secs(rt)} / ${secs(KF.doc.dur)}`;
});

async function askClose() {
  if (!kfDirty() || await confirmBox('Hay cambios en la animacion sin aplicar. ¿Salir y descartarlos?', 'Descartar', 'danger')) kfClose(false);
}
on('kfAskClose', askClose);

// ---------------------------------------------------------------------------------------------------------------
export function KfDock() {
  useKf();
  const box = useRef(null);
  useEffect(() => {
    if (!box.current) return;
    const ro = new ResizeObserver(() => { setKfWidth(Math.max(100, box.current.clientWidth - LABEL_W)); kfEmit(); });
    ro.observe(box.current); return () => ro.disconnect();
  }, [KF.on]);
  if (!KF.on || !KF.doc) return null;
  const d = KF.doc;
  return (<div className="kfDock">
    <Toolbar />
    <div className="kfBody" ref={box} onWheel={wheel}>
      <Ruler />
      {KF.mode === 'tracks' ? <Tracks /> : <Curves />}
      <i className="kfHead" />
    </div>
    <div className="kfHint muted small">
      <b>K</b> clave desde la vista 3D · <b>Mayus+K</b> clave con el valor actual · <b>P</b> pilotar la clave con la vista ·
      <b>Supr</b> borrar · <b>Ctrl+C/V/D</b> copiar/pegar/duplicar · <b>←/→</b> fotograma · <b>Mayus+←/→</b> clave ·
      <b>Ctrl+rueda</b> zoom · <b>Ctrl+Z/Y</b> deshacer (solo de este editor) · <b>Ctrl+Intro</b> aplicar · <b>Esc</b> salir
      {d.keys.length > 400 && <span> · {d.keys.length} claves (grabadas con el movil)</span>}
    </div>
  </div>);
}

function wheel(e) {
  const W = KF.v1 - KF.v0, d = KF.doc.dur;
  const r = e.currentTarget.getBoundingClientRect(), x = Math.max(0, e.clientX - r.left - LABEL_W) / Math.max(1, r.width - LABEL_W);
  if (e.ctrlKey) {
    e.preventDefault();
    const at = KF.v0 + x * W, nw = Math.min(d, Math.max(200, W * (e.deltaY > 0 ? 1.2 : 1 / 1.2)));
    KF.v0 = Math.max(0, Math.min(d - nw, at - x * nw)); KF.v1 = KF.v0 + nw;
  } else {
    const dx = (e.deltaX || e.deltaY) / Math.max(1, r.width) * W;
    KF.v0 = Math.max(0, Math.min(d - W, KF.v0 + dx)); KF.v1 = KF.v0 + W;
  }
  kfEmit();
}

function Toolbar() {
  const d = KF.doc, n = KF.sel.size;
  const ease = n ? (() => { const s = new Set([...KF.sel].map(i => d.keys[i]?.ease || 'auto')); return s.size === 1 ? [...s][0] : ''; })() : '';
  return (<div className="kfBar">
    <b className="kfTitle">◆ Animacion · toma {d.id}</b>
    <div className="kfGroup">
      <button title="Inicio (Inicio)" onClick={() => kfSeek(0)}>⏮</button>
      <button title="Clave anterior (Mayus+←)" onClick={() => goKey(-1)}>◀◆</button>
      <button className="kfPlay" title="Reproducir (Espacio)" onClick={() => setPlaying(!S.playing)}>{S.playing ? '⏸' : '▶'}</button>
      <button title="Clave siguiente (Mayus+→)" onClick={() => goKey(1)}>◆▶</button>
      <button className={KF.loop ? 'on' : ''} title="Repetir la toma en bucle" onClick={() => { KF.loop = !KF.loop; kfEmit(); }}>🔁</button>
      <span id="kfTc" className="kfTc" />
    </div>
    <div className="kfGroup">
      <button className="primary" title="Clave con la pose de la vista 3D (K)" onClick={() => addKey(true)}>◆ Desde la vista</button>
      <button title="Clave con el valor animado en el cursor (Mayus+K)" onClick={() => addKey(false)}>◇ Aqui</button>
      <button disabled={!n || d.keys.length - n < 1} title="Borrar (Supr)" onClick={delKeys}>🗑</button>
      <button disabled={!n} title="Copiar (Ctrl+C)" onClick={copyKeys}>⧉</button>
      <button disabled={!KF.clip} title="Pegar en el cursor (Ctrl+V)" onClick={() => pasteKeys()}>📋</button>
      <select value={ease} disabled={!n} title="Interpolacion de las claves elegidas" onChange={e => kfEdit(() => { for (const i of KF.sel) d.keys[i].ease = e.target.value; })}>
        <option value="" disabled>Interpolacion…</option>
        {EASES.map(([k, l]) => <option key={k} value={k}>{l}</option>)}
      </select>
    </div>
    <div className="kfGroup">
      <button className={KF.mode === 'tracks' ? 'on' : ''} onClick={() => { KF.mode = 'tracks'; kfEmit(); }}>Pistas</button>
      <button className={KF.mode === 'curves' ? 'on' : ''} onClick={() => { KF.mode = 'curves'; kfEmit(); }}>Curvas</button>
      <button className={KF.snap ? 'on' : ''} title="Pegar las claves a los pulsos de la cancion (Alt al arrastrar: libre)" onClick={() => { KF.snap = !KF.snap; kfEmit(); }}>🧲 Pulsos</button>
      <button title="Ver toda la toma" onClick={() => { KF.v0 = 0; KF.v1 = d.dur; kfEmit(); }}>↔</button>
      <button disabled={d.keys.length < 3} title="Quita las claves que sobran (p. ej. de una grabacion con el movil) sin cambiar el movimiento: maximo 1 cm y 0,4 grados de diferencia"
        onClick={() => { const n = simplifyKeys(); toast(n ? `Simplificado: ${n} claves menos (quedan ${KF.doc.keys.length}). Ctrl+Z para deshacer.` : 'No sobra ninguna clave.'); }}>✂ Simplificar</button>
    </div>
    <div className="kfGroup">
      <button disabled={!KF.undo.length} title="Deshacer (Ctrl+Z)" onClick={kfUndo}>↶</button>
      <button disabled={!KF.redo.length} title="Rehacer (Ctrl+Y)" onClick={kfRedo}>↷</button>
    </div>
    <span className="kfSpacer" />
    <button onClick={askClose}>Cancelar</button>
    <button className="primary" onClick={() => kfClose(true)} title="Guarda la animacion en la toma (Ctrl+Intro)">✔ Aplicar</button>
  </div>);
}

// ---- regla: segundos y pulsos; arrastrar = mover el cursor ----
function Ruler() {
  const d = KF.doc, W = KF.v1 - KF.v0, w = Math.max(1, setKfWidthRef());
  const step = [100, 250, 500, 1000, 2000, 5000, 10000].find(s => s / W * w >= 70) || 10000;
  const ticks = [];
  for (let t = Math.ceil(KF.v0 / step) * step; t <= KF.v1; t += step) ticks.push(t);
  const beats = (S.proj?.fretbars || []).map(b => b - d.t).filter(b => b >= KF.v0 && b <= KF.v1);
  const scrub = e => {
    const el = e.currentTarget, r = el.getBoundingClientRect();
    const go = ev => kfSeek(KF.v0 + Math.max(0, Math.min(1, (ev.clientX - r.left) / r.width)) * W);
    go(e); el.setPointerCapture(e.pointerId);
    el.onpointermove = ev => { if (ev.buttons) go(ev); };
  };
  return (<div className="kfRuler"><span className="kfLbl">{secs(d.dur)}</span>
    <div className="kfLane" onPointerDown={scrub}>
      {beats.map((b, i) => <i key={'b' + i} className="kfBeat" style={{ left: xp(b) }} />)}
      {ticks.map(t => <span key={t} className="kfTick" style={{ left: xp(t) }}>{(t / 1000).toFixed(step < 1000 ? 2 : 0)}s</span>)}
    </div></div>);
}
const setKfWidthRef = () => (KF.v1 > KF.v0 ? document.querySelector('.kfBody')?.clientWidth - LABEL_W || 800 : 800);
const xp = t => `${((t - KF.v0) / Math.max(1, KF.v1 - KF.v0)) * 100}%`;

// ---- arrastre de claves / seleccion por recuadro (comun a pistas y curvas) ----
const DR = { mode: null };
function laneT(e, el) { const r = el.getBoundingClientRect(); return KF.v0 + (e.clientX - r.left) / Math.max(1, r.width) * (KF.v1 - KF.v0); }
function keyDown(e, i, lane, chan) {
  e.stopPropagation();
  const d = KF.doc;
  if (e.shiftKey || e.ctrlKey) { KF.sel.has(i) ? KF.sel.delete(i) : KF.sel.add(i); kfEmit(); return; }
  if (!KF.sel.has(i)) KF.sel = new Set([i]);
  kfPush();
  DR.mode = 'move'; DR.lane = lane; DR.x0 = e.clientX; DR.y0 = e.clientY; DR.moved = false; DR.chan = chan;
  DR.start = new Map([...KF.sel].map(j => [d.keys[j], d.keys[j].t])); DR.anchor = d.keys[i];
  DR.v0 = chan ? chanOf(d.keys[i], chan) : 0;
  lane.setPointerCapture(e.pointerId);
  kfSeek(d.keys[i].t); kfEmit();
}
function laneDown(e) {
  if (e.target.dataset.k != null) return;
  const lane = e.currentTarget;
  DR.mode = 'box'; DR.lane = lane; DR.t0 = laneT(e, lane); DR.x0 = e.clientX; DR.add = e.shiftKey || e.ctrlKey;
  lane.setPointerCapture(e.pointerId);
}
function laneMove(e) {
  const d = KF.doc;
  if (DR.mode === 'move') {
    const dx = e.clientX - DR.x0;
    if (!DR.moved && Math.abs(dx) < 3 && Math.abs(e.clientY - DR.y0) < 3) return;
    DR.moved = true;
    const dt = dx / Math.max(1, DR.lane.clientWidth) * (KF.v1 - KF.v0);
    const at = snapT(DR.start.get(DR.anchor) + dt, e.altKey), delta = at - DR.start.get(DR.anchor);
    for (const [k, t0] of DR.start) k.t = Math.min(d.dur, Math.max(0, Math.round(t0 + delta)));
    if (DR.chan && DR.range) {   // curvas: arriba/abajo cambia el valor del canal
      const v = DR.v0 - (e.clientY - DR.y0) / Math.max(1, DR.lane.clientHeight) * DR.range;
      setChan(DR.anchor, DR.chan, v);
    }
    kfSeek(DR.anchor.t); kfEmit();
  } else if (DR.mode === 'box') {
    const r = DR.lane.getBoundingClientRect(), a = Math.min(DR.x0, e.clientX) - r.left, b = Math.max(DR.x0, e.clientX) - r.left;
    let m = DR.lane.querySelector('.kfBox'); if (!m) { m = document.createElement('div'); m.className = 'kfBox'; DR.lane.appendChild(m); }
    m.style.left = a + 'px'; m.style.width = (b - a) + 'px';
  }
}
function laneUp(e) {
  const d = KF.doc;
  if (DR.mode === 'move') {
    if (!DR.moved) KF.undo.pop();   // solo era un clic: no deja paso de deshacer vacio
    else { const keep = [...KF.sel].map(i => d.keys[i]); kfEdit(() => { KF.sel = new Set(keep.map(k => d.keys.indexOf(k))); }, false); }
  } else if (DR.mode === 'box') {
    DR.lane.querySelector('.kfBox')?.remove();
    const t1 = laneT(e, DR.lane), a = Math.min(DR.t0, t1), b = Math.max(DR.t0, t1);
    if (Math.abs(e.clientX - DR.x0) < 4) { if (!DR.add) KF.sel = new Set(); kfSeek(t1); }
    else {
      const hit = d.keys.map((k, i) => (k.t >= a && k.t <= b ? i : -1)).filter(i => i >= 0);
      KF.sel = DR.add ? new Set([...KF.sel, ...hit]) : new Set(hit);
    }
    kfEmit();
  }
  DR.mode = null; DR.range = null;
}
const laneProps = { onPointerDown: laneDown, onPointerMove: laneMove, onPointerUp: laneUp, onDoubleClick: e => { kfSeek(laneT(e, e.currentTarget)); addKey(false); } };

// ---- pistas: una fila por grupo, con su curva en miniatura detras de las claves ----
function sample(chans, n = 160) {
  const d = KF.doc, out = chans.map(() => []);
  for (let i = 0; i <= n; i++) {
    const t = KF.v0 + (KF.v1 - KF.v0) * i / n, W = poseAt(t);
    chans.forEach((c, j) => out[j].push([i / n, poseChan(W, c)]));
  }
  return out;
}
function Spark({ chans }) {
  const S2 = sample(chans, 120), all = S2.flat().map(p => p[1]);
  const lo = Math.min(...all), hi = Math.max(...all), rg = hi - lo || 1;
  return (<svg className="kfSpark" viewBox="0 0 100 100" preserveAspectRatio="none">
    {S2.map((pts, j) => <polyline key={j} fill="none" stroke={CHANNELS[chans[j]].color} strokeWidth="1.2" vectorEffect="non-scaling-stroke" opacity=".55"
      points={pts.map(([x, v]) => `${x * 100},${90 - (v - lo) / rg * 80}`).join(' ')} />)}
  </svg>);
}
const dense = () => { const d = KF.doc, n = d.keys.filter(k => k.t >= KF.v0 && k.t <= KF.v1).length; return n * 7 > setKfWidthRef(); };
function Tracks() {
  const d = KF.doc;
  return (<div className={'kfRows' + (dense() ? ' dense' : '')}>{ROWS.map(([id, label, chans]) => (
    <div key={id} className={'kfRow ' + id}>
      <span className="kfLbl">{label}{chans && <small>{chans.map(c => CHANNELS[c].label.replace('Pos ', '')).join(' · ')}</small>}</span>
      <div className="kfLane" {...laneProps}>
        {chans && <Spark chans={chans} />}
        {(id === 'all' || !dense()) && d.keys.map((k, i) => (k.t >= KF.v0 - 50 && k.t <= KF.v1 + 50) && (
          <i key={i} data-k={i} className={'kfKey e-' + keyEase(d, k) + (KF.sel.has(i) ? ' on' : '')} style={{ left: xp(k.t) }}
            title={`${secs(k.t)} · ${EASES.find(e => e[0] === (k.ease || 'auto'))?.[1]}`}
            onPointerDown={e => keyDown(e, i, e.currentTarget.parentElement, null)} />))}
      </div>
    </div>))}</div>);
}

// ---- curvas: valores de los canales del grupo elegido; arrastrar un punto cambia tiempo y valor ----
function Curves() {
  const d = KF.doc;
  const grp = Object.entries(GROUPS).find(([, cs]) => cs.includes(KF.chans[0]))?.[0] || 'pos';
  const chans = KF.chans;
  const S2 = sample(chans, 240), vals = S2.flat().map(p => p[1]).concat(d.keys.flatMap(k => chans.map(c => chanOf(k, c))));
  let lo = Math.min(...vals), hi = Math.max(...vals); const pad = (hi - lo || 1) * 0.12; lo -= pad; hi += pad;
  const rg = hi - lo, y = v => `${(1 - (v - lo) / rg) * 100}%`;
  return (<div className={'kfRows curves' + (dense() ? ' dense' : '')}>
    <div className="kfRow curveRow">
      <span className="kfLbl">
        {Object.entries({ pos: 'Posicion', rot: 'Rotacion', fov: 'FOV' }).map(([g, l]) =>
          <button key={g} className={'small' + (grp === g ? ' on' : '')} onClick={() => { KF.chans = GROUPS[g]; kfEmit(); }}>{l}</button>)}
        {chans.map(c => <small key={c} style={{ color: CHANNELS[c].color }}>■ {CHANNELS[c].label}</small>)}
        <small className="muted">{hi.toFixed(2)} … {lo.toFixed(2)} {CHANNELS[chans[0]].unit}</small>
      </span>
      <div className="kfLane" {...laneProps}>
        <svg className="kfGraph" viewBox="0 0 100 100" preserveAspectRatio="none">
          {[0.25, 0.5, 0.75].map(g => <line key={g} x1="0" x2="100" y1={g * 100} y2={g * 100} stroke="#ffffff14" vectorEffect="non-scaling-stroke" />)}
          {S2.map((pts, j) => <polyline key={j} fill="none" stroke={CHANNELS[chans[j]].color} strokeWidth="1.6" vectorEffect="non-scaling-stroke"
            points={pts.map(([x, v]) => `${x * 100},${(1 - (v - lo) / rg) * 100}`).join(' ')} />)}
        </svg>
        {d.keys.map((k, i) => (k.t >= KF.v0 - 50 && k.t <= KF.v1 + 50) && chans.map(c => (
          <i key={i + c} data-k={i} className={'kfPt' + (KF.sel.has(i) ? ' on' : '')} style={{ left: xp(k.t), top: y(chanOf(k, c)), '--c': CHANNELS[c].color }}
            title={`${CHANNELS[c].label}: ${chanOf(k, c).toFixed(2)} ${CHANNELS[c].unit} · ${secs(k.t)}`}
            onPointerDown={e => { keyDown(e, i, e.currentTarget.parentElement, c); DR.range = rg; }} />)))}
      </div>
    </div>
  </div>);
}

// ---------------------------------------------------------------------------------------------------------------
// Inspector (derecha)
function NumIn({ value, step = 0.01, digits = 3, onChange }) {
  const [txt, setTxt] = useState(null);
  return (<input type="number" step={step} value={txt ?? String(+(+value).toFixed(digits))}
    onFocus={() => kfPush()} onChange={e => { setTxt(e.target.value); const v = parseFloat(e.target.value); if (Number.isFinite(v)) onChange(v); }}
    onBlur={() => setTxt(null)}
    onWheel={e => { if (document.activeElement !== e.currentTarget) return; e.preventDefault(); onChange(+value + (e.deltaY < 0 ? step : -step) * (e.shiftKey ? 10 : 1)); }} />);
}
const live = fn => v => { fn(v); kfEdit(() => {}, false); };

export function KfSide() {
  useKf();
  if (!KF.on || !KF.doc) return null;
  const d = KF.doc, idx = [...KF.sel], k = idx.length === 1 ? d.keys[idx[0]] : null;
  return (<div className="kfSide">
    {k ? (<>
      <h3>◆ Clave {idx[0] + 1} de {d.keys.length}</h3>
      <div className="kfGrid">
        <label>Tiempo</label><div className="kfIn"><NumIn value={k.t / 1000} step={1 / 30} onChange={live(v => { k.t = Math.min(d.dur, Math.max(0, Math.round(v * 1000))); })} /><em>s</em></div>
        <label>Hacia la siguiente</label>
        <select value={k.ease || 'auto'} onChange={e => kfEdit(() => { k.ease = e.target.value; })}>{EASES.map(([v, l]) => <option key={v} value={v}>{l}</option>)}</select>
      </div>
      <h4>Posicion</h4>
      <div className="kfVec">{['px', 'py', 'pz'].map(c => <div key={c} className="kfIn" style={{ '--c': CHANNELS[c].color }}>
        <b>{c[1].toUpperCase()}</b><NumIn value={chanOf(k, c)} step={0.05} onChange={live(v => setChan(k, c, v))} /></div>)}</div>
      <h4>Rotacion</h4>
      <div className="kfVec">{['pan', 'tilt', 'roll'].map(c => <div key={c} className="kfIn" title={CHANNELS[c].label} style={{ '--c': CHANNELS[c].color }}>
        <b>{CHANNELS[c].label.slice(0, 4)}</b><NumIn value={chanOf(k, c)} step={0.5} digits={2} onChange={live(v => setChan(k, c, v))} /></div>)}</div>
      <h4>Lente</h4>
      <div className="kfGrid">
        <label>FOV horizontal</label><div className="kfIn"><NumIn value={k.fov} step={0.5} digits={1} onChange={live(v => setChan(k, 'fov', v))} /><em>°</em></div>
        <label>Equivale a</label><span className="muted">{(18 / Math.tan(k.fov * Math.PI / 360)).toFixed(0)} mm (35 mm)</span>
      </div>
      <div className="kfBtns">
        <button className="primary" onClick={() => kfEdit(() => { const P = viewPose(); k.p = P.p; k.q = P.q; k.fov = P.fov; })}
          title="Sustituye la pose de la clave por la de la vista 3D">📷 Tomar la pose de la vista 3D</button>
        <button onClick={() => viewToKey(k)} title="Pone la vista 3D exactamente en la clave">👁 Llevar la vista a la clave</button>
        <button className={KF.pilot ? 'on' : ''} onClick={() => setPilot(!KF.pilot)}
          title="Mientras esta activo, mover la vista 3D (raton, WASD/QE) mueve esta clave en directo (P)">✋ {KF.pilot ? 'Pilotando: mueve la vista 3D' : 'Pilotar la clave con la vista'}</button>
      </div>
    </>) : idx.length > 1 ? (<>
      <h3>{idx.length} claves elegidas</h3>
      <div className="kfGrid">
        <label>Interpolacion</label>
        <select defaultValue="" onChange={e => kfEdit(() => { for (const i of idx) d.keys[i].ease = e.target.value; })}>
          <option value="" disabled>Elegir…</option>{EASES.map(([v, l]) => <option key={v} value={v}>{l}</option>)}</select>
        <label>Igualar</label>
        <div className="kfBtns row">
          <button className="small" onClick={() => kfEdit(() => { const f = d.keys[idx[0]].fov; for (const i of idx) d.keys[i].fov = f; })}>FOV</button>
          <button className="small" onClick={() => kfEdit(() => { const r = chanOf(d.keys[idx[0]], 'roll'); for (const i of idx) setChan(d.keys[i], 'roll', r); })}>Giro</button>
          <button className="small" onClick={() => kfEdit(() => { const y = d.keys[idx[0]].p[1]; for (const i of idx) d.keys[i].p[1] = y; })}>Altura</button>
        </div>
      </div>
      <div className="kfBtns"><button className="danger" disabled={d.keys.length - idx.length < 1} onClick={delKeys}>Borrar {idx.length} claves</button></div>
    </>) : (<>
      <h3>Toma {d.id}</h3>
      <p className="muted small">{d.keys.length} claves · {secs(d.dur)} · empieza en {secs(d.t)} de la cancion</p>
      <div className="kfGrid">
        <label>Interpolacion por defecto</label>
        <select value={d.interp} onChange={e => kfEdit(() => { d.interp = e.target.value; })}>
          <option value="smooth">Curva suave</option><option value="linear">Lineal</option></select>
        <label>Frecuencia al exportar</label>
        <div className="kfIn"><input type="range" min="5" max="60" step="1" value={d.fps} onPointerDown={() => kfPush()}
          onChange={e => { d.fps = +e.target.value; kfEmit(); }} /><em>{d.fps}/s</em></div>
      </div>
      <p className="muted small">La frecuencia es cuantas poses por segundo recibe el juego: mas es mas fino (y el .edfx pesa mas).</p>
    </>)}
    <div className="kfPhone">
      <h4>📱 Camara del movil</h4>
      {VCAM.phone ? (<>
        <p className="small">{VCAM.rec ? `Grabando… ${d.keys.length} claves` : 'Pulsa GRABAR en el telefono: la cancion suena desde el inicio de la toma y tu movimiento sustituye las claves (se puede deshacer).'}</p>
        <button className={VCAM.target === d.id ? 'primary' : ''} onClick={() => vcamArm(VCAM.target === d.id ? null : d.id)}>
          {VCAM.target === d.id ? 'Movil listo para esta toma' : 'Preparar el movil para esta toma'}</button>
      </>) : <p className="muted small">Sin conectar (pestana Camaras › Camara del movil).</p>}
    </div>
  </div>);
}
