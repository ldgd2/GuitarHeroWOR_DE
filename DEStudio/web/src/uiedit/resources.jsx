// Recursos del editor de interfaces:
//  - importar archivos propios (PNG, JPG, WEBP, SVG, TTF, OTF, WOFF) desde el panel, la ventana grande o
//    soltandolos en cualquier parte del editor,
//  - la ventana grande de recursos (imagenes del proyecto, fuentes y las imagenes del juego),
//  - el selector de fuentes: nativas de Warriors of Rock, las del proyecto y las de este equipo.
import React, { useEffect, useMemo, useRef, useState } from 'react';
import { Icon } from '../ui/icons.jsx';
import { AssetBrowser, fileToBase64 } from '../studio/AssetBrowser.jsx';
import { api } from '../studio/files.js';
import { SYSTEM_FONTS, fontCatalog, isWorFont, loadWorFont, registerFont, registerSystemFont, worFont } from '../studio/raster.js';
import { assetUrl } from './view2d.js';

export const ACCEPT = '.png,.jpg,.jpeg,.webp,.svg,.ttf,.otf,.woff,.woff2';
const OK_EXT = /\.(png|jpe?g|webp|svg|ttf|otf|woff2?)$/i;
const BUILTIN = ['Inter Variable', 'JetBrains Mono Variable'];

/** nombre visible de una fuente */
export function fontLabel(font, cat) {
  if (!font) return 'Inter';
  if (isWorFont(font)) {
    const w = cat?.wor?.find(x => x.id === font);
    return 'WoR  ' + (w?.label || font.slice(4));
  }
  return font.replace(' Variable', '');
}

/** prepara una fuente antes de usarla (para que las medidas del texto salgan bien a la primera) */
export async function ensureFont(font) {
  if (!font) return;
  if (isWorFont(font)) await loadWorFont(font);
  else if (!BUILTIN.includes(font)) await registerSystemFont(font).catch(() => false);
  if (document.fonts?.ready) await document.fonts.ready;
}

/** sube archivos al proyecto. Las fuentes quedan registradas y en proj.fonts. -> [{file, kind, w, h}] */
export async function uploadFiles(proj, files, editProj, toast) {
  const out = [];
  const bad = files.filter(f => !OK_EXT.test(f.name));
  if (bad.length) toast(`No se admite: ${bad.map(f => f.name).join(', ')} (PNG, JPG, WEBP, SVG, TTF, OTF, WOFF)`, 'warn');
  for (const f of files.filter(f => OK_EXT.test(f.name))) {
    try {
      const r = await api('/api/ui/upload', { pid: proj.id, filename: f.name, data: await fileToBase64(f) });
      if (r.kind === 'font') {
        const family = r.file.replace(/\.[a-z0-9]+$/, '');
        await registerFont(family, assetUrl(proj, r.file));
        editProj(p => { p.fonts = [...(p.fonts || []).filter(x => x.file !== r.file), { family, file: r.file }]; });
        r.family = family;
      }
      out.push(r);
    } catch (e) { toast(`${f.name}: ${e.message}`, 'err'); }
  }
  return out;
}

/** muestra de una fuente nativa del juego (la dibuja con su propio atlas) */
export function WorSample({ font, text = 'Aa Bb 123', h = 28, color = '#e8e8ee' }) {
  const ref = useRef(null);
  const [ok, setOk] = useState(!!worFont(font));
  useEffect(() => { let live = true; loadWorFont(font).then(() => live && setOk(true)); return () => { live = false; }; }, [font]);
  useEffect(() => {
    const F = worFont(font), c = ref.current;
    if (!F || !c) return;
    const m = F.m, k = h / m.line, dpr = window.devicePixelRatio || 1;
    let w = 0;
    const glyph = ch => m.glyphs[ch.codePointAt(0)] || m.glyphs[ch.toUpperCase().codePointAt(0)];
    for (const ch of text) { const g = glyph(ch); w += ((g ? g[4] + (g[8] || 0) : m.space) + m.spacing) * k; }
    c.width = Math.ceil(Math.max(1, w) * dpr); c.height = Math.ceil(h * dpr);
    c.style.width = `${Math.ceil(w)}px`; c.style.height = `${h}px`;
    const g2 = c.getContext('2d');
    g2.scale(dpr, dpr);
    const base = (h - (m.ascent + m.descent) * k) / 2 + m.ascent * k;
    let x = 0;
    for (const ch of text) {
      const g = glyph(ch);
      if (g) { g2.drawImage(F.img, g[0], g[1], g[2], g[3], x + (g[7] || 0) * k, base - (g[5] - g[6]) * k, g[4] * k, g[5] * k); x += (g[4] + (g[8] || 0) + m.spacing) * k; }
      else x += (m.space + m.spacing) * k;
    }
    g2.setTransform(1, 0, 0, 1, 0, 0);
    g2.globalCompositeOperation = 'source-in'; g2.fillStyle = color; g2.fillRect(0, 0, c.width, c.height);
  }, [ok, font, text, h, color]);
  return ok ? <canvas ref={ref} className="ue-worsample" /> : <span className="st-muted">Cargando...</span>;
}

