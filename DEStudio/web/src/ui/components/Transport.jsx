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
  return <span className="audioState ok" title={'Suenan: ' + on.join(', ')}><Icon n="wave" s={13} />{on.length}/{A.info.groups.length} instrumentos</span>;
}

export function Transport() {
  useBus('timeline', 'play', 'song', 'audio');
  const file = useRef();
  const fit = () => { const w = document.getElementById('tlScroll').clientWidth; S.zoom = Math.max(0.004, (w - HEAD - 20) / Math.max(1, S.proj?.length || 1)); S.scroll = 0; emit('timeline'); };
  const tog = (k, icon, label, title) => <button className={'tbtn' + (S[k] ? ' on' : '')} title={title} aria-pressed={!!S[k]}
    onClick={() => { S[k] = !S[k]; emit('timeline'); }}><Icon n={icon} s={14} />{label}</button>;
  return <>
    <div className="tgroup">
      <button className="ghost icon" title="Al principio (Inicio)" onClick={() => seek(0)}><Icon n="start" s={16} /></button>
      <button className={'play' + (S.playing ? ' on' : '')} title="Reproducir / pausa (Espacio)" disabled={!S.proj} onClick={() => setPlaying(!S.playing)}>
        <Icon n={S.playing ? 'pause' : 'play'} s={16} /></button>
    </div>
    <span className="clock">{fmt(S.t)}<small>{S.proj ? ' / ' + fmt(S.proj.length).slice(0, -4) : ''}</small></span>
    <label className="vol" title="Volumen de la cancion"><Icon n={A.volume ? 'vol' : 'mute'} s={15} />
      <input type="range" min="0" max="1" step="0.01" value={A.volume} onChange={e => setVolume(+e.target.value)} /></label>
    <AudioState />
    <span className="sep"></span>
    <div className="tgroup">
      {tog('snap', 'magnet', 'Compas', 'Ajustar al compas al mover y crear')}
      {tog('follow', 'follow', 'Seguir', 'La linea de tiempo sigue al cursor de reproduccion')}
      <select title="Velocidad de reproduccion" value={String(S.speed)} onChange={e => setSpeed(+e.target.value)}>
        {['0.25', '0.5', '0.75', '1', '1.5', '2'].map(v => <option key={v} value={v}>{v}x</option>)}</select>
    </div>
    <i className="tbDiv" />
    <label className="zoom" title="Zoom (Ctrl+rueda)"><Icon n="zoomin" s={15} /><input type="range" min={Math.log(0.004)} max={Math.log(2)} step="0.01" value={Math.log(S.zoom)}
      onChange={e => { const c = S.t; S.zoom = Math.exp(+e.target.value); S.scroll = Math.max(0, c - 200 / S.zoom); emit('timeline'); }} />
      <button className="ghost icon" title="Ver toda la cancion" onClick={fit}><Icon n="fit" s={15} /></button></label>
    <i className="tbDiv" />
    <button className="ghost" disabled={!S.proj} onClick={() => file.current.click()} title="Importar una animacion .ska"><Icon n="upload" s={15} />Importar .ska</button>
    <button className="ghost" disabled={!S.proj} onClick={newClip}><Icon n="clip" s={15} />Nuevo clip</button>
    <input ref={file} type="file" accept=".ska,.xen,.ska.xen" hidden onChange={e => { const f = e.target.files[0]; e.target.value = ''; importSka(f); }} />
  </>;
}
