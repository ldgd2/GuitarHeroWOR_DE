// Linea de tiempo: grupos desplegables (animados con anime.js) con subpistas. Cada subpista tiene su cabecera
// (nombre editable, activar/ocultar, bloquear, menu) y su carril con bloques arrastrables. Las pistas de audio
// dibujan la forma de onda; la de compases, las notas. Ver timeline/layout.js para los tipos de pista.
import anime from 'animejs';
import { changed, dropUndo, emit, fmt, pushUndo, S } from '../../core/state.js';
import { GROUP_GAP, GROUP_H, HEAD, TRACK_H, trackItems } from '../../timeline/lanes.js';
import { isUserTrack, layout, newId, USER_KINDS } from '../../timeline/layout.js';
import { msOf, xOf } from '../../timeline/coords.js';
import { addAt, fxOf, propOf, setLen, setT, shotOf, snapT } from '../../timeline/edit.js';
import { A, toggleGroup } from '../../audio/player.js';
import { ask, confirmBox } from '../overlay.js';
import { Icon, useBus, useEffect, useLayoutEffect, useRef, useState } from '../react.jsx';

const RULER = 26;
const same = (a, b) => a && b && a.type === b.type && a.track === b.track && a.i === b.i && a.key === b.key && a.id === b.id;
const KIND_LABEL = { audio: 'audio', stem: 'audio', bars: 'cancion', shots: 'tomas', auto: 'juego', moment: 'juego', fx: 'vista previa', light: 'luces',
  clip: 'juego', loop: 'juego', events: 'juego', note: 'juego', bandmoment: 'lectura', props: 'vista previa' };
const editLayout = fn => { pushUndo(); fn(layout()); changed(); };

// ---------- forma de onda ----------
function peaksFor(ids) {
  const P = A.info?.peaks; if (!P) return null;
  const arrs = ids.map(i => P[i]).filter(Boolean); if (!arrs.length) return null;
  return arrs;
}
function Wave({ ids, w, h, dim, color }) {
  const ref = useRef();
  useLayoutEffect(() => {
    const cv = ref.current; if (!cv) return;
    const dpr = devicePixelRatio || 1;
    cv.width = Math.max(1, w * dpr); cv.height = h * dpr;
    const c = cv.getContext('2d'); c.setTransform(dpr, 0, 0, dpr, 0, 0); c.clearRect(0, 0, w, h);
    const arrs = peaksFor(ids); if (!arrs) return;
    const pps = A.info.peaks_per_s, mid = h / 2;
    c.fillStyle = color; c.globalAlpha = dim ? 0.25 : 0.9;
    for (let x = 0; x < w; x++) {
      const a = Math.floor(msOf(x + HEAD) / 1000 * pps), b = Math.max(a + 1, Math.floor(msOf(x + HEAD + 1) / 1000 * pps));
      if (b <= 0) continue;
      let v = 0;
      for (const arr of arrs) { let m = 0; for (let k = Math.max(0, a); k < Math.min(arr.length, b); k++) if (arr[k] > m) m = arr[k]; v += m; }
      v = Math.min(1, v); if (!v) continue;
      const y = v * (mid - 2); c.fillRect(x, mid - y, 1, y * 2);
    }
  });
  return <canvas ref={ref} className="wave" style={{ width: w + 'px', height: h + 'px' }} />;
}

// notas de la guitarra en la pista de compases
function Gems({ w, h }) {
  const ref = useRef();
  useLayoutEffect(() => {
    const cv = ref.current, P = S.proj; if (!cv || !P) return;
    const dpr = devicePixelRatio || 1; cv.width = Math.max(1, w * dpr); cv.height = h * dpr;
    const c = cv.getContext('2d'); c.setTransform(dpr, 0, 0, dpr, 0, 0); c.clearRect(0, 0, w, h);
    const t0 = msOf(HEAD), t1 = msOf(HEAD + w);
    c.fillStyle = '#cfd5e1';
    for (const g of P.gems) if (g >= t0 && g <= t1) c.fillRect(xOf(g) - HEAD, 6, 1.5, h - 12);
    c.fillStyle = 'rgba(245,168,59,.8)';
    P.fretbars.forEach((b, i) => { if (i % 4 === 0 && b >= t0 && b <= t1) c.fillRect(xOf(b) - HEAD, h - 4, 2, 4); });
  });
  return <canvas ref={ref} className="wave" style={{ width: w + 'px', height: h + 'px' }} />;
}