/** lista de fuentes por grupos (la usan el selector y la ventana de recursos) */
function useFontGroups(proj) {
  const [cat, setCat] = useState(null);
  useEffect(() => { fontCatalog().then(setCat); }, []);
  const own = (proj.fonts || []).map(f => f.family);
  const system = (cat?.system || []).map(f => f.family);
  const sysSet = new Set(system.map(s => s.toLowerCase()));
  const extra = SYSTEM_FONTS.filter(f => !BUILTIN.includes(f) && !sysSet.has(f.toLowerCase()));
  return {
    cat,
    groups: [
      { key: 'wor', title: 'Warriors of Rock', note: 'ya estan en el juego', items: (cat?.wor || []).map(w => w.id) },
      { key: 'own', title: 'Del proyecto', note: 'importadas', items: own },
      { key: 'editor', title: 'Del editor', note: '', items: BUILTIN },
      { key: 'sys', title: 'De este equipo', note: 'se copian al juego al exportar', items: [...system, ...extra] },
    ],
  };
}

/** selector de fuentes con busqueda y muestra de cada una */
export function FontPicker({ proj, value, onChange, compact }) {
  const [open, setOpen] = useState(false);
  const [pos, setPos] = useState(null);
  const [q, setQ] = useState('');
  const { cat, groups } = useFontGroups(proj);
  const toggle = () => {
    if (open) return setOpen(false);
    // la lista flota sobre todo (fija a la ventana) para que ningun panel la recorte
    const r = box.current.getBoundingClientRect(), W = 320, H = Math.min(520, window.innerHeight * 0.7);
    const left = Math.max(8, Math.min(window.innerWidth - W - 8, r.right - W > 8 && r.left + W > window.innerWidth - 8 ? r.right - W : r.left));
    const below = window.innerHeight - r.bottom - 8 >= Math.min(H, 300);
    setPos({ left, width: W, maxHeight: below ? Math.min(H, window.innerHeight - r.bottom - 12) : Math.min(H, r.top - 12),
      ...(below ? { top: r.bottom + 4 } : { bottom: window.innerHeight - r.top + 4 }) });
    setOpen(true);
  };
  const box = useRef(null);
  useEffect(() => {
    if (!open) return;
    const off = e => { if (box.current && !box.current.contains(e.target)) setOpen(false); };
    const esc = e => { if (e.key === 'Escape') { e.stopPropagation(); setOpen(false); } };
    document.addEventListener('mousedown', off); document.addEventListener('keydown', esc, true);
    return () => { document.removeEventListener('mousedown', off); document.removeEventListener('keydown', esc, true); };
  }, [open]);
  const pick = async f => { setOpen(false); await ensureFont(f); onChange(f); };
  const needle = q.trim().toLowerCase();
  const match = f => !needle || fontLabel(f, cat).toLowerCase().includes(needle);
  return (
    <div className={'ue-fontpick' + (compact ? ' compact' : '')} ref={box}>
      <button className="ue-fontbtn" onClick={toggle} title="Fuente">
        {isWorFont(value) ? <span>{fontLabel(value, cat).replace('WoR  ', 'WoR: ')}</span>
          : <span style={{ fontFamily: `"${value}"` }}>{fontLabel(value, cat)}</span>}
        <Icon n="chevdown" s={12} />
      </button>
      {open && pos && <div className="ue-fontmenu" style={pos}>
        <div className="ue-fontsearch"><Icon n="search" s={14} />
          <input autoFocus placeholder="Buscar fuente" value={q} onChange={e => setQ(e.target.value)} /></div>
        <div className="ue-fontlist">
          {groups.map(g => {
            const items = g.items.filter(match);
            if (!items.length) return null;
            return <div key={g.key}>
              <div className="ue-fonthead">{g.title}{g.note && <span>{g.note}</span>}</div>
              {items.map(f => (
                <button key={f} className={'ue-fontitem' + (f === value ? ' on' : '')} onClick={() => pick(f)} title={fontLabel(f, cat)}>
                  {isWorFont(f) ? <><WorSample font={f} text="Aa Bb 123" h={22} /><small>{fontLabel(f, cat).slice(5)}</small></>
                    : <span style={{ fontFamily: `"${f}"` }}>{fontLabel(f, cat)}</span>}
                </button>))}
            </div>;
          })}
          {!cat && <div className="st-muted ue-pad">Leyendo las fuentes...</div>}
        </div>
      </div>}
    </div>);
}

