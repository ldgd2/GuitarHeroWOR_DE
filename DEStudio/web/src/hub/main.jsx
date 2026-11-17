// Inicio de DE Studio: elegir que crear (cancion personalizada o interfaz 2D / 2.5D / 3D) y la biblioteca de
// proyectos (.desp) para abrirlos, guardar una copia en otra carpeta o borrarlos.
import React, { useEffect, useMemo, useState } from 'react';
import { createRoot } from 'react-dom/client';
import '../css/base.css';
import './hub.css';
import { Icon } from '../ui/icons.jsx';
import { api, editorUrl, listProjects, openFromDisk, saveCopyToDisk } from '../studio/files.js';

const MODES = [['2d', '2D', 'screen', 'Pantallas planas con imagenes, SVG, textos, formas y botones que llevan a otras pantallas.'],
  ['25d', '2.5D', 'film', 'Capas a distintas profundidades y una camara que se mueve: paralaje.'],
  ['3d', '3D', 'cube', 'Planos en el espacio con varias camaras, claves y cortes.']];
const MODE_LABEL = { '2d': '2D', '25d': '2.5D', '3d': '3D' };
const ago = s => {
  const d = (Date.now() / 1000 - s);
  if (d < 90) return 'hace un momento';
  if (d < 3600) return `hace ${Math.round(d / 60)} min`;
  if (d < 86400) return `hace ${Math.round(d / 3600)} h`;
  return new Date(s * 1000).toLocaleDateString();
};

function Hub() {
  const [lib, setLib] = useState(null);
  const [folder, setFolder] = useState('');
  const [songs, setSongs] = useState([]);
  const [song, setSong] = useState('');
  const [q, setQ] = useState('');
  const [filter, setFilter] = useState('all');
  const [confirmDel, setConfirmDel] = useState(null);
  const [msg, setMsg] = useState(null);
  const toast = (text, kind = 'ok') => { setMsg({ text, kind }); clearTimeout(toast.h); toast.h = setTimeout(() => setMsg(null), 6000); };
  const refresh = () => listProjects().then(r => { setLib(r.files); setFolder(r.folder); }).catch(e => toast(e.message, 'err'));
  useEffect(() => {
    refresh();
    api('/api/songs').then(s => { const ed = s.filter(x => x.editable); setSongs(ed); setSong(ed[0]?.id || ''); }).catch(e => toast(e.message, 'err'));
  }, []);
  const shown = useMemo(() => (lib || []).filter(r => (filter === 'all' || r.kind === filter) &&
    (!q || (r.file + ' ' + (r.title || '')).toLowerCase().includes(q.toLowerCase()))), [lib, q, filter]);
  const songName = id => songs.find(s => s.id === id)?.name;

  const openDisk = async () => {
    try { const r = await openFromDisk(); if (r) location.href = editorUrl(r); } catch (e) { toast(e.message, 'err'); }
  };
  const del = async file => {
    try { await api('/api/files/delete', { file }); setConfirmDel(null); refresh(); toast(`Borrado ${file}.desp`); } catch (e) { toast(e.message, 'err'); }
  };

  return (
    <div className="hub">
      <header className="hub-top">
        <div className="hub-brand"><Icon n="package" s={22} /><b>DE Studio</b><span>Warriors of Rock Definitive Edition</span></div>
        <span className="hub-grow" />
        <button onClick={openDisk}><Icon n="folder" />Abrir archivo .desp</button>
      </header>

      <main className="hub-main">
        <section className="hub-create">
          <h2>Crear</h2>
          <div className="hub-cards">
            <article className="hub-card">
              <div className="hub-cardhead"><Icon n="music" s={26} /><div><h3>Cancion personalizada</h3>
                <p>Escena de una cancion de un mod: banda, camaras, tomas, luces, efectos, captura del movil. Se compila al PAK de la cancion.</p></div></div>
              <div className="hub-cardrow">
                <select value={song} onChange={e => setSong(e.target.value)} disabled={!songs.length}>
                  {!songs.length && <option>No hay canciones de mods</option>}
                  {songs.map(s => <option key={s.id} value={s.id}>{s.name}  ({s.mod})</option>)}
                </select>
                <a className={'hub-btn primary' + (song ? '' : ' off')} href={song ? `cancion.html?song=${encodeURIComponent(song)}` : undefined}><Icon n="edit" />Abrir</a>
              </div>
            </article>
            <article className="hub-card">
              <div className="hub-cardhead"><Icon n="screen" s={26} /><div><h3>Interfaz del juego</h3>
                <p>Pantallas y menus con capas, imagenes del juego o tuyas, claves y clips de entrada, bucle y salida. Elige el tipo:</p></div></div>
              <div className="hub-modes">
                {MODES.map(([k, label, icon, desc]) => (
                  <a key={k} className="hub-mode" href={`ui.html?nuevo=${k}`}><Icon n={icon} s={20} /><b>{label}</b><span>{desc}</span></a>
                ))}
              </div>
            </article>
          </div>
        </section>

        <section className="hub-lib">
          <div className="hub-libbar">
            <h2>Proyectos</h2>
            <div className="hub-tabs">
              {[['all', 'Todos'], ['song', 'Canciones'], ['ui', 'Interfaces']].map(([k, v]) =>
                <button key={k} className={filter === k ? 'on' : ''} onClick={() => setFilter(k)}>{v}</button>)}
            </div>
            <span className="hub-grow" />
            <input className="hub-search" placeholder="Buscar" value={q} onChange={e => setQ(e.target.value)} />
          </div>
          <div className="hub-list">
            {lib === null && <div className="hub-muted">Leyendo la biblioteca...</div>}
            {lib && !shown.length && <div className="hub-muted">{lib.length ? 'Nada coincide.' : 'Todavia no hay proyectos. Crea uno arriba o abre un archivo .desp.'}</div>}
            {shown.map(r => (
              <div key={r.file} className="hub-row">
                <Icon n={KIND[r.kind][1]} s={18} />
                <a className="hub-rowmain" href={editorUrl(r)}>
                  <b>{r.file}</b>
                  <span>{KIND[r.kind][0]}{r.kind === 'ui' ? ' ' + (MODE_LABEL[r.mode] || '2D') : r.song ? '  ' + (songName(r.song) || r.title || r.song) : ''}</span>
                </a>
                <span className="hub-meta">{ago(r.modified)}  {(r.size / 1024).toFixed(0)} KB</span>
                {confirmDel === r.file
                  ? <><span className="hub-warn">Borrar {r.file}.desp?</span>
                    <button className="danger" onClick={() => del(r.file)}>Borrar</button>
                    <button onClick={() => setConfirmDel(null)}>Cancelar</button></>
                  : <>
                    <button className="hub-icon" title="Guardar una copia en otra carpeta" onClick={async () => { try { const n = await saveCopyToDisk(r.file); if (n) toast(`Copia guardada: ${n}`); } catch (e) { toast(e.message, 'err'); } }}><Icon n="copy" /></button>
                    <button className="hub-icon" title="Borrar de la biblioteca" onClick={() => setConfirmDel(r.file)}><Icon n="trash" /></button>
                  </>}
              </div>
            ))}
          </div>
          {folder && <div className="hub-muted hub-folder">Biblioteca: <code>{folder}</code>  (un archivo .desp por proyecto, con todo dentro)</div>}
        </section>
      </main>
      {msg && <div className={'hub-toast ' + msg.kind}><Icon n={msg.kind === 'err' ? 'alert' : 'check'} />{msg.text}</div>}
    </div>
  );
}

createRoot(document.getElementById('app')).render(<Hub />);
