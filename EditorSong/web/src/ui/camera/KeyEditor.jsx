// Editor de animacion de camara por keyframes. Al entrar, los controles de la toma desaparecen y queda solo una
// linea de tiempo de la toma con sus claves:
//   - colocas la VISTA 3D (camara de edicion: orbita, WASD/QE) donde quieras y pulsas "Clave aqui": guarda esa pose
//     en el instante del cursor (si ya hay una clave ahi, la actualiza)
//   - clic en la barra = mover el cursor; clic en un rombo = ir a esa clave; arrastrar un rombo = cambiar su tiempo
//   - "Llevar la vista a la clave" pone la vista 3D en la pose de la clave para retocarla y volver a guardarla
//   - el movil (camara virtual) graba aqui mismo su movimiento como claves densas (ver vcam/vcam.js)
import * as THREE from 'three';
import { changed, emit, fmt, pushUndo, S } from '../../core/state.js';
import { worldPose } from '../../camera/shots.js';
import { editCam, onFrame, view } from '../../viewport/scene.js';
import { moveTo } from '../../viewport/controls.js';
import { seek, setPlaying } from '../playback.js';
import { Field, Note, Select, Slider } from '../components/fields.jsx';
import { VCAM, vcamArm } from '../../vcam/vcam.js';

const D2R = THREE.MathUtils.degToRad, R2D = THREE.MathUtils.radToDeg;
const DR = { i: null };   // clave que se esta arrastrando (fuera del componente: sobrevive a los repintados)

// el cursor de la barra sigue a la reproduccion sin repintar el panel
onFrame(() => {
  const h = document.querySelector('.keyHead[data-t0]'); if (!h) return;
  const t0 = +h.dataset.t0, dur = +h.dataset.dur;
  h.style.left = `${(100 * Math.min(1, Math.max(0, (S.t - t0) / Math.max(1, dur)))).toFixed(3)}%`;
});

// convierte la toma a animacion por claves (muestreando su movimiento actual en 3 puntos)
export function toKeys(s) {
  if (s.preset === 'keys' && s.keys?.length) return;
  const keys = [0, 0.5, 1].map(f => {
    const t = Math.round(s.dur * f), W = worldPose(s, s.t + t);
    return { t, p: [W.p.x, W.p.y, W.p.z], q: [W.q.x, W.q.y, W.q.z, W.q.w], fov: W.fov };
  });
  s.keys = keys; s.preset = 'keys'; s.interp ||= 'smooth';
}

function editPose() {
  const asp = view.clientWidth / Math.max(1, view.clientHeight);
  const h = R2D(2 * Math.atan(Math.tan(D2R(editCam.fov) / 2) * asp));
  return { p: editCam.position.toArray(), q: editCam.quaternion.toArray(), fov: Math.min(150, Math.max(5, h)) };
}