// ---------- menu desplegable ----------
function Menu({ items, onClose }) {
  const ref = useRef();
  useEffect(() => {
    anime({ targets: ref.current, opacity: [0, 1], translateY: [-6, 0], duration: 160, easing: 'easeOutQuad' });
    const off = e => { if (!ref.current?.contains(e.target)) onClose(); };
    setTimeout(() => addEventListener('pointerdown', off), 0);
    return () => removeEventListener('pointerdown', off);
  }, []);
  return <div className="menu" ref={ref} onPointerDown={e => e.stopPropagation()}>
    {items.filter(Boolean).map((it, k) => it === '-' ? <hr key={k} /> : it.title ? <div key={k} className="mtitle">{it.title}</div>
      : <button key={k} className={it.danger ? 'danger' : ''} disabled={it.disabled} onClick={() => { onClose(); it.run(); }}>
        {it.icon && <Icon n={it.icon} s={14} />}{it.label}</button>)}
  </div>;
}

function Editable({ value, onCommit, className }) {
  const [ed, setEd] = useState(false);
  if (!ed) return <span className={className} title="Doble clic para renombrar" onDoubleClick={() => setEd(true)}>{value}</span>;
  return <input className="rename" autoFocus defaultValue={value} onPointerDown={e => e.stopPropagation()}
    onBlur={e => { setEd(false); if (e.target.value.trim() && e.target.value !== value) onCommit(e.target.value.trim()); }}
    onKeyDown={e => { if (e.key === 'Enter') e.target.blur(); if (e.key === 'Escape') setEd(false); }} />;
}

const parseNotes = s => {
  const out = new Set();
  for (const part of s.split(/[,\s]+/).filter(Boolean)) {
    const m = /^(\d+)(?:-(\d+))?$/.exec(part); if (!m) continue;
    for (let n = +m[1]; n <= +(m[2] || m[1]); n++) out.add(n);
  }
  return [...out].sort((a, b) => a - b);
};
const fmtNotes = ns => Array.isArray(ns) ? ns.reduce((acc, n, i) => {
  if (i && n === ns[i - 1] + 1) { acc[acc.length - 1][1] = n; } else acc.push([n, n]); return acc;
}, []).map(([a, b]) => a === b ? a : `${a}-${b}`).join(', ') : 'resto';

