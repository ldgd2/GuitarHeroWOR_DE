// Interfaz del editor de animacion por keyframes (ver kf.js). Dos piezas:
//   KfDock  (abajo, a todo lo ancho): barra de herramientas, regla, pistas o curvas, cursor de reproduccion
//   KfSide  (derecha): inspector de la clave elegida / de la toma o del retoque
// Sirve para camaras (tomas) y para retoques de pose de los musicos. Sus controles son propios (no usan los del
// panel normal) para que nada de aqui toque el deshacer del proyecto.
import { Icon, React, useBus, useEffect, useRef, useState } from '../ui/react.jsx';
import { on, S } from '../core/state.js';
import { onFrame } from '../viewport/scene.js';
import { setPlaying } from '../ui/playback.js';
import { confirmBox, toast } from '../ui/overlay.js';
import { keyEase, keyPath } from '../camera/shots.js';
import { ROLE_ES } from '../core/roles.js';
import { VCAM, vcamArm } from '../vcam/vcam.js';
import { DEFAULT_BEZ, EASE_BASIC, easeLabel, easePath, FAMILIES, VARIANTS } from './easing.js';
import { boneGroup, boneName, keyEaseOf, layerAt, MOVABLE, POSE_BONES, POSE_GROUPS } from '../rig/poseLayer.js';
import './gizmo3d.js';
import {
  addKey, CHANNELS, chanAt, chanOf, copyKeys, delKeys, distributeKeys, FRAME_MS, goKey, GROUPS, isPose, KF, kfClose, kfDirty, kfEdit,
  kfEmit, kfPush, kfRedo, kfSeek, kfSub, kfTime, kfUndo, pasteKeys, r4, reverseKeys, scaleKeys, selectAll, selKeys, setChan, setEase,
  setKfWidth, setPilot, simplifyKeys, snapT, viewPose, viewToKey,
} from './kf.js';

const LABEL_W = 132;
const CAM_ROWS = [['all', 'Camara', null], ['pos', 'Posicion', GROUPS.pos], ['rot', 'Rotacion', GROUPS.rot], ['fov', 'Zoom (FOV)', GROUPS.fov]];
const secs = ms => `${(ms / 1000).toFixed(2)} s`;
const easeOf = (d, k) => (isPose() ? keyEaseOf(d, k) : keyEase(d, k));
const easeClass = e => (e === 'hold' || e === 'steps' ? 'hold' : e === 'linear' ? 'linear' : e === 'smooth' ? 'smooth' : 'ease');

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
// Selector de transiciones: basicas en lista y las familias en rejilla (entrada / salida / ambas / salida-entrada)
function EaseThumb({ name, k, size = 26 }) {
  return (<svg className="easeThumb" width={size} height={size} viewBox="0 0 100 100">
    <rect x="10" y="15" width="80" height="70" className="easeBox" />
    <polyline points={easePath(name, k)} className="easeLine" />
  </svg>);
}
function EasePicker({ value, onPick, disabled, k }) {
  const [open, setOpen] = useState(null);
  const btn = useRef(), pop = useRef();
  useEffect(() => {
    if (!open) return;
    const off = e => { if (!pop.current?.contains(e.target) && !btn.current?.contains(e.target)) setOpen(null); };
    addEventListener('pointerdown', off); return () => removeEventListener('pointerdown', off);
  }, [open]);
  const toggle = () => {
    if (open) { setOpen(null); return; }
    const r = btn.current.getBoundingClientRect(), h = 420;
    setOpen({ left: Math.min(innerWidth - 470, r.left), top: r.top - h - 6 > 8 ? r.top - h - 6 : r.bottom + 6 });
  };
  const pick = n => { onPick(n); setOpen(null); };
  return (<>
    <button ref={btn} className="easeBtn" disabled={disabled} onClick={toggle} title="Transicion hacia la clave siguiente">
      {value ? <EaseThumb name={value} k={k} size={18} /> : <Icon n="curve" s={14} />}<span>{value ? easeLabel(value) : 'Transicion'}</span><Icon n="chevdown" s={12} />
    </button>
    {open && <div ref={pop} className="easePop" style={{ left: open.left, top: open.top }}>
      <div className="easeBasic">{EASE_BASIC.filter(e => e[0] !== 'auto' || !isPose()).map(([n, l]) => (
        <button key={n} className={value === n ? 'on' : ''} onClick={() => pick(n)}><EaseThumb name={n} k={n === 'bezier' ? (k?.bez ? k : { bez: DEFAULT_BEZ }) : k} />{l}</button>))}</div>
      <table className="easeGrid"><thead><tr><th />{VARIANTS.map(([v, l]) => <th key={v}>{l}</th>)}</tr></thead>
        <tbody>{FAMILIES.map(([f, fl]) => (<tr key={f}><th>{fl}</th>{VARIANTS.map(([v]) => (
          <td key={v}><button className={value === f + v ? 'on' : ''} title={easeLabel(f + v)} onClick={() => pick(f + v)}><EaseThumb name={f + v} size={34} /></button></td>))}</tr>))}</tbody>
      </table>
    </div>}
  </>);
}

