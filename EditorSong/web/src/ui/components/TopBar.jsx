// Barra superior: buscador de canciones, escenario y acciones.
import { redo, S, undo } from '../../core/state.js';
import { backups, build, openSong, reload, save, setVenue } from '../actions.js';
import { toast } from '../overlay.js';
import { Icon, useBus, useRef, useState  } from '../react.jsx';

function SongPicker() {
  const [q, setQ] = useState(''), [open, setOpen] = useState(false), [hi, setHi] = useState(0);
  const ref = useRef();
  const words = q.toLowerCase().split(/\s+/).filter(Boolean);
  const list = S.songs.filter(s => words.every(w => (s.name + ' ' + s.mod).toLowerCase().includes(w))).slice(0, 80);
  const choose = s => { setOpen(false); setQ(''); ref.current?.blur(); openSong(s.id).catch(e => toast(e.message, 'err')); };
  const key = e => {
    if (e.key === 'ArrowDown') { e.preventDefault(); setHi(Math.min(hi + 1, list.length - 1)); }
    else if (e.key === 'ArrowUp') { e.preventDefault(); setHi(Math.max(hi - 1, 0)); }
    else if (e.key === 'Enter' && list[hi]) choose(list[hi]);
    else if (e.key === 'Escape') { setOpen(false); ref.current?.blur(); }
  };
  return (<div className="songpick">
    <span className="sicon"><Icon n="search" s={14} /></span>
    <input ref={ref} value={q} placeholder={`Buscar entre ${S.songs.length} canciones...`} onFocus={() => setOpen(true)}
      onBlur={() => setTimeout(() => setOpen(false), 150)} onChange={e => { setQ(e.target.value); setHi(0); setOpen(true); }} onKeyDown={key} />
    <div className={'dropdown' + (open ? ' open' : '')}>
      {list.length ? list.map((s, i) => (<div key={s.id} className={i === hi ? 'hi' : ''} onMouseDown={() => choose(s)} onMouseEnter={() => setHi(i)}>
        <span>{s.name}</span><span className={'badge ' + (s.editable ? 'ok' : '')}>{s.editable ? s.mod : 'juego base'}</span></div>))
        : (<p className="muted small">Sin resultados</p>)}
    </div></div>);
}

const Btn = ({ icon, label, onClick, disabled, cls = '', title }) => (<button className={cls} disabled={disabled} title={title || label} onClick={onClick}>
  <Icon n={icon} /><span className="lbl">{label}</span></button>);

export function TopBar() {
  useBus('song', 'changed', 'songs');
  const P = S.proj;
  return (<><strong className="brand"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"
      strokeLinecap="round" aria-hidden="true"><path d="M3.5 21 9.5 3M20.5 21 14.5 3M6.2 13h11.6M8 7.5h8" /></svg>EditorSong</strong>
    <SongPicker />
    <div className="songname">{P ? (<><b>{P.song.name}{S.dirty && (<i className="dot" title="Cambios sin guardar"></i>)}</b>
      <small className="muted">{P.song.editable ? P.song.mod || 'mod' : 'solo referencia'}</small></>) : (<span className="muted">{S.loadingSong ? 'Abriendo...' : 'Ninguna cancion abierta'}</span>)}</div>
    <i className="tbDiv" /><label className="venue">Escenario <select value={S.venueName || ''} onChange={e => setVenue(e.target.value)}>
      {S.venues.map(v => (<option key={v} value={v}>{v.replace(/^Z_/, '')}</option>))}</select></label>
    <span className="sep"></span>
    <Btn icon="undo" label="Deshacer" title="Deshacer (Ctrl+Z)" onClick={undo} disabled={!S.undo.length} cls="ghost" />
    <Btn icon="redo" label="Rehacer" title="Rehacer (Ctrl+Y)" onClick={redo} disabled={!S.redo.length} cls="ghost" />
    <i className="tbDiv" />
    <Btn icon="save" label="Guardar" title="Guarda el proyecto en EditorSong/projects (no toca el juego) - Ctrl+S" onClick={save} disabled={!P} />
    <Btn icon="reload" label="Releer" title="Descarta el proyecto guardado y vuelve a leer la cancion del juego" onClick={reload} disabled={!P} cls="ghost" />
    <Btn icon="history" label="Copias" title="Copias de seguridad de la cancion" onClick={backups} disabled={!P} cls="ghost" />
    <i className="tbDiv" />
    <Btn icon="build" label="Compilar al juego" title="Escribe la cancion en el mod (con copia de seguridad) y la valida" onClick={build}
      disabled={!P?.song.editable} cls="primary" /></>);
}