// ---------- cabeceras ----------
function TrackHead({ tr, g }) {
  const [menu, setMenu] = useState(false);
  const L = layout(), ti = g.tracks.indexOf(tr);
  const move = d => editLayout(() => { const a = g.tracks; const j = ti + d; if (j < 0 || j >= a.length) return; [a[ti], a[j]] = [a[j], a[ti]]; });
  const toGroup = dst => editLayout(() => { g.tracks.splice(ti, 1); dst.tracks.push(tr); dst.open = true; });
  const items = [
    { label: 'Renombrar', icon: 'edit', run: async () => { const n = await ask('Nombre de la pista:', tr.name); if (n) editLayout(() => { tr.name = n; }); } },
    tr.kind === 'light' && Array.isArray(tr.notes) && { label: `Notas: ${fmtNotes(tr.notes)}`, icon: 'light', run: async () => {
      const n = await ask('Notas de luces que van en esta pista (p. ej. 78 o 90-97, 101):', fmtNotes(tr.notes));
      if (n !== null) editLayout(() => { tr.notes = parseNotes(n); });
    } },
    { label: 'Subir', icon: 'chev', disabled: ti === 0, run: () => move(-1) },
    { label: 'Bajar', icon: 'chev', disabled: ti === g.tracks.length - 1, run: () => move(1) },
    '-', { title: 'Mover al grupo' },
    ...L.groups.filter(x => x !== g).map(x => ({ label: x.name, icon: x.icon || 'folder', run: () => toGroup(x) })),
    '-',
    isUserTrack(tr) && { label: 'Borrar pista', icon: 'trash', danger: true, run: async () => {
      if (await confirmBox(`Borrar la pista "${tr.name}"? Sus elementos pasan a la primera pista del mismo tipo.`, 'Borrar', 'danger'))
        editLayout(() => { g.tracks.splice(ti, 1); });
    } },
  ];
  const muted = tr.kind === 'stem' && A.muted.has(tr.stem);
  const empty = tr.kind === 'stem' && A.info && !A.info.groups?.some(x => x.id === tr.stem);
  return <div className={'thead' + (tr.off ? ' off' : '')} style={{ width: HEAD + 'px', '--c': g.color }} onPointerDown={e => e.stopPropagation()}>
    <Editable className="tname" value={tr.name} onCommit={n => editLayout(() => { tr.name = n; })} />
    <span className="kind">{KIND_LABEL[tr.kind]}</span>
    {tr.kind === 'stem'
      ? <button className={'tb' + (muted ? ' on' : '')} disabled={empty} title={muted ? 'Activar este instrumento' : 'Silenciar este instrumento'} onClick={() => toggleGroup(tr.stem)}><Icon n={muted ? 'mute' : 'vol'} s={14} /></button>
      : <button className={'tb' + (tr.off ? ' on' : '')} title={tr.off ? 'Pista apagada (no se ve ni se compila)' : 'Apagar la pista'} onClick={() => editLayout(() => { tr.off = !tr.off; })}><Icon n={tr.off ? 'eyeoff' : 'eye'} s={14} /></button>}
    <button className={'tb' + (tr.lock ? ' on' : '')} title={tr.lock ? 'Desbloquear' : 'Bloquear (no se pueden mover sus elementos)'} onClick={() => editLayout(() => { tr.lock = !tr.lock; })}><Icon n={tr.lock ? 'lock' : 'unlock'} s={14} /></button>
    {tr.kind !== 'audio' && tr.kind !== 'stem' && tr.kind !== 'bars' && tr.kind !== 'bandmoment' &&
      <button className="tb add" title="Anadir en el cursor" onClick={() => addAt(tr, snapT(S.t))}><Icon n="plus" s={14} /></button>}
    <button className="tb" title="Mas opciones" onClick={() => setMenu(!menu)}><Icon n="dots" s={14} /></button>
    {menu && <Menu items={items} onClose={() => setMenu(false)} />}
  </div>;
}