// Curva Bezier de tiempo con dos tiradores (como cubic-bezier de CSS)
const BEZ_PRESETS = [['Suave', [0.25, 0.1, 0.25, 1]], ['Entrada', [0.42, 0, 1, 1]], ['Salida', [0, 0, 0.58, 1]], ['Entrada y salida', [0.42, 0, 0.58, 1]],
  ['Anticipacion', [0.6, -0.28, 0.735, 0.045]], ['Sobrepaso', [0.175, 0.885, 0.32, 1.275]], ['Golpe', [0.7, 0, 0.84, 0]], ['Latigo', [0.16, 1, 0.3, 1]]];
function BezierEditor({ keys }) {
  const k0 = keys[0], b = k0.bez || DEFAULT_BEZ, svg = useRef();
  const W = 180, H = 180, pad = 30, X = x => pad + x * (W - 2 * pad), Y = y => H - pad - y * (H - 2 * pad);
  const set = nb => { for (const k of keys) k.bez = nb.map(v => +v.toFixed(3)); kfEmit(); };
  const drag = (e, h) => {
    kfPush();
    const el = svg.current; el.setPointerCapture(e.pointerId);
    el.onpointermove = ev => {
      if (!ev.buttons) return;
      const r = el.getBoundingClientRect(), x = Math.min(1, Math.max(0, ((ev.clientX - r.left) * W / r.width - pad) / (W - 2 * pad)));
      const y = Math.min(1.6, Math.max(-0.6, (H - pad - (ev.clientY - r.top) * H / r.height) / (H - 2 * pad)));
      const nb = [...b]; nb[h * 2] = x; nb[h * 2 + 1] = y; set(nb);
    };
    el.onpointerup = () => { el.onpointermove = null; };
  };
  return (<div className="bezEd">
    <svg ref={svg} viewBox={`0 0 ${W} ${H}`} className="bezSvg">
      <rect x={pad} y={pad} width={W - 2 * pad} height={H - 2 * pad} className="easeBox" />
      <line x1={X(0)} y1={Y(0)} x2={X(b[0])} y2={Y(b[1])} className="bezArm" />
      <line x1={X(1)} y1={Y(1)} x2={X(b[2])} y2={Y(b[3])} className="bezArm" />
      <path d={`M${X(0)},${Y(0)} C${X(b[0])},${Y(b[1])} ${X(b[2])},${Y(b[3])} ${X(1)},${Y(1)}`} className="easeLine" />
      <circle cx={X(b[0])} cy={Y(b[1])} r="6" className="bezH" onPointerDown={e => drag(e, 0)} />
      <circle cx={X(b[2])} cy={Y(b[3])} r="6" className="bezH" onPointerDown={e => drag(e, 1)} />
    </svg>
    <div className="bezVals mono small">{b.map(v => v.toFixed(2)).join(', ')}</div>
    <div className="bezPresets">{BEZ_PRESETS.map(([l, v]) => <button key={l} className="small" onClick={() => { kfPush(); set(v); }}>{l}</button>)}</div>
  </div>);
}

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
      {isPose() ? <><b>Clic</b> en una articulacion de la vista 3D para elegirla · <b>R</b> girar · <b>G</b> mover (raiz y pelvis) · </>
        : <><b>K</b> clave desde la vista 3D · <b>Clic</b> en un cono para elegir la clave · <b>G</b> mover · <b>R</b> girar · <b>P</b> pilotar · </>}
      <b>L</b> ejes mundo/propios · <b>I</b> clave automatica · <b>Mayus+K</b> clave aqui · <b>Supr</b> borrar · <b>Ctrl+C/V/D</b> ·
      <b>Alt+Izq/Der</b> mover claves · <b>Mayus+Izq/Der</b> clave anterior/siguiente · <b>Ctrl+rueda</b> zoom · <b>Ctrl+Intro</b> aplicar · <b>Esc</b> salir
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
  const d = KF.doc, n = KF.sel.size, pose = isPose(), ks = selKeys();
  const ease = n ? (() => { const s = new Set(ks.map(k => easeOf(d, k))); return s.size === 1 ? [...s][0] : ''; })() : '';
  return (<div className="kfBar">
    <b className="kfTitle"><Icon n={pose ? 'bone' : 'keyframe'} s={14} />{pose ? 'Retoque de pose' : 'Animacion'}
      <span>{pose ? `${d.name} · ${ROLE_ES[d.role]}` : `Toma ${d.id}`}</span></b>
    <div className="kfGroup">
      <button title="Inicio (Inicio)" onClick={() => kfSeek(0)}><Icon n="start" s={15} /></button>
      <button title="Clave anterior (Mayus+Izq)" onClick={() => goKey(-1)}><Icon n="prevkey" s={15} /></button>
      <button className="kfPlay" title="Reproducir (Espacio)" onClick={() => setPlaying(!S.playing)}><Icon n={S.playing ? 'pause' : 'play'} s={15} /></button>
      <button title="Clave siguiente (Mayus+Der)" onClick={() => goKey(1)}><Icon n="nextkey" s={15} /></button>
      <button className={KF.loop ? 'on' : ''} title="Repetir en bucle" onClick={() => { KF.loop = !KF.loop; kfEmit(); }}><Icon n="loop" s={15} /></button>
      <span id="kfTc" className="kfTc" />
    </div>
    <div className="kfGroup">
      {!pose && <button className="primary" title="Clave con la pose de la vista 3D (K)" onClick={() => addKey(true)}><Icon n="keyframe" s={13} />Desde la vista</button>}
      <button className={pose ? 'primary' : ''} title="Clave con el valor animado en el cursor (Mayus+K)" onClick={() => addKey(false)}><Icon n="keyframe2" s={13} />{pose ? 'Clave aqui' : 'Aqui'}</button>
      <button disabled={!n || (!pose && d.keys.length - n < 1)} title="Borrar (Supr)" onClick={delKeys}><Icon n="trash" s={15} /></button>
      <button disabled={!n} title="Copiar (Ctrl+C)" onClick={copyKeys}><Icon n="copy" s={15} /></button>
      <button disabled={KF.clip?.kind !== KF.kind} title="Pegar en el cursor (Ctrl+V)" onClick={() => pasteKeys()}><Icon n="paste" s={15} /></button>
      <EasePicker value={ease} disabled={!n} k={ks[0]} onPick={v => setEase(v, v === 'bezier' ? { bez: ks[0]?.bez || DEFAULT_BEZ } : {})} />
    </div>
    <div className="kfGroup" title="Gizmo de la vista 3D">
      <button className={KF.tool === 'move' ? 'on' : ''} disabled={pose && !MOVABLE.has(KF.bone)} title="Mover (G)" onClick={() => { KF.tool = 'move'; kfEmit(); }}><Icon n="truck" s={14} />Mover</button>
      <button className={KF.tool === 'rotate' ? 'on' : ''} title="Girar (R)" onClick={() => { KF.tool = 'rotate'; kfEmit(); }}><Icon n="orbit" s={14} />Girar</button>
      <button title="Ejes del gizmo (L)" onClick={() => { KF.space = KF.space === 'world' ? 'local' : 'world'; kfEmit(); }}>{KF.space === 'world' ? 'Mundo' : 'Propios'}</button>
      <button className={KF.autokey ? 'on' : ''} title="Clave automatica (I): al mover el gizmo sin clave en el cursor, se crea una" onClick={() => { KF.autokey = !KF.autokey; kfEmit(); }}><Icon n="record" s={11} />Auto</button>
    </div>
    <div className="kfGroup">
      <button className={KF.mode === 'tracks' ? 'on' : ''} onClick={() => { KF.mode = 'tracks'; kfEmit(); }}>Pistas</button>
      <button className={KF.mode === 'curves' ? 'on' : ''} onClick={() => { KF.mode = 'curves'; kfEmit(); }}>Curvas</button>
      <button className={KF.snap ? 'on' : ''} title="Pegar las claves a los pulsos de la cancion (Alt al arrastrar: libre)" onClick={() => { KF.snap = !KF.snap; kfEmit(); }}><Icon n="magnet" s={14} />Pulsos</button>
      <button title="Ver todo" onClick={() => { KF.v0 = 0; KF.v1 = d.dur; kfEmit(); }}><Icon n="fit" s={15} /></button>
      {!pose && <button disabled={d.keys.length < 3} title="Quita las claves que sobran (p. ej. de una grabacion con el movil) sin cambiar el movimiento: maximo 1 cm y 0,4 grados de diferencia"
        onClick={() => { const r = simplifyKeys(); toast(r ? `Simplificado: ${r} claves menos (quedan ${KF.doc.keys.length}). Ctrl+Z para deshacer.` : 'No sobra ninguna clave.'); }}><Icon n="curve" s={14} />Simplificar</button>}
    </div>
    <div className="kfGroup">
      <button disabled={!KF.undo.length} title="Deshacer (Ctrl+Z)" onClick={kfUndo}><Icon n="undo" s={15} /></button>
      <button disabled={!KF.redo.length} title="Rehacer (Ctrl+Y)" onClick={kfRedo}><Icon n="redo" s={15} /></button>
    </div>
    <span className="kfSpacer" />
    <button onClick={askClose}>Cancelar</button>
    <button className="primary" onClick={() => kfClose(true)} title="Guarda la animacion (Ctrl+Intro). Guardar y Compilar tambien la aplican."><Icon n="check" s={14} />Aplicar</button>
  </div>);
}

