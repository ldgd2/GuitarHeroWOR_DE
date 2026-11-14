// Transporte: reproducir, reloj, audio, ajuste a compas, velocidad, zoom y acciones de la linea de tiempo.
import { emit, fmt, S } from '../../core/state.js';
import { HEAD } from '../../timeline/lanes.js';
import { importSka, newClip } from '../actions.js';
import { seek, setPlaying, setSpeed } from '../playback.js';
import { A, setVolume } from '../../audio/player.js';
import { Icon, useBus, useRef } from '../react.jsx';

function AudioState() {
  useBus('audio');
  if (!S.proj) return null;
  if (A.loading) return <span className="audioState"><span className="spin" /> Preparando audio...</span>;
  if (!A.info?.available) return <span className="audioState" title={A.info?.reason || ''}>Sin audio: {A.info?.reason || '-'}</span>;
  const on = A.info.groups.filter(g => !A.muted.has(g.id)).map(g => g.label);
  return <span className="audioState ok" title={'Suenan: ' + on.join(', ')}>♪ {on.length}/{A.info.groups.length} instrumentos</span>;
}

export function Transport() {
  useBus('timeline', 'play', 'song', 'audio');
  const file = useRef();
  const fit = () => { const w = document.getElementById('tlScroll').clientWidth; S.zoom = Math.max(0.004, (w - HEAD - 20) / Math.max(1, S.proj?.length || 1)); S.scroll = 0; emit('timeline'); };
  return <>
    <button className={'play' + (S.playing ? ' on' : '')} title="Reproducir / pausa (Espacio)" disabled={!S.proj} onClick={() => setPlaying(!S.playing)}>
      <Icon n={S.playing ? 'pause' : 'play'} s={18} /></button>
    <button className="ghost" title="Al principio (Inicio)" onClick={() => seek(0)}>⏮</button>
    <span className="clock">{fmt(S.t)}<small>{S.proj ? ' / ' + fmt(S.proj.length).slice(0, -4) : ''}</small></span>
    <label className="vol" title="Volumen de la cancion"><Icon n={A.volume ? 'vol' : 'mute'} />
      <input type="range" min="0" max="1" step="0.01" value={A.volume} onChange={e => setVolume(+e.target.value)} /></label>
    <AudioState />
    <span className="sep"></span>
    <label className="check"><input type="checkbox" checked={S.snap} onChange={e => { S.snap = e.target.checked; emit('timeline'); }} /><span className="box"></span>Compas</label>
    <label className="check"><input type="checkbox" checked={S.follow} onChange={e => { S.follow = e.target.checked; emit('timeline'); }} /><span className="box"></span>Seguir</label>
    <select title="Velocidad" value={String(S.speed)} onChange={e => setSpeed(+e.target.value)}>
      {['0.25', '0.5', '0.75', '1', '1.5', '2'].map(v => <option key={v} value={v}>{v}x</option>)}</select>
    <label className="zoom" title="Zoom (Ctrl+rueda)">Zoom <input type="range" min={Math.log(0.004)} max={Math.log(2)} step="0.01" value={Math.log(S.zoom)}
      onChange={e => { const c = S.t; S.zoom = Math.exp(+e.target.value); S.scroll = Math.max(0, c - 200 / S.zoom); emit('timeline'); }} />
      <button className="ghost small" onClick={fit}>Ajustar</button></label>
    <button className="ghost" disabled={!S.proj} onClick={() => file.current.click()}><Icon n="file" /> Importar .ska</button>
    <button className="ghost" disabled={!S.proj} onClick={newClip}><Icon n="clip" /> Nuevo clip</button>
    <input ref={file} type="file" accept=".ska,.xen,.ska.xen" hidden onChange={e => { const f = e.target.files[0]; e.target.value = ''; importSka(f); }} />
  </>;
}