function GroupHead({ g, groups, count }) {
  const [menu, setMenu] = useState(false);
  const gi = groups.indexOf(g);
  const addTrack = kind => editLayout(() => {
    const n = g.tracks.filter(t => t.kind === kind).length + 1;
    const t = { id: newId(kind + '_'), kind, name: `${USER_KINDS[kind]} ${n}` };
    if (kind === 'light') t.notes = [];
    g.tracks.push(t); g.open = true;
  });
  const move = d => editLayout(L => { const j = gi + d; if (j < 0 || j >= L.groups.length) return; [L.groups[gi], L.groups[j]] = [L.groups[j], L.groups[gi]]; });
  const items = [
    { title: 'Nueva subpista' },
    ...Object.entries(USER_KINDS).map(([k, l]) => ({ label: { shots: 'Escena de camaras', fx: 'Pista de efectos', light: 'Pista de luces / fuego', props: 'Pista de objetos 3D' }[k] || l, icon: 'plus', run: () => addTrack(k) })),
    '-',
    { label: 'Renombrar grupo', icon: 'edit', run: async () => { const n = await ask('Nombre del grupo:', g.name); if (n) editLayout(() => { g.name = n; }); } },
    { label: 'Subir grupo', icon: 'chev', disabled: gi === 0, run: () => move(-1) },
    { label: 'Bajar grupo', icon: 'chev', disabled: gi === groups.length - 1, run: () => move(1) },
    { label: 'Borrar grupo', icon: 'trash', danger: true, disabled: g.tracks.length > 0, run: () => editLayout(L => { L.groups.splice(gi, 1); }) },
  ];
  const toggle = () => { g.open = !g.open; emit('timeline'); };
  return <div className="ghead" style={{ height: GROUP_H + 'px', '--c': g.color }}>
    <div className="gcell" style={{ width: HEAD + 'px' }} onPointerDown={e => e.stopPropagation()}>
      <button className={'chev' + (g.open ? ' open' : '')} onClick={toggle}><Icon n="chev" s={14} /></button>
      <span className="gicon"><Icon n={g.icon || 'folder'} s={14} /></span>
      <Editable className="gname" value={g.name} onCommit={n => editLayout(() => { g.name = n; })} />
      <span className="gcount">{g.tracks.length} · {count}</span>
      {g.add && <button className="tb" title={`Nueva subpista de ${USER_KINDS[g.add].toLowerCase()}`} onClick={() => addTrack(g.add)}><Icon n="plus" s={14} /></button>}
      <button className="tb" onClick={() => setMenu(!menu)}><Icon n="dots" s={14} /></button>
      {menu && <Menu items={items} onClose={() => setMenu(false)} />}
    </div>
  </div>;
}

// ---------- bloques ----------
function Item({ it, h, onDown, fresh }) {
  const ref = useRef();
  useEffect(() => { if (fresh) anime({ targets: ref.current, scaleY: [0.2, 1], opacity: [0, 1], duration: 380, easing: 'easeOutBack' }); }, []);
  const x = xOf(it.t) - HEAD, w = Math.max(it.len ? 4 : 3, it.len * S.zoom);
  const sel = same(S.sel, it.sel);
  return <div ref={ref} className={'it' + (sel ? ' sel' : '') + (it.len ? '' : ' pt') + (it.cls ? ' ' + it.cls : '')}
    style={{ left: x + 'px', width: w + 'px', height: h - 8 + 'px', '--c': it.color }}
    title={it.title || it.label} onPointerDown={e => onDown(e, it)}>
    {w > 26 && <span>{it.label}</span>}{it.resize && w > 10 && <i className="grip"></i>}</div>;
}

const seen = new Set();   // bloques ya pintados (para animar solo los nuevos)

function TrackRow({ tr, g, groups, W, onItemDown, t0, t1 }) {
  const h = TRACK_H[tr.kind] || 30, lw = W - HEAD;
  let lane;
  if (tr.kind === 'audio' || tr.kind === 'stem') {
    const ids = tr.kind === 'audio' ? (A.info?.groups || []).map(x => x.id).filter(x => !A.muted.has(x)) : [tr.stem];
    lane = A.info?.available ? <Wave ids={ids} w={lw} h={h} dim={tr.kind === 'stem' && A.muted.has(tr.stem)} color={tr.kind === 'audio' ? '#5aa9ff' : g.color} />
      : tr.kind === 'audio' ? <span className="laneMsg">{A.loading ? 'Cargando audio...' : A.info?.reason || 'Sin audio'}</span> : null;
  } else if (tr.kind === 'bars') lane = <Gems w={lw} h={h} />;
  else {
    lane = trackItems(tr).filter(it => it.t + it.len >= t0 && it.t <= t1).map((it, k) => {
      const key = it.uid || tr.id + ':' + k, fresh = it.uid && !seen.has(it.uid) && S.animateNew;
      if (it.uid) seen.add(it.uid);
      return <Item key={key} it={it} h={h} onDown={(e, x) => onItemDown(e, x, tr)} fresh={fresh} />;
    });
  }
  return <div className={'trk k_' + tr.kind + (tr.off ? ' off' : '')} data-track={tr.id} data-kind={tr.kind} style={{ height: h + 'px' }}>
    <TrackHead tr={tr} g={g} />
    <div className="lane" style={{ width: lw + 'px' }}>{lane}</div>
  </div>;
}

