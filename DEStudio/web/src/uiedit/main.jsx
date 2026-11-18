// Editor de interfaces de DE Studio (ui.html), al estilo de un programa de diseno: menus, herramientas, opciones,
// lienzo con reglas y guias, paneles de Propiedades / Recursos / Capas / Pantallas y linea de tiempo.
//   2D, 2.5D y 3D. Varias pantallas por interfaz; capas que son botones con acciones (ir a otra pantalla, volver,
//   cerrar, ejecutar un script del juego, cambiar una variable). Vista previa interactiva. En el juego la dibuja
//   re-wor (src/de/ui3d.cpp) y la abren los scripts re_ui_<nombre>_show / _hide / _goto. Ver LEEME.md.
import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';
import '../css/base.css';
import '../studio/studio.css';
import './ue.css';
import { Icon } from '../ui/icons.jsx';
import { Timeline } from '../studio/Timeline.jsx';
import { api, listProjects, openFromDisk, saveCopyToDisk } from '../studio/files.js';
import { keyTimes, moveKeys, uid } from '../studio/keys.js';
import { registerFont, textSize } from '../studio/raster.js';
import { autoTransitions, defaultHints, hintLayers } from './presets.js';
import { Stage2D, fitView } from './Stage2D.jsx';
import { Stage3D } from './Stage3D.jsx';
import { Preview } from './Preview.jsx';
import { LogicWindow } from './LogicWindow.jsx';
import { fileToBase64 } from '../studio/AssetBrowser.jsx';
import { animClips, animSource, isAnimClip } from './anims.js';
import { bakeTextures } from './bake.js';
import { MenuBar, OptionsBar, TOOL_KEYS, ToolBar } from './chrome.jsx';
import { ResourcesWindow, ensureFont, uploadFiles, useGlobalDrop } from './resources.jsx';
import { AssetsPanel, AlignBar, LayersPanel, PropsPanel, ScreensPanel, moveLayers } from './panels.jsx';
import {
  CAM25, CAM3D_Z, CLIPS, DEFAULT_SHAPE, DEFAULT_TEXT, History, MODES, TYPE_ICON, ancestors, clean, clipOf, descendants,
  newLayer, newProject, newScreen, normalize, propsFor, setProp, stateAt, activeCamAt, W, H, LIVE_PROPS, LIVE_DEFAULTS, BLEND_MODES, cam25At, restorePanel } from './model.js';
import { assetUrl, fitText } from './view2d.js';

const clone = o => structuredClone(o);
const DEFAULT_OPTS = { text: { ...DEFAULT_TEXT, color: [255, 255, 255] }, shape: JSON.parse(JSON.stringify(DEFAULT_SHAPE)),
  snap: true, snapLayers: true, grid: false, gridSize: 20, guides: true, showButtons: false, bg: 'dark', outside: true };
let clipboard = null;   // capas copiadas (con sus claves): sirven entre pantallas