// ---- regla: segundos y pulsos; arrastrar = mover el cursor ----
function Ruler() {
  const d = KF.doc, W = KF.v1 - KF.v0, w = Math.max(1, laneWidth());
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
const laneWidth = () => (KF.v1 > KF.v0 ? document.querySelector('.kfBody')?.clientWidth - LABEL_W || 800 : 800);
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
      const rowF = DR.lane.dataset.row, hit = d.keys.map((k, i) => (k.t >= a && k.t <= b && (!rowF || keyInRow(k, rowF)) ? i : -1)).filter(i => i >= 0);
      KF.sel = DR.add ? new Set([...KF.sel, ...hit]) : new Set(hit);
    }
    kfEmit();
  }
  DR.mode = null; DR.range = null;
}
const laneProps = { onPointerDown: laneDown, onPointerMove: laneMove, onPointerUp: laneUp, onDoubleClick: e => { kfSeek(laneT(e, e.currentTarget)); addKey(false); } };

// ---- pistas: una fila por grupo, con su curva en miniatura detras de las claves ----
function sample(chans, n = 160) {
  const out = chans.map(() => []);
  for (let i = 0; i <= n; i++) {
    const t = KF.v0 + (KF.v1 - KF.v0) * i / n;
    chans.forEach((c, j) => out[j].push([i / n, chanAt(t, c)]));
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
const dense = () => { const d = KF.doc, n = d.keys.filter(k => k.t >= KF.v0 && k.t <= KF.v1).length; return n * 7 > laneWidth(); };
// una clave de pose sale en la fila de un grupo si toca algun hueso de ese grupo
function keyInRow(k, row) {
  if (!isPose() || row === 'all') return true;
  return [...Object.keys(k.b || {}), ...Object.keys(k.bp || {})].some(i => boneGroup(+i) === row);
}
function rowsOf() {
  if (!isPose()) return CAM_ROWS;
  return [['all', 'Pose', null], ...POSE_GROUPS.map(([g, l]) => [g, l, KF.bone != null && boneGroup(KF.bone) === g ? GROUPS.brot : null])];
}
function Tracks() {
  const d = KF.doc;
  return (<div className={'kfRows' + (dense() ? ' dense' : '') + (isPose() ? ' pose' : '')}>{rowsOf().map(([id, label, chans]) => (
    <div key={id} className={'kfRow ' + id}>
      <span className="kfLbl">{label}{chans && <small>{isPose() ? boneName(KF.bone) : chans.map(c => CHANNELS[c].label.replace('Pos ', '')).join(' · ')}</small>}</span>
      <div className="kfLane" data-row={isPose() ? id : undefined} {...laneProps}>
        {chans && <Spark chans={chans} />}
        {(id === 'all' || !dense()) && d.keys.map((k, i) => (k.t >= KF.v0 - 50 && k.t <= KF.v1 + 50) && keyInRow(k, id) && (
          <i key={i} data-k={i} className={'kfKey e-' + easeClass(easeOf(d, k)) + (KF.sel.has(i) ? ' on' : '')} style={{ left: xp(k.t) }}
            title={`${secs(k.t)} · ${easeLabel(easeOf(d, k))}`}
            onPointerDown={e => keyDown(e, i, e.currentTarget.parentElement, null)} />))}
      </div>
    </div>))}</div>);
}

// ---- curvas: valores de los canales del grupo elegido; arrastrar un punto cambia tiempo y valor ----
function Curves() {
  const d = KF.doc, pose = isPose();
  if (pose && KF.bone == null) return <div className="kfRows curves"><p className="kfEmpty muted">Elige una articulacion en la vista 3D (clic sobre los puntos del musico) para ver sus curvas.</p></div>;
  const tabs = pose ? { brot: 'Giro', ...(MOVABLE.has(KF.bone) ? { bpos: 'Desplazamiento' } : {}) } : { pos: 'Posicion', rot: 'Rotacion', fov: 'FOV' };
  const grp = Object.keys(tabs).find(g => GROUPS[g].includes(KF.chans[0])) || Object.keys(tabs)[0];
  const chans = GROUPS[grp];
  const S2 = sample(chans, 240), vals = S2.flat().map(p => p[1]).concat(d.keys.flatMap(k => chans.map(c => chanOf(k, c))));
  let lo = Math.min(...vals), hi = Math.max(...vals); const pad = (hi - lo || 1) * 0.12; lo -= pad; hi += pad;
  const rg = hi - lo, y = v => `${(1 - (v - lo) / rg) * 100}%`;
  const zero = lo < 0 && hi > 0 ? (1 - (0 - lo) / rg) * 100 : null;
  return (<div className={'kfRows curves' + (dense() ? ' dense' : '')}>
    <div className="kfRow curveRow">
      <span className="kfLbl">
        {pose && <small className="curveBone">{boneName(KF.bone)}</small>}
        {Object.entries(tabs).map(([g, l]) =>
          <button key={g} className={'small' + (grp === g ? ' on' : '')} onClick={() => { KF.chans = GROUPS[g]; kfEmit(); }}>{l}</button>)}
        {chans.map(c => <small key={c} className="chanKey" style={{ '--c': CHANNELS[c].color }}><i />{CHANNELS[c].label}</small>)}
        <small className="muted">{hi.toFixed(2)} a {lo.toFixed(2)} {CHANNELS[chans[0]].unit}</small>
      </span>
      <div className="kfLane" {...laneProps}>
        <svg className="kfGraph" viewBox="0 0 100 100" preserveAspectRatio="none">
          {[0.25, 0.5, 0.75].map(g => <line key={g} x1="0" x2="100" y1={g * 100} y2={g * 100} stroke="#ffffff14" vectorEffect="non-scaling-stroke" />)}
          {zero != null && <line x1="0" x2="100" y1={zero} y2={zero} stroke="#ffffff30" strokeDasharray="2 2" vectorEffect="non-scaling-stroke" />}
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
// etiqueta que cambia el valor arrastrando en horizontal (como en Blender o Unity); Mayus = fino
function Scrub({ value, step, onChange, children, title }) {
  const down = e => {
    e.preventDefault(); kfPush();
    const el = e.currentTarget, x0 = e.clientX, v0 = +value;
    el.setPointerCapture(e.pointerId);
    el.onpointermove = ev => { if (ev.buttons) onChange(v0 + (ev.clientX - x0) * step * (ev.shiftKey ? 0.1 : 1)); };
    el.onpointerup = () => { el.onpointermove = null; };
  };
  return <b className="scrub" title={title || 'Arrastra para cambiar (Mayus: fino)'} onPointerDown={down}>{children}</b>;
}
// campo que suma una cantidad a todas las claves elegidas al confirmar
function DeltaIn({ label, unit, step = 0.1, onApply, color, init = 0 }) {
  const [v, setV] = useState(String(init));
  const go = () => { const n = parseFloat(v); if (Number.isFinite(n) && n !== init) onApply(n); setV(String(init)); };
  return (<div className="kfIn" style={{ '--c': color }}><b>{label}</b>
    <input type="number" step={step} value={v} onChange={e => setV(e.target.value)} onBlur={go} onKeyDown={e => { if (e.key === 'Enter') e.target.blur(); }} />
    {unit && <em>{unit}</em>}</div>);
}
const live = fn => v => { fn(v); kfEdit(() => {}, false); };

function ChanRow({ k, c, step, digits = 2 }) {
  const ch = CHANNELS[c], v = chanOf(k, c);
  return (<div className="kfIn" title={ch.label} style={{ '--c': ch.color }}>
    <Scrub value={v} step={step} onChange={live(x => setChan(k, c, x))}>{ch.label.replace('Pos ', '').replace('Giro ', '').replace('Mover ', '').slice(0, 4)}</Scrub>
    <NumIn value={v} step={step} digits={digits} onChange={live(x => setChan(k, c, x))} /><em>{ch.unit}</em></div>);
}

function KeyTiming({ d, k }) {
  const e = easeOf(d, k);
  return (<>
    <div className="kfGrid">
      <label>Tiempo</label><div className="kfIn"><NumIn value={k.t / 1000} step={1 / 30} onChange={live(v => { k.t = Math.min(d.dur, Math.max(0, Math.round(v * 1000))); })} /><em>s</em></div>
      <label>Hacia la siguiente</label>
      <EasePicker value={e} k={k} onPick={v => kfEdit(() => { k.ease = v; if (v === 'bezier') k.bez ||= [...DEFAULT_BEZ]; })} />
      {!isPose() && <><label>Camino</label>
        <div className="kfBtns row">
          <button className={'small' + (keyPath(d, k) === 'curve' ? ' on' : '')} onClick={() => kfEdit(() => { k.path = 'curve'; })}>Curvo</button>
          <button className={'small' + (keyPath(d, k) === 'line' ? ' on' : '')} onClick={() => kfEdit(() => { k.path = 'line'; })}>Recto</button>
        </div></>}
      {e === 'steps' && <><label>Escalones</label><div className="kfIn"><NumIn value={k.steps || 4} step={1} digits={0} onChange={live(v => { k.steps = Math.max(1, Math.min(60, Math.round(v))); })} /></div></>}
    </div>
    {e === 'bezier' && <BezierEditor keys={[k]} />}
  </>);
}

function TimeOps({ n }) {
  return (<>
    <h4>Tiempo</h4>
    <div className="kfGrid">
      <label>Mover</label><DeltaIn label="ms" step={FRAME_MS} onApply={v => kfEdit(d => { for (const k of selKeys()) k.t = Math.min(d.dur, Math.max(0, Math.round(k.t + v))); })} />
      <label>Escalar</label><DeltaIn label="%" init={100} step={5} onApply={v => scaleKeys(v / 100, selKeys()[0].t)} />
    </div>
    <div className="kfBtns row">
      <button className="small" disabled={n < 2} onClick={reverseKeys} title="Invierte el orden en el tiempo">Invertir</button>
      <button className="small" disabled={n < 3} onClick={distributeKeys} title="Mismo espacio entre claves">Repartir</button>
      <button className="small" onClick={() => kfEdit(() => { const t0 = selKeys()[0].t, at = snapT(kfTime(), true); for (const k of selKeys()) k.t = Math.round(at + k.t - t0); })} title="La primera elegida pasa al cursor">Al cursor</button>
    </div>
  </>);
}

function BoneSelect() {
  return (<select value={KF.bone ?? ''} onChange={e => { KF.bone = e.target.value === '' ? null : +e.target.value; kfEmit(); }}>
    <option value="">Elegir articulacion</option>
    {POSE_GROUPS.map(([g, l]) => <optgroup key={g} label={l}>{POSE_BONES.filter(b => b[2] === g).map(([i, n]) => <option key={i} value={i}>{n}</option>)}</optgroup>)}
  </select>);
}

function PoseSide() {
  const d = KF.doc, idx = [...KF.sel], k = idx.length === 1 ? d.keys[idx[0]] : null, i = KF.bone;
  const cur = i != null ? layerAt(d, kfTime()).get(i) : null;
  return (<>
    <h3 className="withIco"><Icon n="bone" s={14} />{ROLE_ES[d.role]}: {d.name}</h3>
    <div className="kfGrid"><label>Articulacion</label><BoneSelect /></div>
    {k ? (<>
      <h4>Clave {idx[0] + 1} de {d.keys.length}</h4>
      <KeyTiming d={d} k={k} />
      {i != null && (<>
        <h4>{boneName(i)} en esta clave</h4>
        <div className="kfVec">{GROUPS.brot.map(c => <ChanRow key={c} k={k} c={c} step={0.5} />)}</div>
        {MOVABLE.has(i) && <div className="kfVec">{GROUPS.bpos.map(c => <ChanRow key={c} k={k} c={c} step={0.5} digits={1} />)}</div>}
        <div className="kfBtns">
          <button onClick={() => kfEdit(() => { delete k.b?.[i]; delete k.bp?.[i]; })} title="Esta clave deja esta articulacion como la anima el juego">Quitar {boneName(i).toLowerCase()} de la clave</button>
          <button disabled={idx[0] === 0} onClick={() => kfEdit(() => { const p = d.keys[idx[0] - 1]; if (p.b?.[i]) (k.b ||= {})[i] = [...p.b[i]]; else delete k.b?.[i]; if (p.bp?.[i]) (k.bp ||= {})[i] = [...p.bp[i]]; })}>Copiar de la clave anterior</button>
        </div>
      </>)}
      {i == null && <p className="muted small">Elige una articulacion (clic en la vista 3D o en la lista) para ver y cambiar sus valores.</p>}
    </>) : idx.length > 1 ? (<>
      <h4>{idx.length} claves elegidas</h4>
      <div className="kfGrid"><label>Transicion</label><EasePicker value="" k={d.keys[idx[0]]} onPick={v => setEase(v, v === 'bezier' ? { bez: [...DEFAULT_BEZ] } : {})} /></div>
      <TimeOps n={idx.length} />
      <div className="kfBtns"><button className="danger" onClick={delKeys}>Borrar {idx.length} claves</button></div>
    </>) : (<>
      {i != null && cur && <p className="muted small">{boneName(i)} en el cursor: retoque de {(2 * Math.acos(Math.min(1, Math.abs(cur.q.w))) * 180 / Math.PI).toFixed(1)} grados.</p>}
      <div className="kfBtns"><button className="primary" onClick={() => addKey(false)}><Icon n="keyframe2" s={13} />Crear clave en el cursor</button></div>
      <h4>Retoque</h4>
      <div className="kfGrid">
        <label>Fundido</label><div className="kfIn"><NumIn value={d.fade} step={50} digits={0} onChange={live(v => { d.fade = Math.max(0, Math.round(v)); })} /><em>ms</em></div>
        <label>Transicion por defecto</label>
        <EasePicker value={d.interp} onPick={v => kfEdit(() => { d.interp = v; })} />
      </div>
      <p className="muted small">{d.keys.length} claves · {secs(d.dur)} · empieza en {secs(d.t)}. Las claves guardan giros EXTRA sobre la animacion
        del musico: donde no hay retoque, se ve la animacion del juego tal cual.</p>
    </>)}
  </>);
}

function CamSide() {
  const d = KF.doc, idx = [...KF.sel], k = idx.length === 1 ? d.keys[idx[0]] : null;
  if (k) return (<>
    <h3 className="withIco"><Icon n="keyframe" s={14} />Clave {idx[0] + 1} de {d.keys.length}</h3>
    <KeyTiming d={d} k={k} />
    <h4>Posicion</h4>
    <div className="kfVec">{GROUPS.pos.map(c => <ChanRow key={c} k={k} c={c} step={0.05} digits={3} />)}</div>
    <h4>Rotacion</h4>
    <div className="kfVec">{GROUPS.rot.map(c => <ChanRow key={c} k={k} c={c} step={0.5} />)}</div>
    <h4>Lente</h4>
    <div className="kfGrid">
      <label>FOV horizontal</label><div className="kfIn"><NumIn value={k.fov} step={0.5} digits={1} onChange={live(v => setChan(k, 'fov', v))} /><em>°</em></div>
      <label>Equivale a</label><span className="muted">{(18 / Math.tan(k.fov * Math.PI / 360)).toFixed(0)} mm (35 mm)</span>
    </div>
    <div className="kfBtns">
      <button className="primary" onClick={() => kfEdit(() => { const P = viewPose(); k.p = r4(P.p); k.q = r4(P.q); k.fov = P.fov; })}
        title="Sustituye la pose de la clave por la de la vista 3D"><Icon n="cam" s={14} />Tomar la pose de la vista 3D</button>
      <button onClick={() => viewToKey(k)} title="Pone la vista 3D exactamente en la clave"><Icon n="eye" s={14} />Llevar la vista a la clave</button>
      <button className={KF.pilot ? 'on' : ''} onClick={() => setPilot(!KF.pilot)}
        title="Mientras esta activo, mover la vista 3D (raton, WASD/QE) mueve esta clave en directo (P)"><Icon n="hand" s={14} />{KF.pilot ? 'Pilotando: mueve la vista 3D' : 'Pilotar la clave con la vista'}</button>
    </div>
  </>);
  if (idx.length > 1) {
    const ks = selKeys();
    const shift = (c, v) => kfEdit(() => { for (const kk of ks) setChan(kk, c, chanOf(kk, c) + v); });
    return (<>
      <h3>{idx.length} claves elegidas</h3>
      <div className="kfGrid"><label>Transicion</label><EasePicker value="" k={ks[0]} onPick={v => setEase(v, v === 'bezier' ? { bez: [...DEFAULT_BEZ] } : {})} />
        <label>Camino</label>
        <div className="kfBtns row">
          <button className="small" onClick={() => kfEdit(() => { for (const kk of ks) kk.path = 'curve'; })}>Curvo</button>
          <button className="small" onClick={() => kfEdit(() => { for (const kk of ks) kk.path = 'line'; })}>Recto</button>
        </div></div>
      <h4>Desplazar todas</h4>
      <div className="kfVec">{GROUPS.pos.map(c => <DeltaIn key={c} label={c[1].toUpperCase()} unit="m" step={0.05} color={CHANNELS[c].color} onApply={v => shift(c, v)} />)}</div>
      <h4>Girar todas</h4>
      <div className="kfVec">{GROUPS.rot.map(c => <DeltaIn key={c} label={CHANNELS[c].label.slice(0, 4)} unit="°" step={1} color={CHANNELS[c].color} onApply={v => shift(c, v)} />)}</div>
      <TimeOps n={idx.length} />
      <h4>Igualar a la primera</h4>
      <div className="kfBtns row">
        <button className="small" onClick={() => kfEdit(() => { const f = ks[0].fov; for (const kk of ks) kk.fov = f; })}>FOV</button>
        <button className="small" onClick={() => kfEdit(() => { const r = chanOf(ks[0], 'roll'); for (const kk of ks) setChan(kk, 'roll', r); })}>Giro</button>
        <button className="small" onClick={() => kfEdit(() => { const y = ks[0].p[1]; for (const kk of ks) kk.p[1] = y; })}>Altura</button>
      </div>
      <div className="kfBtns"><button className="danger" disabled={d.keys.length - idx.length < 1} onClick={delKeys}>Borrar {idx.length} claves</button></div>
    </>);
  }
  return (<>
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
    <p className="muted small">Clic en un cono de la vista 3D para elegir su clave y moverla con el gizmo. Sin clave elegida, el gizmo
      esta en la camara del cursor: con <b>Auto</b> activo, al moverlo se crea una clave.</p>
  </>);
}

export function KfSide() {
  useKf();
  if (!KF.on || !KF.doc) return null;
  const d = KF.doc;
  return (<div className="kfSide">
    {isPose() ? <PoseSide /> : <CamSide />}
    {!isPose() && <div className="kfPhone">
      <h4 className="withIco"><Icon n="phone" s={14} />Camara del movil</h4>
      {VCAM.phone ? (<>
        <p className="small">{VCAM.rec ? `Grabando: ${d.keys.length} claves` : 'Pulsa GRABAR en el telefono: la cancion suena desde el inicio de la toma y tu movimiento sustituye las claves (se puede deshacer).'}</p>
        <button className={VCAM.target === d.id ? 'primary' : ''} onClick={() => vcamArm(VCAM.target === d.id ? null : d.id)}>
          {VCAM.target === d.id ? 'Movil listo para esta toma' : 'Preparar el movil para esta toma'}</button>
      </>) : <p className="muted small">Sin conectar (seccion Camaras, Camara del movil).</p>}
    </div>}
  </div>);
}