// resumen de un grupo plegado: marcas finas de todos sus elementos
function GroupSummary({ g, W, t0, t1 }) {
  const marks = [];
  for (const tr of g.tracks) for (const it of (['audio', 'stem', 'bars'].includes(tr.kind) ? [] : trackItems(tr)))
    if (it.t + it.len >= t0 && it.t <= t1) marks.push(it);
  return <div className="gsum" style={{ width: W - HEAD + 'px' }}>
    {marks.slice(0, 600).map((it, k) => <i key={k} style={{ left: xOf(it.t) - HEAD + 'px', width: Math.max(2, it.len * S.zoom) + 'px', background: it.color }} />)}
  </div>;
}

function GroupBlock({ g, groups, W, onItemDown, t0, t1 }) {
  const body = useRef(), first = useRef(true), [shown, setShown] = useState(g.open);
  useLayoutEffect(() => {
    if (first.current) { first.current = false; return; }
    const el = body.current; if (!el) return;
    if (g.open) {
      setShown(true);
      requestAnimationFrame(() => {
        const full = el.scrollHeight;
        anime({ targets: el, height: [0, full], opacity: [0, 1], duration: 320, easing: 'easeOutCubic', complete: () => { el.style.height = ''; } });
        anime({ targets: el.querySelectorAll('.trk'), translateX: [-14, 0], opacity: [0, 1], delay: anime.stagger(28), duration: 300, easing: 'easeOutQuad' });
      });
    } else {
      anime({ targets: el, height: [el.scrollHeight, 0], opacity: [1, 0], duration: 240, easing: 'easeInCubic', complete: () => setShown(false) });
    }
  }, [g.open]);
  const count = g.tracks.reduce((n, tr) => n + (['audio', 'stem', 'bars'].includes(tr.kind) ? 0 : trackItems(tr).length), 0);
  return <section className="grp" style={{ '--c': g.color, marginBottom: GROUP_GAP + 'px' }}>
    <div className="grow">
      <GroupHead g={g} groups={groups} count={count} />
      {!g.open && <GroupSummary g={g} W={W} t0={t0} t1={t1} />}
    </div>
    {(shown || g.open) && <div className="gbody" ref={body} style={g.open ? null : { overflow: 'hidden' }}>
      {g.tracks.map(tr => <TrackRow key={tr.id} tr={tr} g={g} groups={groups} W={W} onItemDown={onItemDown} t0={t0} t1={t1} />)}
      {!g.tracks.length && <div className="trk emptyTrk"><div className="thead" style={{ width: HEAD + 'px' }}>Grupo vacio</div>
        <div className="lane laneMsg">Usa el menu ··· del grupo para crear subpistas o mueve aqui pistas de otros grupos.</div></div>}
    </div>}
  </section>;
}