function App() {
  const [proj, setProj] = useState(null);
  const [sid, setSidRaw] = useState(null);
  const [sel, setSelRaw] = useState([]);
  const [selKey, setSelKey] = useState(null);
  const [selCut, setSelCut] = useState(null);
  const [clip, setClip] = useState('base');
  const [t, setT] = useState(0);
  const [playing, setPlaying] = useState(false);
  const [tool, setTool] = useState('move');
  const [opts, setOptsRaw] = useState(DEFAULT_OPTS);
  const [view, setView] = useState({ z: 0.5, x: 40, y: 40, fit: true });
  const [editingText, setEditingText] = useState(null);
  const [topTab, setTopTab] = useState('props');
  const [botTab, setBotTab] = useState('layers');
  const [showTl, setShowTl] = useState(true);
  // vista de animacion: interfaz a la izquierda, claves a la derecha (a toda la altura)
  const [animSide, setAnimSide] = useState(() => { try { return localStorage.getItem('ue-anim-side') === '1'; } catch { return false; } });
  useEffect(() => { try { localStorage.setItem('ue-anim-side', animSide ? '1' : '0'); } catch { /* sin almacenamiento */ } }, [animSide]);
  const [msg, setMsg] = useState(null);
  const [busy, setBusy] = useState(null);
  const [qb, setQb] = useState(null);
  const [ref, setRef] = useState(null);
  const [refAlpha, setRefAlpha] = useState(0.5);
  const [dialog, setDialog] = useState(null);
  const [preview, setPreview] = useState(false);
  const [logicAt, setLogicAt] = useState(null);   // {tab, jump}: como se abre la ventana Logica
  const hist = useRef(new History());
  const projRef = useRef(null); projRef.current = proj;
  const sidRef = useRef(null); sidRef.current = sid;
  const dragBase = useRef(null);

  const toast = useCallback((text, kind = 'ok') => { setMsg({ text, kind }); clearTimeout(toast.h); toast.h = setTimeout(() => setMsg(null), 7000); }, []);
  const scrOf = (q, id) => q.screens.find(s => s.id === id) || q.screens[0];
  const edit = useCallback((fn, record = true) => setProj(p => { if (record) hist.current.push(p); const q = clone(p); fn(scrOf(q, sidRef.current), q); return q; }), []);
  const editProj = useCallback((fn, record = true) => setProj(p => { if (record) hist.current.push(p); const q = clone(p); fn(q); return q; }), []);
  const live = useMemo(() => ({
    begin: () => { hist.current.push(projRef.current); dragBase.current = clone(projRef.current); },
    set: fn => setProj(() => { const q = clone(dragBase.current); fn(scrOf(q, sidRef.current), q); return q; }),
    setProj: fn => setProj(() => { const q = clone(dragBase.current); fn(q); return q; }),
  }), []);
  const setOpts = fn => setOptsRaw(o => { const q = clone(o); fn(q); return q; });
  const setSel = ids => { setSelRaw(ids); setSelCut(null); setEditingText(null); };
  const setSid = id => { setSidRaw(id); setSelRaw([]); setSelKey(null); setSelCut(null); setEditingText(null); setClip('base'); };

  const load = q => {
    hist.current = new History(); setProj(q); setSidRaw(q.screens.find(s => s.id === q.start)?.id || q.screens[0].id);
    setSelRaw([]); setSelKey(null); setSelCut(null); setClip('base'); setT(0); setDialog(null); setView(v => ({ ...v, fit: true }));
    // fuentes del proyecto y medidas de los textos con ellas
    const used = new Set(q.screens.flatMap(s => s.layers.filter(l => l.type === 'text').map(l => l.text?.font)).filter(Boolean));
    const own = new Set((q.fonts || []).map(f => f.family));
    Promise.all([...(q.fonts || []).map(f => registerFont(f.family, assetUrl(q, f.file))),
      ...[...used].filter(f => !own.has(f)).map(ensureFont)]).then(() => document.fonts.ready).then(() =>
      setProj(p => { if (!p || p.id !== q.id) return p; const c = clone(p); c.screens.forEach(s => s.layers.forEach(fitText)); return c; }));
  };

  useEffect(() => {
    const qs = new URLSearchParams(location.search);
    const file = decodeURIComponent(location.hash.slice(1));
    if (file) openFile(file);
    else if (MODES[qs.get('nuevo')]) load(newProject(qs.get('nuevo'), clean(qs.get('nombre') || '') || 'mi_interfaz'));
    else setDialog('new');
  }, []);

  const doc = proj ? scrOf(proj, sid) : null;
  const mode = proj?.mode || '2d';
  const dur = doc && clip !== 'base' ? doc.clips[clip]?.duration || 1 : 0;
  // animaciones con nombre (se lanzan desde el codigo con play_anim): pestanas extra de la linea de tiempo
  useEffect(() => { if (doc && clip !== 'base' && !doc.clips[clip]) setClip('intro'); }, [doc?.id, clip]);
  const curAnim = doc && isAnimClip(clip, doc.clips[clip]) ? doc.clips[clip] : null;
  const addAnim = () => {
    const used = new Set(proj.screens.flatMap(s => animClips(s).map(([, c]) => c.name)));
    let n = 1;
    while (used.has(`animacion ${n}`)) n++;
    const key = 'a_' + Math.random().toString(36).slice(2, 9);
    edit(d => { d.clips[key] = { custom: true, name: `animacion ${n}`, duration: 1, tracks: {}, ...(mode === '3d' ? { cuts: [] } : {}) }; });
    setClip(key); setT(0);
  };
  const delAnim = () => { const k = clip; setClip('intro'); edit(d => { delete d.clips[k]; }); };
  // clic derecho en una capa de la linea de tiempo: claves de sus propiedades y efectos, y su modo de fusion
  const LIVE_ON = { e_glow: 24, e_glowa: 1, e_blur: 8, e_chroma: 6, e_bright: 1.6, e_sat: 0, e_inv: 1, e_hue: 120, e_grain: 0.5, e_flicker: 0.6, e_shake: 6 };
  const rowMenu = (id, at) => {
    if (clip === 'base') return [{ label: 'Elige Entrada, Bucle, Salida o una animacion para poner claves', act: () => {} }];
    const isCam = id === CAM25;
    const l = isCam ? null : doc.layers.find(q => q.id === id);
    if (!isCam && !l) return [];
    const st = isCam ? cam25At(doc, clip, at) : stateAt(doc, l, clip, at);
    const key = (prop, v, from) => edit(d => {
      const L = isCam ? { id: CAM25 } : d.layers.find(q => q.id === id);
      const has = d.clips[clip]?.tracks?.[id]?.[prop]?.length;
      if (!has && from !== undefined && at > 0.001) setProp(d, clip, L, prop, from, 0);   // que el efecto entre animado
      setProp(d, clip, L, prop, v, at);
    });
    const items = [{ sep: 'Clave de' }];
    for (const [p, label] of propsFor(mode, isCam ? CAM25 : l.type)) items.push({ label, icon: 'keyframe', act: () => key(p, st[p]) });
    if (!isCam && ['image', 'text', 'shape'].includes(l.type)) {
      items.push({ sep: 'Efecto (aparece animado)' });
      for (const [p, label] of LIVE_PROPS) {
        if (p === 'e_glowc') { items.push({ label, icon: 'palette', act: () => key(p, st[p] ?? LIVE_DEFAULTS[p]) }); continue; }
        const cur = st[p] ?? LIVE_DEFAULTS[p];
        const on = cur !== LIVE_DEFAULTS[p] ? cur : LIVE_ON[p] ?? cur;
        items.push({ label, icon: p === 'e_glow' ? 'glow' : p === 'e_blur' ? 'blur' : p === 'e_chroma' ? 'chroma' : p === 'e_grain' ? 'grain' : p === 'e_shake' ? 'shake' : p === 'e_flicker' ? 'flicker' : p === 'e_hue' ? 'hue' : 'fx',
          act: () => key(p, on, LIVE_DEFAULTS[p]) });
      }
      items.push({ sep: 'Modo de fusion' });
      for (const [k, v] of BLEND_MODES) items.push({ label: v, on: (l.blend || 'normal') === k, act: () => edit(d => { d.layers.find(q => q.id === id).blend = k; }) });
    }
    items.push({ sep: true });
    items.push({ label: 'Borrar sus claves en este clip', icon: 'trash', act: () => edit(d => { if (d.clips[clip]?.tracks) delete d.clips[clip].tracks[id]; }) });
    return items;
  };
  const animCall = curAnim ? `play_anim(${JSON.stringify(curAnim.name)})` : '';
  const selLayers = doc ? sel.map(id => doc.layers.find(l => l.id === id)).filter(Boolean) : [];

  useEffect(() => {
    if (!playing || clip === 'base') return;
    let raf, last = performance.now();
    const step = now => {
      const dt = (now - last) / 1000; last = now;
      setT(v => { let n = v + dt; if (n >= dur) { if (clip === 'loop') n %= Math.max(dur, 0.01); else { n = dur; setPlaying(false); } } return n; });
      raf = requestAnimationFrame(step);
    };
    raf = requestAnimationFrame(step);
    return () => cancelAnimationFrame(raf);
  }, [playing, clip, dur]);
  useEffect(() => { setT(0); setPlaying(false); setSelKey(null); setSelCut(null); }, [clip]);

  // ---------------------------------------------------------------- archivo
  async function openFile(file) {
    try {
      const p = normalize(await api(`/api/ui/project?file=${encodeURIComponent(file)}`));
      load(p); history.replaceState(null, '', location.pathname + '#' + encodeURIComponent(p.file || file)); toast(`Abierto ${file}`);
    } catch (e) { toast(e.message, 'err'); setDialog('new'); }
  }
  const save = async () => {
    if (!proj) return null;
    try {
      const r = await api('/api/ui/project', { ...proj, name: clean(proj.name) || 'interfaz' });
      setProj(p => ({ ...p, name: r.name, file: r.file }));
      history.replaceState(null, '', location.pathname + '#' + encodeURIComponent(r.file));
      toast(`Guardado en la biblioteca: ${r.file}.desp`);
      return r.file;
    } catch (e) { toast(e.message, 'err'); return null; }
  };
  const saveCopy = async () => { const f = await save(); if (!f) return; try { const n = await saveCopyToDisk(f); if (n) toast(`Copia guardada: ${n}`); } catch (e) { toast(e.message, 'err'); } };
  const openDisk = async () => {
    try { const r = await openFromDisk(); if (!r) return; if (r.kind !== 'ui') return toast('Ese archivo es un proyecto de cancion: abrelo desde el inicio.', 'warn'); openFile(r.file); }
    catch (e) { toast(e.message, 'err'); }
  };
  const body = async () => {
    setBusy('Preparando las imagenes para el juego...');
    try {
      const textures = await bakeTextures(proj, (i, n) => setBusy(`Imagenes para el juego: ${i} de ${n}`));
      const anims = animSource(proj);
      for (const w of anims.warnings.slice(0, 3)) toast(w, 'warn');
      return { project: { ...proj, name: clean(proj.name) || 'interfaz', logic: { ...(proj.logic || {}), anims: anims.text } }, textures };
    } finally { setBusy(null); }
  };
  const doExport = async () => {
    try {
      const r = await api('/api/ui/export', await body());
      toast(`Exportada: re_ui_${r.name}_show / _hide (${r.images} imagenes). Se carga al arrancar el juego.` + (r.warnings.length ? '  Avisos: ' + r.warnings.join('; ') : ''), r.warnings.length ? 'warn' : 'ok');
    } catch (e) { toast(e.message, 'err'); }
  };
  const test = async action => {
    try {
      const r = await api('/api/ui/test', action === 'show' ? { ...(await body()), action } : { project: proj, action });
      toast(r.ok ? (action === 'show' ? 'Abierta en el juego' : 'Salida en el juego') + (r.warnings?.length ? '  Avisos: ' + r.warnings.join('; ') : '') : 'El juego respondio: ' + r.replies.join(' | '), r.ok ? 'ok' : 'warn');
    } catch (e) { toast(e.message, 'err'); }
  };
  const showQb = async () => { try { setQb(await api('/api/ui/qb', await body())); } catch (e) { toast(e.message, 'err'); } };

  // ---------------------------------------------------------------- capas
  const nextOrder = (d, parent = null) => Math.max(0, ...d.layers.filter(l => (l.parent || null) === parent).map(l => l.base.order || 0)) + 1;
  const addLayer = (type, extra = {}, at) => {
    const l = newLayer(mode, type, extra);
    const parentGroup = selLayers.length === 1 && selLayers[0].type === 'group' && type !== 'camera' ? selLayers[0] : null;
    if (type === 'shape' && !extra.shape) { l.shape = clone(opts.shape); if (l.shape.kind === 'path' || l.shape.kind === 'line') l.shape.kind = 'rect'; }
    if (type === 'text' && !extra.text) { l.text = { ...clone(opts.text) }; fitText(l); }
    if (mode === '3d') {
      if (type === 'camera') { l.base.z = CAM3D_Z; l.name = `Camara ${doc.layers.filter(q => q.type === 'camera').length + 1}`; }
    } else if (at) { l.base.x = at[0]; l.base.y = at[1]; }
    if (parentGroup) {
      l.parent = parentGroup.id;
      if (mode === '3d') { l.base.x = 0; l.base.y = 0; l.base.z = 0; }
      else for (const g of [parentGroup, ...ancestors(doc, parentGroup)]) { l.base.x -= g.base.x; l.base.y -= g.base.y; }
    }
    edit(d => { l.base.order = nextOrder(d, l.parent || null); d.layers.push(l); if (type === 'camera' && !d.activeCam) d.activeCam = l.id; });
    setSel([l.id]);
    return l;
  };
  const removeLayers = ids => {
    ids = ids.filter(id => id !== CAM25);
    if (!ids.length) return;
    edit(d => {
      const gone = new Set();
      for (const id of ids) for (const g of descendants(d, id)) gone.add(g);
      d.layers = d.layers.filter(l => !gone.has(l.id));
      for (const c of Object.values(d.clips)) { for (const g of gone) delete c.tracks[g]; if (c.cuts) c.cuts = c.cuts.filter(x => !gone.has(x.cam)); }
      if (gone.has(d.activeCam)) d.activeCam = d.layers.find(l => l.type === 'camera')?.id || null;
      if (gone.has(d.focusStart)) d.focusStart = null;
      for (const l of d.layers) if (gone.has(l.showOnFocus)) l.showOnFocus = null;
    });
    setSel([]);
  };
  const copy = () => {
    if (!selLayers.length) return;
    const ids = new Set(); for (const l of selLayers) for (const x of descendants(doc, l.id)) ids.add(x);
    const layers = doc.layers.filter(l => ids.has(l.id));
    const tracks = {};
    for (const [c, v] of Object.entries(doc.clips)) for (const id of ids) if (v.tracks[id]) (tracks[c] ??= {})[id] = v.tracks[id];
    clipboard = clone({ layers, tracks, roots: selLayers.map(l => l.id) });
    toast(`${selLayers.length} capa(s) copiadas`);
  };
  const paste = (offset = 20) => {
    if (!clipboard) return;
    const map = {}; for (const l of clipboard.layers) map[l.id] = uid(l.type[0]);
    const fresh = clipboard.layers.map(l => ({ ...clone(l), id: map[l.id], parent: l.parent && map[l.parent] ? map[l.parent] : (clipboard.roots.includes(l.id) ? null : l.parent) }));
    edit(d => {
      let o = nextOrder(d);
      for (const l of fresh) {
        if (!clipboard.layers.find(q => map[q.id] === l.id && q.parent && map[q.parent])) { l.parent = null; l.base.order = o++; if (mode !== '3d') { l.base.x += offset; l.base.y += offset; } }
        if (l.showOnFocus && map[l.showOnFocus]) l.showOnFocus = map[l.showOnFocus];
        d.layers.push(l);
      }
      for (const [c, tr] of Object.entries(clipboard.tracks)) for (const [id, v] of Object.entries(tr)) if (d.clips[c]) d.clips[c].tracks[map[id]] = clone(v);
    });
    setSel(clipboard.roots.map(id => map[id]));
  };
  const duplicate = () => { const keep = clipboard; copy(); paste(16); clipboard = keep; };
  const group = () => {
    const ls = selLayers.filter(l => l.type !== 'camera');
    if (!ls.length) return;
    const g = newLayer(mode, 'group', { name: 'Grupo' });
    if (mode !== '3d') { g.base.x = 0; g.base.y = 0; }
    g.parent = ls[0].parent || null;
    edit(d => {
      g.base.order = Math.max(...ls.map(l => d.layers.find(q => q.id === l.id).base.order || 0));
      d.layers.push(g);
      moveLayers(d, ls.map(l => l.id), g.id, 'inside', mode);
    });
    setSel([g.id]);
  };
  const ungroup = () => {
    const gs = selLayers.filter(l => l.type === 'group');
    if (!gs.length) return;
    edit(d => {
      for (const g0 of gs) {
        const g = d.layers.find(l => l.id === g0.id);
        const kids = d.layers.filter(l => l.parent === g.id);
        for (const k of kids) { k.parent = g.parent || null; if (mode !== '3d') { k.base.x += g.base.x; k.base.y += g.base.y; } k.base.order = (g.base.order || 0) + (k.base.order || 0) / 1000; }
        d.layers = d.layers.filter(l => l.id !== g.id);
        for (const c of Object.values(d.clips)) delete c.tracks[g.id];
      }
    });
    setSel([]);
  };
  const arrange = how => {
    if (!selLayers.length) return;
    edit(d => {
      for (const l0 of selLayers) {
        const l = d.layers.find(q => q.id === l0.id);
        const sibs = d.layers.filter(q => (q.parent || null) === (l.parent || null)).sort((a, b) => (a.base.order || 0) - (b.base.order || 0));
        const i = sibs.indexOf(l);
        sibs.splice(i, 1);
        const j = how === 'front' ? sibs.length : how === 'back' ? 0 : how === 'up' ? Math.min(sibs.length, i + 1) : Math.max(0, i - 1);
        sibs.splice(j, 0, l);
        sibs.forEach((q, k) => { q.base.order = k + 1; });
      }
    });
  };
  const pickGame = im => {
    const image = { pak: im.pak, key: im.key, name: im.name || im.key, w: im.w, h: im.h };
    const target = selLayers.length === 1 && selLayers[0].type === 'image' ? selLayers[0] : null;
    if (target) edit(d => { const l = d.layers.find(x => x.id === target.id); l.image = image; l.base.w = im.w; l.base.h = im.h; });
    else addLayer('image', { name: im.name || 'Imagen', image, base: { w: im.w, h: im.h } });
  };
  const pickAsset = a => {
    let w = a.w || 256, h = a.h || 256;
    const f = Math.min(1, 900 / w, 600 / h); w = Math.round(w * f); h = Math.round(h * f);
    const image = { custom: true, file: a.file, name: a.file, svg: a.kind === 'svg', w: a.w, h: a.h };
    const target = selLayers.length === 1 && selLayers[0].type === 'image' ? selLayers[0] : null;
    if (target) edit(d => { const l = d.layers.find(x => x.id === target.id); l.image = image; l.base.w = w; l.base.h = h; });
    else addLayer('image', { name: a.file.replace(/\.[a-z0-9]+$/, ''), image, base: { w, h } });
  };
  // archivos soltados en cualquier parte del editor: imagenes a la pantalla, fuentes al proyecto
  // Photoshop / Illustrator / PDF: cada capa del archivo pasa a ser una capa de imagen de la pantalla, en su sitio
  // (el documento se encaja en 1280x720), con sus grupos, opacidad y visibilidad. Una pantalla por archivo.
  const LAYERED = /\.(psd|psb|ai|pdf)$/i;
  const importLayered = async (file, asNewScreen = false) => {
    if (mode === '3d') { toast('Importar PSD o AI es para interfaces 2D y 2.5D', 'warn'); return; }
    setBusy(`Importando ${file.name}...`);
    try {
      const r = await api('/api/ui/import_layered', { pid: projRef.current.id, filename: file.name, data: await fileToBase64(file) });
      // fuentes de los textos: al proyecto (y cargadas antes de medir los textos)
      const fonts = [...new Map(r.layers.filter(L => L.font).map(L => [L.font.file, L.font])).values()];
      for (const f of fonts) { try { await registerFont(f.family, assetUrl(projRef.current, f.file)); } catch { /* se ve con otra */ } }
      if (fonts.length) editProj(p => { p.fonts = [...(p.fonts || []).filter(x => !fonts.some(f => f.file === x.file)), ...fonts]; });
      const k = Math.min(W / r.w, H / r.h), ox = (W - r.w * k) / 2, oy = (H - r.h * k) / 2;
      const fill = d => {
        const groups = {};
        const groupOf = path => {
          let parent = null;
          path.forEach((name, i) => {
            const key = path.slice(0, i + 1).join('/');
            if (!groups[key]) {
              const g = newLayer(mode, 'group', { name, parent, base: { x: 0, y: 0 } });
              g.base.order = nextOrder(d, parent);
              d.layers.push(g);
              groups[key] = g.id;
            }
            parent = groups[key];
          });
          return parent;
        };
        for (const L of r.layers) {
          const parent = groupOf(L.group || []);
          if (L.kind === 'text') {   // texto de Photoshop: texto editable con su fuente
            const px = Math.max(4, L.size * k);
            const t = newLayer(mode, 'text', { name: L.name, parent, visible: L.visible, blend: L.blend || 'normal',
              text: { ...DEFAULT_TEXT, value: L.text, font: L.font?.family || DEFAULT_TEXT.font, weight: L.bold ? 700 : 400, italic: !!L.italic,
                px, color: L.color, align: L.align, spacing: (L.spacing || 0) * px, line: L.line || 1.15, box: 'auto' },
              base: { x: ox + L.cx * k, y: oy + L.cy * k, rot: L.rot || 0, alpha: L.opacity ?? 1 } });
            for (const [kk, v] of Object.entries(L.fx || {})) t.fx[kk] = { ...t.fx[kk], ...v, ...(v.width ? { width: v.width * k } : {}),
              ...(v.size ? { size: v.size * k } : {}), ...(v.blur ? { blur: v.blur * k } : {}), ...(v.dx != null ? { dx: v.dx * k, dy: v.dy * k } : {}) };
            fitText(t);
            t.base.order = nextOrder(d, parent);
            d.layers.push(t);
            continue;
          }
          const l = newLayer(mode, 'image', { blend: L.blend || 'normal', name: L.name, parent, visible: L.visible,
            image: { custom: true, file: L.file, name: L.file, svg: false, w: L.w, h: L.h },
            base: { x: ox + (L.x + L.w / 2) * k, y: oy + (L.y + L.h / 2) * k, w: L.w * k, h: L.h * k, alpha: L.opacity } });
          l.base.order = nextOrder(d, parent);
          d.layers.push(l);
        }
      };
      if (asNewScreen) {
        const s = newScreen(mode, r.name || file.name);
        fill(s);
        editProj(p => { p.screens.push(s); });
        setSid(s.id);
      } else edit(fill);
      toast(`${r.layers.length} capa${r.layers.length !== 1 ? 's' : ''} de ${file.name}` + (r.warnings?.length ? ` (${r.warnings.length} aviso${r.warnings.length > 1 ? 's' : ''})` : ''));
      for (const w of (r.warnings || []).slice(0, 3)) toast(w, 'warn');
    } catch (e) { toast(e.message, 'err'); } finally { setBusy(null); }
  };
  const pickLayered = asNewScreen => {
    const inp = document.createElement('input');
    inp.type = 'file'; inp.accept = '.psd,.psb,.ai,.pdf';
    inp.onchange = () => { const f = inp.files[0]; if (f) importLayered(f, asNewScreen); };
    inp.click();
  };
  const dropFiles = async files => {
    if (!projRef.current || !files.length) return;
    const lay = files.filter(f => LAYERED.test(f.name));
    if (lay.length) { for (const f of lay) await importLayered(f, false); files = files.filter(f => !LAYERED.test(f.name)); if (!files.length) return; }
    setBusy('Importando...');
    try {
      const r = await uploadFiles(projRef.current, files, editProj, toast);
      const imgs = r.filter(a => a.kind !== 'font'), fnts = r.filter(a => a.kind === 'font');
      imgs.forEach((a, i) => {
        let w = a.w || 256, h = a.h || 256;
        const f = Math.min(1, 900 / w, 600 / h); w = Math.round(w * f); h = Math.round(h * f);
        const image = { custom: true, file: a.file, name: a.file, svg: a.kind === 'svg', w: a.w, h: a.h };
        addLayer('image', { name: a.file.replace(/\.[a-z0-9]+$/, ''), image, base: { w, h } }, mode === '3d' ? null : [640 + i * 24, 360 + i * 24]);
      });
      if (fnts.length && selLayers.some(l => l.type === 'text')) pickFont(fnts[0].family);
      if (r.length) toast([imgs.length && `${imgs.length} imagen${imgs.length > 1 ? 'es' : ''} en la pantalla`,
        fnts.length && `${fnts.length} fuente${fnts.length > 1 ? 's' : ''} en el proyecto (${fnts.map(f => f.family).join(', ')})`].filter(Boolean).join(', '));
    } finally { setBusy(null); }
  };
  const dropOver = useGlobalDrop(files => { if (dialog !== 'res') dropFiles(files); });
  const pickFont = family => {
    const texts = selLayers.filter(l => l.type === 'text');
    if (texts.length) edit(d => { for (const t0 of texts) { const l = d.layers.find(x => x.id === t0.id); l.text.font = family; fitText(l); } });
    setOpts(o => { o.text.font = family; });
    toast(`Fuente ${family}${texts.length ? ' aplicada' : ' para los textos nuevos'}`);
  };
  const create3d = k => {
    if (k === 'image') { setTopTab('assets'); return toast('Elige una imagen en Recursos', 'warn'); }
    if (k === 'group' || k === 'camera' || k === 'text') return addLayer(k);
    const sh = clone(opts.shape); sh.kind = k;
    addLayer('shape', { name: { rect: 'Rectangulo', ellipse: 'Elipse', polygon: 'Poligono', star: 'Estrella' }[k], shape: sh });
  };

  // ---------------------------------------------------------------- pantallas
  // indicaciones de botones (verde continuar, rojo volver...) y entrada / salida automaticas
  const addHints = () => {
    if (mode === '3d') { toast('Las indicaciones son para pantallas 2D y 2.5D', 'warn'); return; }
    const font = proj.fonts?.[0]?.family || DEFAULT_TEXT.font;
    const ls = hintLayers(mode, defaultHints(doc), { font, measure: (v, px) => textSize({ ...DEFAULT_TEXT, font, value: v, px })[0] });
    edit(d => {
      const old = d.layers.find(l => l.name === 'indicaciones de botones' && !l.parent);
      if (old) { const gone = descendants(d, old.id); d.layers = d.layers.filter(l => !gone.has(l.id)); }
      ls[0].base.order = nextOrder(d);
      d.layers.push(...ls);
    });
    setSel([ls[0].id]);
    toast('Indicaciones anadidas: cambia sus textos o colores en sus capas');
  };
  const addTransitions = () => {
    if (mode === '3d') { toast('Solo para pantallas 2D y 2.5D', 'warn'); return; }
    const has = ['intro', 'outro'].some(c => Object.keys(doc.clips[c]?.tracks || {}).length);
    edit(d => { const t = autoTransitions(d); d.clips.intro = { ...d.clips.intro, ...t.intro }; d.clips.outro = { ...d.clips.outro, ...t.outro }; });
    toast(has ? 'Entrada y salida rehechas (se reemplazaron las que habia)' : 'Entrada y salida creadas: ajustalas en la linea de tiempo');
  };
  const addScreen = () => { const s = newScreen(mode, `Pantalla ${proj.screens.length + 1}`); editProj(p => { p.screens.push(s); }); setSid(s.id); };
  const dupScreen = () => {
    const s = clone(doc); s.id = uid('s'); s.name = doc.name + ' copia';
    const map = {}; for (const l of s.layers) map[l.id] = uid(l.type[0]);
    s.layers = s.layers.map(l => ({ ...l, id: map[l.id], parent: l.parent ? map[l.parent] : null, showOnFocus: l.showOnFocus ? map[l.showOnFocus] : null }));
    for (const c of Object.values(s.clips)) { c.tracks = Object.fromEntries(Object.entries(c.tracks).map(([k, v]) => [map[k] || k, v])); if (c.cuts) c.cuts = c.cuts.map(x => ({ ...x, cam: map[x.cam] || x.cam })); }
    if (s.activeCam) s.activeCam = map[s.activeCam]; if (s.focusStart) s.focusStart = map[s.focusStart];
    editProj(p => { p.screens.splice(p.screens.findIndex(x => x.id === doc.id) + 1, 0, s); });
    setSid(s.id);
  };
  const removeScreen = () => {
    if (proj.screens.length < 2) return;
    const gone = doc.id, next = proj.screens.find(s => s.id !== gone).id;
    editProj(p => {
      p.screens = p.screens.filter(s => s.id !== gone);
      if (p.start === gone) p.start = p.screens[0].id;
      for (const s of p.screens) for (const l of s.layers) if (l.ui?.actions) l.ui.actions = l.ui.actions.filter(a => !(a.type === 'goto' && a.screen === gone));
    });
    setSid(next);
  };

  // ---------------------------------------------------------------- vista
  const stageSize = () => { const el = document.querySelector('.ue-view'); const r = el?.getBoundingClientRect(); return r ? { w: r.width, h: r.height } : { w: 800, h: 500 }; };
  const zoomTo = how => {
    const s = stageSize();
    if (how === 'fit') return setView(fitView(s));
    const nz = typeof how === 'number' ? how : Math.max(0.05, Math.min(32, view.z * (how === 'in' ? 1.25 : 0.8)));
    const cx = s.w / 2, cy = s.h / 2, wx = (cx - view.x) / view.z, wy = (cy - view.y) / view.z;
    setView({ z: nz, x: cx - wx * nz, y: cy - wy * nz, fit: false });
  };

  // ---------------------------------------------------------------- teclado
  useEffect(() => {
    const onKey = e => {
      if (dialog === 'res' && e.key === 'Escape' && !/INPUT/.test(e.target.tagName)) { setDialog(null); return; }
      if (proj && !dialog && !preview && (e.ctrlKey || e.metaKey) && e.shiftKey && e.key.toLowerCase() === 'r') { e.preventDefault(); setDialog('res'); return; }
      if (!proj || dialog || preview || /INPUT|SELECT|TEXTAREA/.test(e.target.tagName)) return;
      const ctrl = e.ctrlKey || e.metaKey, k = e.key.toLowerCase();
      const run = fn => { e.preventDefault(); fn(); };
      if (ctrl) {
        if (k === 'z' && !e.shiftKey) return run(() => { const q = hist.current.undo(proj); if (q) setProj(q); });
        if (k === 'y' || (k === 'z' && e.shiftKey)) return run(() => { const q = hist.current.redo(proj); if (q) setProj(q); });
        if (k === 's') return run(save);
        if (k === 'c') return run(copy);
        if (k === 'x') return run(() => { copy(); removeLayers(sel); });
        if (k === 'v') return run(() => paste());
        if (k === 'j') return run(duplicate);
        if (k === 'a') return run(() => setSel(doc.layers.filter(l => !l.locked && l.visible !== false && !l.parent).map(l => l.id)));
        if (k === 'd') return run(() => setSel([]));
        if (k === 'g') return run(e.shiftKey ? ungroup : group);
        if (k === '0') return run(() => zoomTo('fit'));
        if (k === '1') return run(() => zoomTo(1));
        if (k === '+' || k === '=') return run(() => zoomTo('in'));
        if (k === '-') return run(() => zoomTo('out'));
        if (e.key === ']') return run(() => arrange(e.shiftKey ? 'front' : 'up'));
        if (e.key === '[') return run(() => arrange(e.shiftKey ? 'back' : 'down'));
        return;
      }
      if (e.key === ' ' && clip !== 'base' && mode === '3d') return run(() => setPlaying(v => !v));
      if (k === 'enter' && clip !== 'base') return run(() => setPlaying(v => !v));
      if (e.key === 'F5') return run(() => setPreview(true));
      if (e.key === 'Escape') { setSel([]); setTool('move'); return; }
      if (e.key === 'Delete' || e.key === 'Backspace') {
        e.preventDefault();
        if (selCut != null) { edit(d => { d.clips[clip].cuts.splice(selCut, 1); }); setSelCut(null); }
        else if (selKey) { edit(d => { const tr = d.clips[clip].tracks[selKey.lid]?.[selKey.prop]; if (tr) d.clips[clip].tracks[selKey.lid][selKey.prop] = tr.filter(x => Math.abs(x.t - selKey.t) > 1e-6); }); setSelKey(null); }
        else removeLayers(sel);
        return;
      }
      if (mode !== '3d' && TOOL_KEYS[k] && !e.altKey) return run(() => setTool(TOOL_KEYS[k]));
      if (selLayers.length && mode !== '3d' && e.key.startsWith('Arrow')) {
        e.preventDefault();
        const dd = e.shiftKey ? 10 : 1;
        const [dx, dy] = { ArrowLeft: [-dd, 0], ArrowRight: [dd, 0], ArrowUp: [0, -dd], ArrowDown: [0, dd] }[e.key];
        edit(d => { for (const l0 of selLayers) { const L = d.layers.find(q => q.id === l0.id); if (!L || L.locked) continue; const s = stateAt(d, L, clip, t);
          if (dx) setProp(d, clip, L, 'x', s.x + dx, t); if (dy) setProp(d, clip, L, 'y', s.y + dy, t); } });
      }
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  });

  if (!proj) return <div className="st-app st-empty-app">{dialog && <StartDialog openFile={openFile} openDisk={openDisk} create={load} toast={toast} />}{msg && <Toast msg={msg} />}</div>;

  // ---------------------------------------------------------------- linea de tiempo
  const rows = [
    ...(mode === '25d' ? [{ id: CAM25, name: 'Camara 2.5D', icon: 'cam', props: propsFor('25d', CAM25), tracks: clipOf(doc, clip)?.tracks?.[CAM25] }] : []),
    ...doc.layers.filter(l => l.visible !== false).sort((a, b) => (b.base.order || 0) - (a.base.order || 0)).map(l => ({ id: l.id, name: l.name, icon: TYPE_ICON[l.type],
      props: [...propsFor(mode, l.type), ...LIVE_PROPS.filter(([k]) => clipOf(doc, clip)?.tracks?.[l.id]?.[k])], tracks: clipOf(doc, clip)?.tracks?.[l.id] })),
  ];
  const cams = doc.layers.filter(l => l.type === 'camera');
  const lanes = mode === '3d' && clip !== 'base' ? [{
    id: 'cuts', name: 'Cortes de camara', icon: 'cam', sel: selCut,
    marks: (doc.clips[clip].cuts || []).map(c => ({ t: c.t, label: cams.find(x => x.id === c.cam)?.name || '?' })),
    onAdd: at => {
      const cam = selLayers[0]?.type === 'camera' ? selLayers[0].id : activeCamAt(doc, clip, at)?.id;
      if (!cam) return toast('Anade una camara primero.', 'warn');
      edit(d => { d.clips[clip].cuts.push({ t: at, cam }); });
      setSelCut(doc.clips[clip].cuts.length);
    },
    onMove: (i, to, first) => edit(d => { d.clips[clip].cuts[i].t = to; }, first),
    onPick: i => { setSelCut(i); setSelKey(null); },
  }] : [];

  // ---------------------------------------------------------------- menus
  const n = clean(proj.name) || 'interfaz';
  // Script del proyecto ('p'), de una pantalla ('s:<id>') o de una capa ('l:<id>'): abre la ventana Logica en el
  const openScript = (key, tab = 'code') => { setLogicAt({ tab, key }); setDialog('logic'); };
  // "Volver al panel" desde las propiedades de una capa
  const dropScript = id => editProj(p => {
    for (const s of p.screens) for (const l of s.layers) if (l.id === id && l.script) {
      restorePanel(l);
      delete l.script;
    }
  });
  const menus = [
    { label: 'Archivo', items: [
      { label: 'Nueva interfaz...', icon: 'file', act: () => setDialog('new') },
      { label: 'Abrir de la biblioteca...', icon: 'folder', act: () => setDialog('new') },
      { label: 'Abrir archivo .desp...', act: openDisk },
      { sep: 1 },
      { label: 'Guardar', icon: 'save', kbd: 'Ctrl+S', act: save },
      { label: 'Guardar una copia...', icon: 'copy', act: saveCopy },
      { sep: 1 },
      { label: 'Recursos...', icon: 'tex', kbd: 'Ctrl+Mayus+R', act: () => setDialog('res') },
      { label: 'Importar imagen, SVG o fuente...', icon: 'upload', act: () => { setDialog('res'); setTimeout(() => document.querySelector('.ue-reswin .ue-upload input')?.click(), 80); } },
      { label: 'Imagen de referencia (no se exporta)...', icon: 'tex', act: () => document.getElementById('ue-ref-input')?.click() },
      ref && { label: 'Quitar la referencia', act: () => setRef(null) },
      { sep: 1 },
      { label: 'Exportar al juego', icon: 'build', act: doExport },
      { label: 'Probar en el juego', icon: 'play', act: () => test('show') },
      { label: 'Salida en el juego', icon: 'stop', act: () => test('hide') },
      { label: 'Ver lo que se genera...', icon: 'code', act: showQb },
      { sep: 1 },
      { label: 'Volver al inicio de DE Studio', act: () => { location.href = 'index.html'; } },
    ] },
    { label: 'Editar', items: [
      { label: 'Deshacer', icon: 'undo', kbd: 'Ctrl+Z', act: () => { const q = hist.current.undo(proj); if (q) setProj(q); } },
      { label: 'Rehacer', icon: 'redo', kbd: 'Ctrl+Y', act: () => { const q = hist.current.redo(proj); if (q) setProj(q); } },
      { sep: 1 },
      { label: 'Cortar', kbd: 'Ctrl+X', disabled: !sel.length, act: () => { copy(); removeLayers(sel); } },
      { label: 'Copiar', icon: 'copy', kbd: 'Ctrl+C', disabled: !sel.length, act: copy },
      { label: 'Pegar', icon: 'paste', kbd: 'Ctrl+V', disabled: !clipboard, act: () => paste() },
      { label: 'Duplicar', icon: 'duplicate', kbd: 'Ctrl+J', disabled: !sel.length, act: duplicate },
      { label: 'Borrar', icon: 'trash', kbd: 'Supr', disabled: !sel.length, act: () => removeLayers(sel) },
      { sep: 1 },
      { label: 'Seleccionar todo', kbd: 'Ctrl+A', act: () => setSel(doc.layers.filter(l => !l.locked && !l.parent).map(l => l.id)) },
      { label: 'Deseleccionar', kbd: 'Ctrl+D', act: () => setSel([]) },
    ] },
    { label: 'Capa', items: [
      { label: 'Nueva', icon: 'plus', sub: [
        { label: 'Texto', icon: 'text', act: () => addLayer('text') },
        { label: 'Rectangulo', icon: 'rect', act: () => create3d('rect') },
        { label: 'Elipse', icon: 'ellipse', act: () => create3d('ellipse') },
        { label: 'Poligono', icon: 'polygon', act: () => create3d('polygon') },
        { label: 'Estrella', icon: 'star', act: () => create3d('star') },
        { label: 'Grupo', icon: 'folder', act: () => addLayer('group') },
        mode === '3d' && { label: 'Camara', icon: 'cam', act: () => addLayer('camera') },
      ] },
      { sep: 1 },
      { label: 'Agrupar', icon: 'group', kbd: 'Ctrl+G', disabled: !sel.length, act: group },
      { label: 'Desagrupar', icon: 'ungroup', kbd: 'Ctrl+Mayus+G', disabled: !selLayers.some(l => l.type === 'group'), act: ungroup },
      { sep: 1 },
      { label: 'Traer al frente', icon: 'tofront', kbd: 'Ctrl+Mayus+]', disabled: !sel.length, act: () => arrange('front') },
      { label: 'Traer adelante', kbd: 'Ctrl+]', disabled: !sel.length, act: () => arrange('up') },
      { label: 'Enviar atras', kbd: 'Ctrl+[', disabled: !sel.length, act: () => arrange('down') },
      { label: 'Enviar al fondo', icon: 'back', kbd: 'Ctrl+Mayus+[', disabled: !sel.length, act: () => arrange('back') },
      { sep: 1 },
      { label: selLayers.every(l => l.ui?.focus) && selLayers.length ? 'Dejar de ser boton' : 'Convertir en boton', icon: 'target', disabled: !selLayers.length,
        act: () => { const v = !selLayers.every(l => l.ui?.focus); edit(d => { for (const l of selLayers) { const L = d.layers.find(q => q.id === l.id); if (L.ui && L.type !== 'group' && L.type !== 'camera') L.ui.focus = v; } }); setTopTab('props'); } },
      { label: 'Bloquear o desbloquear', icon: 'lock', disabled: !sel.length, act: () => edit(d => { for (const l of selLayers) { const L = d.layers.find(q => q.id === l.id); L.locked = !L.locked; } }) },
      { label: 'Ocultar o mostrar', icon: 'eye', disabled: !sel.length, act: () => edit(d => { for (const l of selLayers) { const L = d.layers.find(q => q.id === l.id); L.visible = L.visible === false; } }) },
    ] },
    { label: 'Pantalla', items: [
      { label: 'Nueva pantalla', icon: 'plus', act: addScreen },
      { label: 'Duplicar esta pantalla', icon: 'duplicate', act: dupScreen },
      { sep: 1 },
      { label: 'Importar PSD o AI en esta pantalla', icon: 'upload', act: () => pickLayered(false) },
      { label: 'Pantalla nueva desde PSD o AI', icon: 'screens', act: () => pickLayered(true) },
      { sep: 1 },
      { label: 'Anadir indicaciones de botones', icon: 'plus', act: addHints },
      { label: 'Animaciones de entrada y salida automaticas', icon: 'play', act: addTransitions },
      { sep: 1 },
      { label: 'Usar como inicial', icon: 'play', disabled: proj.start === doc.id, act: () => editProj(p => { p.start = doc.id; }) },
      { label: 'Borrar esta pantalla', icon: 'trash', disabled: proj.screens.length < 2, act: removeScreen },
      { sep: 1 },
      ...proj.screens.map(s => ({ label: s.name + (proj.start === s.id ? '  (inicial)' : ''), checked: s.id === doc.id, act: () => setSid(s.id) })),
    ] },
    { label: 'Ver', items: [
      { label: 'Vista previa interactiva', icon: 'play', kbd: 'F5', act: () => setPreview(true) },
      { label: 'Logica (Python y reglas)', icon: 'code', act: () => openScript('p') },
      { sep: 1 },
      { label: 'Acercar', icon: 'zoomin', kbd: 'Ctrl++', act: () => zoomTo('in') },
      { label: 'Alejar', icon: 'zoomout', kbd: 'Ctrl+-', act: () => zoomTo('out') },
      { label: 'Encajar en la ventana', icon: 'fit', kbd: 'Ctrl+0', act: () => zoomTo('fit') },
      { label: 'Tamano real (100%)', kbd: 'Ctrl+1', act: () => zoomTo(1) },
      { sep: 1 },
      { label: 'Ajuste magnetico', checked: opts.snap, act: () => setOpts(o => { o.snap = !o.snap; }) },
      { label: 'Ajustar a otras capas', checked: opts.snapLayers, act: () => setOpts(o => { o.snapLayers = !o.snapLayers; }) },
      { label: 'Guias', checked: opts.guides, act: () => setOpts(o => { o.guides = !o.guides; }) },
      { label: 'Borrar todas las guias', act: () => editProj(p => { p.guides = { x: [], y: [] }; }) },
      { label: 'Cuadricula', checked: opts.grid, act: () => setOpts(o => { o.grid = !o.grid; }) },
      { label: 'Ver lo que sale del lienzo', checked: opts.outside !== false, act: () => setOpts(o => { o.outside = o.outside === false; }) },
      { label: 'Marcar los botones', checked: opts.showButtons, act: () => setOpts(o => { o.showButtons = !o.showButtons; }) },
      { label: 'Fondo', sub: [['dark', 'Oscuro'], ['black', 'Negro'], ['checker', 'Cuadros (transparencia)'], ['light', 'Claro']].map(([k, v]) =>
        ({ label: v, checked: opts.bg === k, act: () => setOpts(o => { o.bg = k; }) })) },
      { label: 'Linea de tiempo', checked: showTl, act: () => setShowTl(v => !v) },
    ] },
    { label: 'Ayuda', items: [
      { label: 'Atajos de teclado', icon: 'info', act: () => setDialog('keys') },
      { label: 'Como se usa en el juego', icon: 'code', act: () => setDialog('game') },
    ] },
  ];

  return (
    <div className={'st-app ue-app mode-' + mode + (showTl ? '' : ' no-tl')}>
      <MenuBar menus={menus} right={<>
        <label className="ue-name" title="Nombre de la interfaz: en el juego, re_ui_<nombre>_show y _hide">
          <input value={proj.name} onChange={e => editProj(p => { p.name = e.target.value; }, false)} onBlur={e => editProj(p => { p.name = clean(e.target.value) || 'mi_interfaz'; }, false)} />
        </label>
        <button onClick={() => openScript('p')} title="Logica de la interfaz: codigo en Python, reglas por bloques y referencia"><Icon n="code" />Logica</button>
        <button onClick={() => setPreview(true)} title="Vista previa interactiva (F5): flechas, Intro y Esc, o el raton"><Icon n="play" />Vista previa</button>
        <button onClick={doExport} title="Escribe la interfaz en la carpeta del juego"><Icon n="build" />Exportar</button>
        <button className="primary" onClick={() => test('show')} title="Exporta y la abre en el juego que tienes abierto"><Icon n="gamepad" />Probar en el juego</button>
      </>} />
      <OptionsBar proj={proj} mode={mode} tool={tool} opts={opts} setOpts={setOpts} selLayers={selLayers}
        editSel={fn => edit(d => { for (const l of selLayers) { const L = d.layers.find(q => q.id === l.id); fn(L); fitText(L); } })}
        doc={doc} clip={clip} t={t} edit={edit} view={view} zoomTo={zoomTo} />
      <ToolBar mode={mode} tool={tool} setTool={setTool} onCreate3d={create3d} fill={opts.shape.fill.color} stroke={opts.shape.stroke.color} setOpts={setOpts} />
      <main className="ue-center">
        <div className="ue-doctabs">
          {proj.screens.map(s => <button key={s.id} className={s.id === doc.id ? 'on' : ''} onClick={() => setSid(s.id)} title={s.name}>
            {proj.start === s.id && <Icon n="play" s={11} />}{s.name}</button>)}
          <button className="st-icon" onClick={addScreen} title="Pantalla nueva"><Icon n="plus" s={14} /></button>
        </div>
        <div className={'ue-split' + (animSide && showTl ? ' side' : '')}>
        {mode === '3d'
          ? <Stage3D key={doc.id} proj={proj} doc={doc} clip={clip} t={t} sel={sel} setSel={setSel} live={live} />
          : <Stage2D proj={proj} doc={doc} mode={mode} clip={clip} t={t} sel={sel} setSel={setSel} tool={tool} setTool={setTool}
            edit={edit} live={live} opts={opts} view={view} setView={setView} refImg={ref} refAlpha={refAlpha}
            editingText={editingText} setEditingText={setEditingText} />}
        {showTl && <Timeline clips={[...CLIPS, ...animClips(doc).map(([k, c]) => [k, c.name, `Animacion propia: ${`play_anim(${JSON.stringify(c.name)})`} en el codigo`])]}
          side={animSide} rowMenu={rowMenu}
          extra={<>
            <button className={'st-icon' + (animSide ? ' on' : '')} title={animSide ? 'Linea de tiempo abajo' : 'Vista de animacion: la interfaz a la izquierda y las claves a la derecha'}
              onClick={() => setAnimSide(v => !v)}><Icon n="split" /></button>
            <button className="st-icon" title="Nueva animacion con nombre (se lanza desde el codigo, al pulsar un boton, una tecla...)" onClick={addAnim}><Icon n="plus" /></button>
            {curAnim && <div className="st-tlanim">
              <input value={curAnim.name} title="Nombre de la animacion (el que se usa en el codigo)" onChange={e => { const v = e.target.value; edit(d => { d.clips[clip].name = v; }); }} />
              <code title="Copiar" onClick={() => { navigator.clipboard?.writeText(animCall).catch(() => {}); toast('Copiado: ' + animCall); }}>{animCall}</code>
              <button className="st-icon" title="Borrar esta animacion" onClick={delAnim}><Icon n="trash" /></button>
            </div>}
          </>}
          clip={clip} setClip={setClip} dur={dur} setDur={v => edit(d => { d.clips[clip].duration = v; })}
          t={t} setT={setT} playing={playing} setPlaying={setPlaying} rows={rows} lanes={lanes}
          sel={sel[0]} setSel={id => setSel(id ? [id] : [])} selKey={selKey} setSelKey={k => { setSelKey(k); setSelCut(null); }}
          onMoveKeys={(id, props, from, to, first) => edit(d => moveKeys(d.clips[clip], id, props, from, to), first)}
          onEase={(id, prop, kt, ease, scope) => edit(d => {
            const tr = d.clips[clip]?.tracks?.[id];
            if (!tr) return;
            const ks = scope === 'row' ? Object.values(tr).flat() : scope === 'track' ? tr[prop] || [] : (tr[prop] || []).filter(k => Math.abs(k.t - kt) < 1e-3);
            for (const k of ks) k.ease = ease;
          })}
          onDeleteKey={(id, prop, kt) => edit(d => { const tr = d.clips[clip]?.tracks?.[id]; if (tr?.[prop]) tr[prop] = tr[prop].filter(k => Math.abs(k.t - kt) > 1e-3); })}
          baseText={`Base: la pantalla "${doc.name}" sin animar. Elige Entrada, Bucle o Salida para animarla con claves (cada pantalla tiene las suyas).`} />}
              </div>
      </main>
      <aside className="ue-dock">
        <div className="ue-panel top">
          <div className="ue-tabs">
            <button className={topTab === 'props' ? 'on' : ''} onClick={() => setTopTab('props')}><Icon n="sliders" s={14} />Propiedades</button>
            <button className={topTab === 'assets' ? 'on' : ''} onClick={() => setTopTab('assets')}><Icon n="tex" s={14} />Recursos</button>
          </div>
          <div className="ue-panelbody">
            {topTab === 'props' && <PropsPanel proj={proj} doc={doc} mode={mode} sel={sel} clip={clip} t={t} edit={edit} editProj={editProj} onScript={openScript} onDropScript={dropScript} onImport={pickLayered}
              selKey={selKey} setSelKey={setSelKey} selCut={selCut} setSelCut={setSelCut} setSid={setSid} setTool={setTool} />}
            {topTab === 'assets' && <AssetsPanel proj={proj} editProj={editProj} onPickGame={pickGame} onPickAsset={pickAsset} onPickFont={pickFont} toast={toast}
              onOpenWindow={() => setDialog('res')} />}
          </div>
        </div>
        <div className="ue-panel bottom">
          <div className="ue-tabs">
            <button className={botTab === 'layers' ? 'on' : ''} onClick={() => setBotTab('layers')}><Icon n="layers" s={14} />Capas</button>
            <button className={botTab === 'screens' ? 'on' : ''} onClick={() => setBotTab('screens')}><Icon n="screens" s={14} />Pantallas<span className="ue-count">{proj.screens.length}</span></button>
          </div>
          <div className="ue-panelbody">
            {botTab === 'layers' && <LayersPanel proj={proj} doc={doc} mode={mode} sel={sel} setSel={setSel} edit={edit}
              onAdd={type => type === 'group' && sel.length ? group() : addLayer(type)} onRemove={removeLayers} />}
            {botTab === 'screens' && <ScreensPanel proj={proj} sid={doc.id} setSid={setSid} editProj={editProj} addScreen={addScreen} dupScreen={dupScreen} removeScreen={removeScreen} onImport={pickLayered} />}
          </div>
        </div>
      </aside>
      <input id="ue-ref-input" type="file" accept="image/*" hidden onChange={e => { const f = e.target.files[0]; e.target.value = ''; if (f) setRef(URL.createObjectURL(f)); }} />
      {msg && <Toast msg={msg} />}
      {busy && <div className="ue-busy"><span className="spinner" />{busy}</div>}
      {qb && <QbView qb={qb} close={() => setQb(null)} />}
      {preview && <Preview proj={proj} close={() => setPreview(false)} />}
      {dialog === 'logic' && <LogicWindow proj={proj} editProj={editProj} start={logicAt} close={() => { setDialog(null); setLogicAt(null); }} />}
      {dialog === 'res' && <ResourcesWindow proj={proj} editProj={editProj} onPickGame={pickGame} onPickAsset={pickAsset} onPickFont={pickFont}
        toast={toast} close={() => setDialog(null)} />}
      {dropOver && dialog !== 'res' && <div className="ue-globaldrop"><div><Icon n="upload" s={40} /><b>Suelta para importar</b>
        <span>Las imagenes y SVG se ponen en la pantalla; las fuentes se anaden al proyecto.</span></div></div>}
      {dialog === 'new' && <StartDialog openFile={openFile} openDisk={openDisk} create={load} toast={toast} close={() => setDialog(null)} />}
      {dialog === 'keys' && <InfoDialog title="Atajos de teclado" close={() => setDialog(null)}>
        <table className="ue-keys"><tbody>{[
          ['V / H / Z', 'Mover, mano, zoom'], ['T', 'Texto (clic: punto, arrastrar: parrafo; doble clic en un texto para editarlo)'],
          ['U / E / Y / K / L', 'Rectangulo, elipse, poligono, estrella, linea (Mayus: proporcional, Alt: desde el centro)'],
          ['P', 'Pluma (clic: esquina, arrastrar: curva, clic en el primer punto: cerrar, Intro: terminar)'],
          ['Barra espaciadora', 'Mano mientras se mantiene'], ['Ctrl + rueda', 'Zoom en el cursor'], ['Rueda / Mayus + rueda', 'Desplazar'],
          ['Ctrl+0 / Ctrl+1', 'Encajar / 100%'], ['Flechas (Mayus: x10)', 'Mover lo elegido'], ['Alt al arrastrar', 'Sin ajuste magnetico'],
          ['Ctrl+C / X / V / J', 'Copiar, cortar, pegar, duplicar'], ['Ctrl+G / Ctrl+Mayus+G', 'Agrupar / desagrupar'],
          ['Ctrl+] / [  (Mayus: del todo)', 'Adelante / atras'], ['Ctrl+A / Ctrl+D', 'Seleccionar todo / nada'], ['Supr', 'Borrar'],
          ['Ctrl+Z / Ctrl+Y', 'Deshacer / rehacer'], ['Ctrl+S', 'Guardar'], ['Intro', 'Reproducir el clip'], ['F5', 'Vista previa interactiva'],
          ['Reglas', 'Arrastra desde una regla para crear una guia; arrastra la guia a la regla para quitarla'],
        ].map(([k, v]) => <tr key={k}><td><kbd>{k}</kbd></td><td>{v}</td></tr>)}</tbody></table>
      </InfoDialog>}
      {dialog === 'game' && <InfoDialog title="En el juego" close={() => setDialog(null)}>
        <p>Al exportar, la interfaz queda en la carpeta del juego. Desde cualquier script del juego:</p>
        <pre>{`re_ui_${n}_show              abre la pantalla inicial (entrada y bucle)
re_ui_${n}_hide              salida y la cierra
re_ui_${n}_hide_now          la quita al momento
re_ui_${n}_goto screen = 1   va a otra pantalla (0 = la primera)
${proj.screens.map((s, i) => `re_ui_${n}_goto_${clean(s.name) || 'pantalla'}_${i}`).join('\n')}
re_ui_${n}_active            1 mientras esta abierta`}</pre>
        <p>Si tiene botones, mientras esta abierta el mando la maneja (arriba, abajo, izquierda, derecha, elegir y atras) y tambien el raton.
          Los botones pueden ir a otra pantalla, volver, cerrar, ejecutar un script del juego o cambiar una variable entera del juego; las capas
          pueden mostrarse segun una variable del juego. La dibuja re-wor encima del juego.</p>
      </InfoDialog>}
    </div>
  );
}

const Toast = ({ msg }) => <div className={'st-toast ' + msg.kind}><Icon n={msg.kind === 'err' ? 'alert' : msg.kind === 'warn' ? 'warn' : 'check'} />{msg.text}</div>;
const MODE_ICON = { '2d': 'screen', '25d': 'film', '3d': 'cube' };

function InfoDialog({ title, children, close }) {
  return <div className="st-modal" onMouseDown={close}><div className="st-modalbox ue-info" onMouseDown={e => e.stopPropagation()}>
    <h3>{title}<button className="st-icon" onClick={close}><Icon n="x" /></button></h3>{children}</div></div>;
}

function StartDialog({ openFile, openDisk, create, toast, close }) {
  const [mode, setMode] = useState('2d');
  const [name, setName] = useState('mi_interfaz');
  const [list, setList] = useState(null);
  useEffect(() => { listProjects('ui').then(r => setList(r.files)).catch(e => toast(e.message, 'err')); }, []);
  const go = m => create(newProject(m, clean(name) || 'mi_interfaz'));
  return (
    <div className="st-modal">
      <div className="st-modalbox st-start">
        <h3><Icon n="screen" />Nueva interfaz{close && <button className="st-icon" onClick={close} title="Cerrar"><Icon n="x" /></button>}</h3>
        <p className="st-muted">Que tipo de interfaz quieres hacer?</p>
        <div className="st-modes">
          {Object.entries(MODES).map(([k, m]) => (
            <button key={k} className={'st-mode' + (mode === k ? ' on' : '')} onClick={() => setMode(k)} onDoubleClick={() => go(k)}>
              <Icon n={MODE_ICON[k]} s={26} /><b>{m.label}</b><span>{m.desc}</span>
            </button>))}
        </div>
        <div className="st-startrow">
          <label>Nombre <input value={name} onChange={e => setName(e.target.value)} onBlur={e => setName(clean(e.target.value) || 'mi_interfaz')} /></label>
          <span className="st-muted">En el juego: re_ui_{clean(name) || 'mi_interfaz'}_show / _hide</span>
          <span className="st-grow" />
          <button className="primary" onClick={() => go(mode)}><Icon n="plus" />Crear {MODES[mode].label}</button>
        </div>
        <h4>Abrir</h4>
        <div className="st-recent">
          {list === null && <div className="st-muted">Leyendo la biblioteca...</div>}
          {list?.length === 0 && <div className="st-muted">Todavia no hay interfaces guardadas.</div>}
          {list?.map(r => <button key={r.file} onClick={() => openFile(r.file)}><Icon n={MODE_ICON[r.mode] || 'screen'} />{r.file}<span>{MODES[r.mode]?.label || '2D'}  {new Date(r.modified * 1000).toLocaleString()}</span></button>)}
        </div>
        <div className="st-startrow">
          <a className="st-link" href="index.html">Volver al inicio</a>
          <span className="st-grow" />
          <button onClick={openDisk}><Icon n="folder" />Abrir archivo .desp</button>
        </div>
      </div>
    </div>
  );
}

function QbView({ qb, close }) {
  return (
    <div className="st-modal" onMouseDown={close}>
      <div className="st-modalbox" onMouseDown={e => e.stopPropagation()}>
        <h3><Icon n="code" />qb/ui_{qb.name}.q  +  ui3d/{qb.name}/scene.ui3d<button className="st-icon" onClick={close}><Icon n="x" /></button></h3>
        {qb.warnings?.length > 0 && <div className="st-note"><Icon n="warn" s={14} />{qb.warnings.join(' / ')}</div>}
        <pre>{qb.q}{'\n\n// ---- escena para re-wor (primeras lineas) ----\n' + (qb.scene || '')}</pre>
      </div>
    </div>
  );
}

createRoot(document.getElementById('app')).render(<App />);