/** ventana grande de recursos */
export function ResourcesWindow({ proj, editProj, onPickGame, onPickAsset, onPickFont, toast, close, start = 'images' }) {
  const [sec, setSec] = useState(start);
  const [list, setList] = useState(null);
  const [q, setQ] = useState('');
  const [size, setSize] = useState(150);
  const [drag, setDrag] = useState(false);
  const { cat, groups } = useFontGroups(proj);
  const reload = () => api(`/api/ui/assets?pid=${encodeURIComponent(proj.id)}`).then(r => setList(r.assets)).catch(e => toast(e.message, 'err'));
  useEffect(() => { reload(); }, [proj.id]);
  const upload = async files => {
    const r = await uploadFiles(proj, files, editProj, toast);
    if (r.length) toast(`${r.length} archivo${r.length > 1 ? 's' : ''} importado${r.length > 1 ? 's' : ''}`);
    reload();
  };
  const needle = q.trim().toLowerCase();
  const imgs = (list || []).filter(a => a.kind !== 'font' && (!needle || a.file.toLowerCase().includes(needle)));
  const nav = [['images', 'tex', 'Imagenes del proyecto', list ? list.filter(a => a.kind !== 'font').length : null],
    ['fonts', 'font', 'Fuentes', null], ['game', 'package', 'Imagenes del juego', null]];
  return (
    <div className="st-modal" onMouseDown={close}>
      <div className={'st-modalbox ue-reswin' + (drag ? ' drag' : '')} onMouseDown={e => e.stopPropagation()}
        onDragOver={e => { if ([...e.dataTransfer.types].includes('Files')) { e.preventDefault(); e.stopPropagation(); setDrag(true); } }}
        onDragLeave={e => { if (!e.currentTarget.contains(e.relatedTarget)) setDrag(false); }}
        onDrop={e => { e.preventDefault(); e.stopPropagation(); setDrag(false); upload([...e.dataTransfer.files]); }}>
        <h3><Icon n="tex" />Recursos<span className="st-grow" />
          <button className="st-icon" onClick={close} title="Cerrar (Esc)"><Icon n="x" /></button></h3>
        <div className="ue-resbody">
          <nav className="ue-resnav">
            {nav.map(([k, ic, label, n]) => (
              <button key={k} className={sec === k ? 'on' : ''} onClick={() => setSec(k)}>
                <Icon n={ic} s={16} /><span>{label}</span>{n != null && <em>{n}</em>}</button>))}
            <span className="st-grow" />
            <label className="st-file ue-upload"><Icon n="upload" />Importar archivos
              <input type="file" multiple accept={ACCEPT} onChange={e => { const f = [...e.target.files]; e.target.value = ''; upload(f); }} /></label>
            <p className="st-muted">Tambien puedes soltar archivos en cualquier parte del editor.</p>
          </nav>
          <section className="ue-resmain">
            {sec !== 'game' && <div className="ue-restools">
              <span className="ue-fontsearch"><Icon n="search" s={14} />
                <input placeholder={sec === 'fonts' ? 'Buscar fuente' : 'Buscar imagen'} value={q} onChange={e => setQ(e.target.value)} /></span>
              {sec === 'images' && <label className="ue-ressize" title="Tamano de las miniaturas"><Icon n="zoomin" s={14} />
                <input type="range" min="90" max="280" value={size} onChange={e => setSize(+e.target.value)} /></label>}
            </div>}
            {sec === 'images' && <div className="ue-resgrid" style={{ gridTemplateColumns: `repeat(auto-fill, minmax(${size}px, 1fr))` }}>
              {list === null && <div className="st-muted">Leyendo...</div>}
              {list && !imgs.length && <div className="ue-resempty"><Icon n="upload" s={28} />
                <b>{needle ? 'Nada coincide con la busqueda' : 'Aun no hay imagenes propias'}</b>
                <span>Suelta aqui PNG, JPG, WEBP o SVG, o usa Importar archivos.</span></div>}
              {imgs.map(a => (
                <button key={a.file} className="ue-rescard" onClick={() => { onPickAsset(a); close(); }} title="Anadir a la pantalla (o cambiar la imagen seleccionada)">
                  <span className="ue-reschk" style={{ height: size * 0.75 }}><img loading="lazy" src={assetUrl(proj, a.file)} alt="" /></span>
                  <b>{a.file}</b><small>{a.kind === 'svg' ? 'SVG vectorial' : `${a.w} x ${a.h}`}  {Math.max(1, Math.round(a.size / 1024))} KB</small>
                </button>))}
            </div>}
            {sec === 'fonts' && <div className="ue-resfonts">
              {groups.map(g => {
                const items = g.items.filter(f => !needle || fontLabel(f, cat).toLowerCase().includes(needle));
                if (!items.length) return null;
                return <div key={g.key} className="ue-resfontgroup">
                  <h4>{g.title}{g.note && <span>{g.note}</span>}<em>{items.length}</em></h4>
                  <div className="ue-resfontgrid">
                    {items.map(f => (
                      <button key={f} className="ue-resfont" onClick={async () => { await ensureFont(f); onPickFont(f); }}>
                        {isWorFont(f) ? <WorSample font={f} text="Rock Aa 123" h={34} />
                          : <span className="ue-resfontsample" style={{ fontFamily: `"${f}"` }}>Rock Aa 123</span>}
                        <small>{fontLabel(f, cat)}</small>
                      </button>))}
                  </div>
                </div>;
              })}
              {!cat && <div className="st-muted">Leyendo las fuentes...</div>}
            </div>}
            {sec === 'game' && <div className="ue-resgame"><AssetBrowser title="" warnNotLoaded={false} onError={m => toast(m, 'err')}
              onPick={a => { onPickGame(a); close(); }} /></div>}
          </section>
        </div>
        {drag && <div className="ue-dropzone"><Icon n="upload" s={34} /><b>Suelta para importar</b></div>}
      </div>
    </div>);
}

/** capa encima del editor mientras se arrastran archivos desde fuera */
export function useGlobalDrop(onFiles) {
  const [over, setOver] = useState(false);
  const depth = useRef(0);
  const cb = useRef(onFiles); cb.current = onFiles;
  useEffect(() => {
    const isFiles = e => [...(e.dataTransfer?.types || [])].includes('Files');
    const enter = e => { if (!isFiles(e)) return; depth.current++; setOver(true); };
    const leave = e => { if (!isFiles(e)) return; depth.current = Math.max(0, depth.current - 1); if (!depth.current) setOver(false); };
    const overH = e => { if (isFiles(e)) e.preventDefault(); };
    const drop = e => {
      if (!isFiles(e)) return;
      e.preventDefault(); depth.current = 0; setOver(false);
      cb.current([...e.dataTransfer.files], e);
    };
    window.addEventListener('dragenter', enter); window.addEventListener('dragleave', leave);
    window.addEventListener('dragover', overH); window.addEventListener('drop', drop);
    return () => {
      window.removeEventListener('dragenter', enter); window.removeEventListener('dragleave', leave);
      window.removeEventListener('dragover', overH); window.removeEventListener('drop', drop);
    };
  }, []);
  return over;
}