// ---------- regla ----------
function Ruler({ W }) {
  const ref = useRef();
  useLayoutEffect(() => {
    const cv = ref.current, P = S.proj; if (!cv || !P) return;
    const w = W - HEAD, dpr = devicePixelRatio || 1; cv.width = Math.max(1, w * dpr); cv.height = RULER * dpr;
    const c = cv.getContext('2d'); c.setTransform(dpr, 0, 0, dpr, 0, 0); c.clearRect(0, 0, w, RULER);
    c.font = '10px system-ui'; c.textBaseline = 'middle';
    let lastX = -1e9; const t0 = msOf(HEAD), t1 = msOf(W);
    P.fretbars.forEach((b, i) => {
      if (b < t0 || b > t1) return;
      const x = xOf(b) - HEAD, bar = i % 4 === 0;
      c.fillStyle = bar ? '#5c6578' : '#343a48'; c.fillRect(x, bar ? 12 : 19, 1, RULER);
      if (bar && x - lastX > 58) { c.fillStyle = '#9aa3b5'; c.fillText(fmt(b).slice(0, -4) + `  · ${i / 4 + 1}`, x + 4, 7); lastX = x; }
    });
  });
  return <canvas ref={ref} style={{ width: W - HEAD + 'px', height: RULER + 'px' }} />;
}

