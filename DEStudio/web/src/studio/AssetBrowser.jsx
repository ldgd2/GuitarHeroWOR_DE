// Navegador de imagenes compartido: imagenes de interfaz sacadas de los PAK del juego (con su nombre real) e
// importacion de PNG propios. onPick({pak, key, name, w, h}) / onUpload(file).
import React, { useEffect, useMemo, useState } from 'react';
import { Icon } from '../ui/icons.jsx';
import { api } from './files.js';

export const gameImageUrl = (pak, key) => `/api/ui/image?pak=${encodeURIComponent(pak)}&key=${key}`;

export function AssetBrowser({ title, hint, onPick, onUpload, onError, warnNotLoaded = true }) {
  const [paks, setPaks] = useState([]);
  const [always, setAlways] = useState([]);
  const [pak, setPak] = useState('ui_shared');
  const [imgs, setImgs] = useState(null);
  const [q, setQ] = useState('');
  useEffect(() => { api('/api/ui/paks').then(r => { setPaks(r.paks); setAlways(r.always); }).catch(e => onError?.(e.message)); }, []);
  useEffect(() => {
    setImgs(null);
    api(`/api/ui/images?pak=${encodeURIComponent(pak)}`).then(r => setImgs(r.images)).catch(e => { setImgs([]); onError?.(e.message); });
  }, [pak]);
  const shown = useMemo(() => (imgs || []).filter(i => !q || (i.name + i.key).toLowerCase().includes(q.toLowerCase())), [imgs, q]);
  return (
    <section className="st-box st-assets">
      <h3>{title}{hint && <span className="st-muted">{hint}</span>}</h3>
      <div className="st-assetbar">
        <select value={pak} onChange={e => setPak(e.target.value)} title="PAK del juego">
          {paks.map(n => <option key={n} value={n}>{n}{always.includes(n) ? '  (siempre cargado)' : ''}</option>)}
        </select>
        {onUpload && <label className="st-file st-icon" title="Importar un PNG propio">
          <Icon n="upload" /><input type="file" accept="image/png" onChange={e => { const f = e.target.files[0]; e.target.value = ''; if (f) onUpload(f); }} />
        </label>}
      </div>
      <input className="st-search" placeholder="Buscar por nombre" value={q} onChange={e => setQ(e.target.value)} />
      {warnNotLoaded && pak && always.length > 0 && !always.includes(pak) &&
        <div className="st-note"><Icon n="warn" s={14} />El juego solo carga este PAK en su pantalla: en 2D, fuera de ella la imagen sale vacia.</div>}
      <div className="st-grid">
        {imgs === null && <div className="st-muted">Leyendo {pak}...</div>}
        {shown.map(im => (
          <button key={im.key} className="st-thumb" onClick={() => onPick({ ...im, pak })} title={`${im.name || im.key}  ${im.w} x ${im.h}  ${im.fmt}`}>
            <img loading="lazy" src={gameImageUrl(pak, im.key)} alt="" />
            <span>{im.name || im.key}</span>
          </button>
        ))}
      </div>
    </section>
  );
}

/** PNG del disco -> base64 (sin cabecera data:) */
export const fileToBase64 = f => new Promise(res => { const r = new FileReader(); r.onload = () => res(r.result.split(',')[1]); r.readAsDataURL(f); });