export function KeyEditor({ s, onDone }) {
  const K = s.keys || [], rt = Math.min(s.dur, Math.max(0, Math.round(S.t - s.t)));
  const near = K.findIndex(k => Math.abs(k.t - rt) <= 40);
  const sel = S.keySel != null && K[S.keySel] ? S.keySel : near;
  const xOf = t => `${(100 * t / Math.max(1, s.dur)).toFixed(3)}%`;
  const tOf = (ev, el) => { const r = el.getBoundingClientRect(); return Math.round(Math.min(1, Math.max(0, (ev.clientX - r.left) / r.width)) * s.dur); };

  const putKey = () => {
    pushUndo();
    const k = { t: rt, ...editPose() };
    if (near >= 0) K[near] = k; else { K.push(k); K.sort((a, b) => a.t - b.t); }
    s.keys = K; S.keySel = K.indexOf(k); changed();
  };
  const delKey = () => { if (sel < 0 || K.length <= 1) return; pushUndo(); K.splice(sel, 1); S.keySel = null; changed(); };
  const goKey = i => { S.keySel = i; seek(s.t + K[i].t); emit('inspector'); };
  const viewToKey = () => {
    const k = K[sel]; if (!k) return;
    const q = new THREE.Quaternion(...k.q), p = new THREE.Vector3(...k.p);
    moveTo(p, p.clone().add(new THREE.Vector3(0, 0, -4).applyQuaternion(q)), 0.35);
  };
  const nav = d => { const i = d > 0 ? K.findIndex(k => k.t > rt + 1) : K.map(k => k.t < rt - 1).lastIndexOf(true); if (i >= 0) goKey(i); };

  const down = ev => {
    const bar = ev.currentTarget, i = +(ev.target.dataset.i ?? -1);
    if (i >= 0) { pushUndo(); DR.i = i; goKey(i); } else seek(s.t + tOf(ev, bar));
    bar.setPointerCapture(ev.pointerId);
  };
  const move = ev => {
    if (DR.i == null) { if (ev.buttons) seek(s.t + tOf(ev, ev.currentTarget)); return; }
    K[DR.i].t = tOf(ev, ev.currentTarget); seek(s.t + K[DR.i].t); emit('inspector');
  };
  const up = () => { if (DR.i != null) { const k = K[DR.i]; K.sort((a, b) => a.t - b.t); S.keySel = K.indexOf(k); DR.i = null; changed(); } };

  return <div className="fade keyEd">
    <header className="shotHead" style={{ '--c': '#ffd34d' }}><b className="big">◆</b>
      <div><h3>Animacion de la toma {s.id}</h3><p className="muted small">{K.length} claves · {fmt(S.t).slice(0, -4)} ({(rt / 1000).toFixed(2)} s de {(s.dur / 1000).toFixed(1)} s)</p></div>
      <button className="primary" onClick={onDone}>Listo</button></header>
    <div className="keyBar" onPointerDown={down} onPointerMove={move} onPointerUp={up}>
      {K.map((k, i) => <i key={i} data-i={i} className={'keyDot' + (i === sel ? ' on' : '')} style={{ left: xOf(k.t) }} title={`${(k.t / 1000).toFixed(2)} s`} />)}
      <u className="keyHead" data-t0={s.t} data-dur={s.dur} style={{ left: xOf(rt) }} />
    </div>
    <div className="row keyBtns">
      <button onClick={() => nav(-1)} title="Clave anterior">⏮</button>
      <button onClick={() => setPlaying(!S.playing)}>{S.playing ? '⏸' : '▶'}</button>
      <button onClick={() => nav(1)} title="Clave siguiente">⏭</button>
      <button className="primary" onClick={putKey} title="Guarda la pose de la vista 3D en este instante">{near >= 0 ? '◆ Actualizar clave' : '◆ Clave aqui'}</button>
      <button disabled={sel < 0} onClick={viewToKey} title="Pone la vista 3D en la pose de la clave para retocarla">👁 Llevar la vista a la clave</button>
      <button className="danger" disabled={sel < 0 || K.length <= 1} onClick={delKey}>Borrar clave</button>
    </div>
    <Field label="Interpolacion"><Select value={s.interp || 'smooth'} options={[['smooth', 'Suave (curva)'], ['linear', 'Lineal']]}
      onChange={v => { pushUndo(); s.interp = v; changed(); }} /></Field>
    <Slider label="Frecuencia (muestras por segundo al exportar)" value={s.fps || 15} min={5} max={60} step={1} digits={0}
      onChange={v => { s.fps = v; }} hint="Cuantas poses por segundo recibe el juego: mas = movimiento mas fino (y .edfx mas grande)" />
    <div className="row">
      <button className={VCAM.target === s.id ? 'primary' : ''} disabled={!VCAM.phone} onClick={() => vcamArm(VCAM.target === s.id ? null : s.id)}>
        📱 {VCAM.target === s.id ? 'Movil listo: pulsa GRABAR en el telefono' : 'Grabar esta toma con el movil'}</button>
      <span className="muted small">{VCAM.phone ? 'Movil conectado' : 'Movil sin conectar (pestana Camaras › Camara del movil)'}</span>
    </div>
    <Note>Mueve la <b>vista 3D</b> hasta el encuadre que quieras, pon el cursor en el instante y pulsa <b>Clave aqui</b>. El monitor
      muestra la animacion resultante. Al grabar con el movil, las claves de la toma se reemplazan por las del movimiento grabado.</Note>
  </div>;
}