// ---------- linea de tiempo ----------
export function Timeline() {
  useBus('timeline', 'changed', 'song', 'inspector', 'audio');
  const root = useRef(), grid = useRef(), drag = useRef(null);
  const [W, setW] = useState(1000);
  useEffect(() => {
    const ro = new ResizeObserver(() => { setW(root.current.clientWidth); emit('timeline'); });
    ro.observe(root.current); return () => ro.disconnect();
  }, []);
  useEffect(() => { const id = setTimeout(() => { S.animateNew = true; }, 1500); return () => clearTimeout(id); }, [S.proj?.song?.id]);
  const P = S.proj;
  // rejilla de compases (vertical) sobre toda la altura
  useLayoutEffect(() => {
    const cv = grid.current, el = root.current; if (!cv || !P) return;
    const H = el.scrollHeight, dpr = devicePixelRatio || 1;
    cv.style.height = H + 'px'; cv.width = W * dpr; cv.height = H * dpr;
    const c = cv.getContext('2d'); c.setTransform(dpr, 0, 0, dpr, 0, 0); c.clearRect(0, 0, W, H);
    const t0 = msOf(HEAD), t1 = msOf(W);
    P.fretbars.forEach((b, i) => {
      if (b < t0 || b > t1) return;
      c.fillStyle = i % 4 ? 'rgba(255,255,255,.03)' : 'rgba(255,255,255,.08)'; c.fillRect(xOf(b), RULER, 1, H);
    });
  });

  const local = e => { const r = root.current.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; };
  const onItemDown = (e, it, tr) => {
    if (e.button !== 0) return;
    e.stopPropagation();
    if (!it.sel) return;
    S.sel = it.sel; S.tab = { shot: 'cams', fx: 'fx', prop: 'props' }[it.sel.type] || 'insp';
    emit('inspector'); emit('timeline');
    if (tr.lock) return;
    const [x] = local(e), x1 = xOf(it.t) + Math.max(4, it.len * S.zoom);
    pushUndo();
    drag.current = { sel: it.sel, kind: tr.kind, edge: it.resize && x > x1 - 7, x0: x, t0: it.t, len0: it.len, moved: false };
    root.current.setPointerCapture(e.pointerId);
  };
  const onDown = e => {
    if (!P || e.button !== 0) return;
    const [x] = local(e); if (x < HEAD) return;
    S.t = Math.max(0, msOf(x)); S.sel = null;
    drag.current = { scrub: true }; root.current.setPointerCapture(e.pointerId);
    emit('inspector'); emit('timeline');
  };
  const onMove = e => {
    const d = drag.current; if (!d || !P) return;
    const [x] = local(e);
    if (d.scrub) { S.t = Math.max(0, Math.min(P.length, msOf(x))); emit('timeline'); return; }
    if (Math.abs(x - d.x0) > 2) d.moved = true;
    // arrastrar en vertical a otra subpista del mismo tipo (tomas, efectos, objetos)
    const row = document.elementFromPoint(e.clientX, e.clientY)?.closest('.trk');
    if (row && row.dataset.kind === d.kind && ['shots', 'fx', 'props'].includes(d.kind)) {
      const obj = d.sel.type === 'shot' ? shotOf(d.sel) : d.sel.type === 'fx' ? fxOf(d.sel) : propOf(d.sel);
      if (obj && obj.track !== row.dataset.track) { obj.track = row.dataset.track; d.moved = true; }
    }
    if (!d.moved) return;
    const dt = (x - d.x0) / S.zoom;
    if (d.edge) setLen(d.sel, d.len0 + dt); else setT(d.sel, snapT(d.t0 + dt, e.altKey));
    S.ver++; emit('timeline');
  };
  const onUp = () => {
    const d = drag.current; drag.current = null; if (!d || d.scrub) return;
    if (d.moved) changed(); else dropUndo();
  };
  const onDbl = e => {
    if (!P) return;
    const [x] = local(e); if (x < HEAD) return;
    const row = e.target.closest('.trk'); if (!row || e.target.closest('.it')) return;
    const tr = layout().groups.flatMap(g => g.tracks).find(t => t.id === row.dataset.track);
    if (tr && !['audio', 'stem', 'bars', 'bandmoment'].includes(tr.kind)) addAt(tr, snapT(msOf(x)));
  };
  useEffect(() => {   // rueda: Ctrl = zoom, Mayus = desplazar (listener no pasivo)
    const el = root.current;
    const wheel = e => {
      if (!S.proj) return;
      if (e.ctrlKey) {
        e.preventDefault();
        const x = local(e)[0], m = msOf(x);
        S.zoom = Math.min(2, Math.max(0.004, S.zoom * (e.deltaY < 0 ? 1.2 : 1 / 1.2)));
        S.scroll = Math.max(0, m - (x - HEAD) / S.zoom);
      } else if (e.shiftKey || Math.abs(e.deltaX) > Math.abs(e.deltaY)) {
        e.preventDefault(); S.scroll = Math.max(0, S.scroll + (e.deltaX || e.deltaY) / S.zoom);
      } else return;
      emit('timeline');
    };
    el.addEventListener('wheel', wheel, { passive: false });
    return () => el.removeEventListener('wheel', wheel);
  }, []);

  if (!P) return <div id="tlScroll" className="tl empty" ref={root}><p>Abre una cancion para ver su linea de tiempo.</p></div>;
  const groups = layout().groups;
  const t0 = msOf(HEAD) - 50, t1 = msOf(W) + 50, px = xOf(S.t);
  const newGroup = async () => { const n = await ask('Nombre del grupo nuevo:', 'Escenas'); if (n) editLayout(L => { L.groups.push({ id: newId('g_'), name: n, icon: 'folder', color: '#8a93a6', open: true, tracks: [] }); }); };
  const allOpen = groups.every(g => g.open);
  return <div id="tlScroll" className="tl" ref={root} onPointerDown={onDown} onPointerMove={onMove} onPointerUp={onUp} onDoubleClick={onDbl}>
    <canvas className="tlGrid" ref={grid} style={{ width: W + 'px' }}></canvas>
    <div className="ruler" style={{ height: RULER + 'px' }}>
      <div className="rhead" style={{ width: HEAD + 'px' }} onPointerDown={e => e.stopPropagation()}>
        <button className="tb" title={allOpen ? 'Plegar todos los grupos' : 'Desplegar todos'} onClick={() => { groups.forEach(g => { g.open = !allOpen; }); emit('timeline'); }}>
          <Icon n="chev" s={14} /> {allOpen ? 'Plegar' : 'Desplegar'}</button>
        <button className="tb" title="Nuevo grupo de pistas" onClick={newGroup}><Icon n="folder" s={14} /> Grupo</button>
      </div>
      <Ruler W={W} />
    </div>
    {groups.map(g => <GroupBlock key={g.id} g={g} groups={groups} W={W} onItemDown={onItemDown} t0={t0} t1={t1} />)}
    {px >= HEAD && <div className="playhead" style={{ left: px + 'px' }}><i></i></div>}
  </div>;
}
