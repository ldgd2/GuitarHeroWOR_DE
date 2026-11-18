// Lienzo de las pantallas 2D y 2.5D, al estilo de un programa de diseno: zoom y desplazamiento, reglas y guias,
// caja de transformacion (mover, tamano desde 8 tiradores, girar por fuera de las esquinas), seleccion multiple y
// por recuadro, ajuste magnetico (bordes y centros del lienzo, guias y otras capas) y herramientas de dibujo:
// texto (clic = texto de punto, arrastrar = caja), formas (rectangulo, elipse, poligono, estrella, linea) y pluma.
// En 2.5D cada capa se proyecta con la camara (lo mismo que hace el exportador).
import { listsOf } from './lists.js';
import React, { useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';
import { LayerCanvas } from '../studio/LayerCanvas.jsx';
import { fontCss, textSize } from '../studio/raster.js';
import { H, W, newLayer, setProp, stateAt, BLEND_CSS, condOk } from './model.js';
import { bboxOf, corners, fitText, hits, itemBox, layerUrl, layout, screenToWorldDelta, unionBox } from './view2d.js';
import { lookCss, liveCss } from './logic.js';

const HANDLES = [[-1, -1], [0, -1], [1, -1], [1, 0], [1, 1], [0, 1], [-1, 1], [-1, 0]];
const CURSOR = { '-1,-1': 'nwse-resize', '1,1': 'nwse-resize', '1,-1': 'nesw-resize', '-1,1': 'nesw-resize', '0,-1': 'ns-resize', '0,1': 'ns-resize', '-1,0': 'ew-resize', '1,0': 'ew-resize' };
const SHAPE_TOOLS = { rect: 'rect', ellipse: 'ellipse', polygon: 'polygon', star: 'star', line: 'line' };
const RULER = 20;

// numero 0..1 repetible por capa y fotograma (grano, parpadeo, temblor)
function rnd(id, seed, salt) {
  let h = 2166136261 ^ salt;
  for (const ch of String(id) + ':' + seed) h = Math.imul(h ^ ch.charCodeAt(0), 16777619);
  h ^= h >>> 13; h = Math.imul(h, 0x5bd1e995); h ^= h >>> 15;
  return (h >>> 0) / 4294967296;
}

export function Stage2D({ proj, doc, mode, clip, t, sel, setSel, tool, setTool, edit, live, opts, view, setView, refImg, refAlpha,
  editingText, setEditingText, overrides, readOnly, onPick, onListWheel }) {
  const wrap = useRef(null);
  const [size, setSize] = useState({ w: 800, h: 500 });
  const [drag, setDrag] = useState(null);          // estado visual del gesto en curso (recuadro, forma nueva, guias)
  const [pen, setPen] = useState(null);            // puntos de la pluma
  const [space, setSpace] = useState(false);
  const [hoverGuide, setHoverGuide] = useState(null);
  const urlOf = l => layerUrl(proj, l);
  // al disenar, una capa con condicion se ve si se cumple con el valor de diseno de su variable (Interaccion)
  const items = useMemo(() => layout(doc, mode, clip, t).filter(it => readOnly || !it.l.showIf || condOk(it.l.showIf, proj?.designVars)),
    [doc, mode, clip, t, readOnly, proj?.designVars]);
  const listBoxes = useMemo(() => readOnly ? [] : listsOf(doc), [doc, readOnly]);
  const z = view.z;
  const toS = (x, y) => [view.x + x * z, view.y + y * z];                       // mundo -> pantalla (px del contenedor)
  const toW = (cx, cy) => [(cx - view.x) / z, (cy - view.y) / z];

  // tamano del contenedor y encuadre inicial
  useLayoutEffect(() => {
    const ro = new ResizeObserver(() => {
      const r = wrap.current.getBoundingClientRect();
      setSize({ w: r.width, h: r.height });
    });
    ro.observe(wrap.current);
    return () => ro.disconnect();
  }, []);
  useEffect(() => { if (view.fit) setView(fitView(size, readOnly)); }, [size.w, size.h, view.fit]);

  // barra espaciadora = mano
  useEffect(() => {
    if (readOnly) return;
    const dn = e => { if (e.code === 'Space' && !/INPUT|TEXTAREA|SELECT/.test(e.target.tagName) && !e.repeat) { setSpace(true); } };
    const up = e => { if (e.code === 'Space') setSpace(false); };
    window.addEventListener('keydown', dn); window.addEventListener('keyup', up);
    return () => { window.removeEventListener('keydown', dn); window.removeEventListener('keyup', up); };
  }, [readOnly]);

  // pluma: Intro termina, Esc cancela
  useEffect(() => {
    if (!pen) return;
    const k = e => {
      if (e.key === 'Enter') { e.preventDefault(); finishPen(false); }
      if (e.key === 'Escape') { e.preventDefault(); setPen(null); }
    };
    window.addEventListener('keydown', k);
    return () => window.removeEventListener('keydown', k);
  });
  useEffect(() => { if (tool !== 'pen') setPen(null); }, [tool]);

  // ------------------------------------------------------------------ rueda: Ctrl = zoom en el cursor, si no desplaza
  const onWheel = e => {
    const r = wrap.current.getBoundingClientRect();
    const cx = e.clientX - r.left, cy = e.clientY - r.top;
    if (onListWheel && !e.ctrlKey && Math.abs(e.deltaY) > 0) {   // vista previa: la rueda mueve las listas
      const [wx, wy] = toW(cx, cy);
      if (onListWheel(wx, wy, Math.sign(e.deltaY))) return;
    }
    if (e.ctrlKey || e.altKey || readOnly) {
      const nz = Math.max(0.05, Math.min(32, z * Math.pow(1.0015, -e.deltaY)));
      const [wx, wy] = toW(cx, cy);
      setView({ z: nz, x: cx - wx * nz, y: cy - wy * nz, fit: false });
    } else {
      setView({ ...view, x: view.x - (e.shiftKey ? e.deltaY : e.deltaX), y: view.y - (e.shiftKey ? 0 : e.deltaY), fit: false });
    }
  };
  useEffect(() => {
    const el = wrap.current;
    const h = e => { e.preventDefault(); onWheel(e); };
    el.addEventListener('wheel', h, { passive: false });
    return () => el.removeEventListener('wheel', h);
  });

  // ------------------------------------------------------------------ ajuste magnetico
  const snapTargets = (exclude) => {
    const xs = [0, W / 2, W, ...(proj.guides?.x || [])], ys = [0, H / 2, H, ...(proj.guides?.y || [])];
    if (opts.snapLayers) for (const it of items) {
      if (it.group || exclude.has(it.l.id)) continue;
      const b = bboxOf(corners(it));
      xs.push(b.x0, (b.x0 + b.x1) / 2, b.x1); ys.push(b.y0, (b.y0 + b.y1) / 2, b.y1);
    }
    return { xs, ys };
  };
  const snap1 = (vals, cands, tol) => {
    let best = null;
    for (const v of vals) for (const c of cands) { const d = c - v; if (Math.abs(d) <= tol && (!best || Math.abs(d) < Math.abs(best.d))) best = { d, at: c }; }
    return best;
  };

  // ------------------------------------------------------------------ gestos
  const pointer = e => { const r = wrap.current.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; };
  const follow = (onMove, onUp) => {
    const mm = ev => onMove(ev);
    const uu = ev => { window.removeEventListener('pointermove', mm); window.removeEventListener('pointerup', uu); onUp?.(ev); };
    window.addEventListener('pointermove', mm);
    window.addEventListener('pointerup', uu);
  };
  const pan = e => {
    const x0 = e.clientX, y0 = e.clientY, v0 = view;
    follow(ev => setView({ ...v0, x: v0.x + ev.clientX - x0, y: v0.y + ev.clientY - y0, fit: false }));
  };
  const selItems = () => items.filter(it => sel.includes(it.l.id));
  // vista previa: lo de encima en (wx, wy) con los desplazamientos y recortes de la interfaz en marcha
  const hitPreview = (wx, wy) => [...items].reverse().find(it => {
    if (it.group) return false;
    const ov = overrides?.(it);
    if (ov?.hide || (ov?.alpha ?? 1) < 0.05) return false;
    const c = ov?.clip;
    if (c && (wx < c.x0 || wx > c.x1 || wy < c.y0 || wy > c.y1)) return false;
    return hits(it, wx - (ov?.fx?.dx || 0), wy - (ov?.fx?.dy || 0));
  });

  const onDown = e => {
    if (readOnly) {
      const [cx, cy] = pointer(e); const [wx, wy] = toW(cx, cy);
      const hit = hitPreview(wx, wy);
      onPick?.(hit?.l.id || null, 'click');
      return;
    }
    if (editingText) { setEditingText(null); return; }
    if (e.button === 1 || space || tool === 'hand') { e.preventDefault(); pan(e); return; }
    if (e.button !== 0) return;
    const [cx, cy] = pointer(e);
    const [wx, wy] = toW(cx, cy);
    if (tool === 'zoom') {
      const nz = Math.max(0.05, Math.min(32, z * (e.altKey ? 1 / 1.5 : 1.5)));
      setView({ z: nz, x: cx - wx * nz, y: cy - wy * nz, fit: false });
      return;
    }
    if (tool === 'text') return startText(e, wx, wy);
    if (SHAPE_TOOLS[tool]) return startShape(e, wx, wy, SHAPE_TOOLS[tool]);
    if (tool === 'pen') return penDown(e, wx, wy, cx, cy);
    // mover / seleccionar
    if (hoverGuide) return dragGuide(hoverGuide.axis, hoverGuide.i);
    const hit = [...items].reverse().find(it => !it.group && !it.l.locked && hits(it, wx, wy, 2 / z));
    const add = e.shiftKey || e.ctrlKey || e.metaKey;       // Mayus o Ctrl: anadir / quitar de la seleccion
    if (!hit) {
      if (!add) setSel([]);
      return marquee(cx, cy, add);
    }
    let ids = sel;
    if (add) {
      if (sel.includes(hit.l.id)) { setSel(sel.filter(i => i !== hit.l.id)); return; }
      ids = [...sel, hit.l.id]; setSel(ids);
      return moveSel(e, ids, wx, wy);
    }
    if (!sel.includes(hit.l.id)) { ids = [hit.l.id]; setSel(ids); }
    moveSel(e, ids, wx, wy);
  };

  const onDouble = e => {
    if (readOnly) return;
    const [cx, cy] = pointer(e); const [wx, wy] = toW(cx, cy);
    const hit = [...items].reverse().find(it => !it.group && hits(it, wx, wy));
    if (hit?.l.type === 'text' && !hit.l.locked) { setSel([hit.l.id]); setEditingText(hit.l.id); }
  };

  function marquee(cx, cy, add) {
    const base = add ? sel : [];
    follow(ev => {
      const [x, y] = pointer(ev);
      const b = { x0: Math.min(cx, x), y0: Math.min(cy, y), x1: Math.max(cx, x), y1: Math.max(cy, y) };
      setDrag({ kind: 'marquee', b });
      const [wx0, wy0] = toW(b.x0, b.y0), [wx1, wy1] = toW(b.x1, b.y1);
      const ids = items.filter(it => !it.group && !it.l.locked).filter(it => {
        const q = bboxOf(corners(it)); return q.x0 < wx1 && q.x1 > wx0 && q.y0 < wy1 && q.y1 > wy0;
      }).map(it => it.l.id);
      setSel([...new Set([...base, ...ids])]);
    }, () => setDrag(null));
  }

  function moveSel(e, ids, wx0, wy0) {
    const its = items.filter(it => ids.includes(it.l.id) && !it.l.locked);
    if (!its.length) return;
    const st0 = new Map(its.map(it => [it.l.id, stateAt(doc, it.l, clip, t)]));
    const box0 = unionBox(its.map(it => itemBox(items, it, doc)));
    const tg = snapTargets(new Set(ids));
    let started = false;
    follow(ev => {
      const [cx, cy] = pointer(ev); let [wx, wy] = toW(cx, cy);
      let dx = wx - wx0, dy = wy - wy0;
      if (!started && Math.abs(dx) * z + Math.abs(dy) * z < 3) return;
      if (!started) { started = true; live.begin(); }
      if (ev.shiftKey) { if (Math.abs(dx) > Math.abs(dy)) dy = 0; else dx = 0; }
      const lines = [];
      if (opts.snap && !ev.altKey) {
        const tol = 6 / z;
        const sx = snap1([box0.x0 + dx, (box0.x0 + box0.x1) / 2 + dx, box0.x1 + dx], tg.xs, tol);
        const sy = snap1([box0.y0 + dy, (box0.y0 + box0.y1) / 2 + dy, box0.y1 + dy], tg.ys, tol);
        if (sx) { dx += sx.d; lines.push({ x: sx.at }); }
        if (sy) { dy += sy.d; lines.push({ y: sy.at }); }
      }
      setDrag({ kind: 'move', lines });
      live.set(d => {
        for (const it of its) {
          const L = d.layers.find(q => q.id === it.l.id), s0 = st0.get(it.l.id);
          const [ddx, ddy] = screenToWorldDelta(it, dx, dy);
          const r = v => (ev.altKey ? Math.round(v * 10) / 10 : Math.round(v));
          setProp(d, clip, L, 'x', r(s0.x + ddx), t); setProp(d, clip, L, 'y', r(s0.y + ddy), t);
        }
      });
    }, () => setDrag(null));
  }

  // tiradores de tamano (una capa: en su marco girado; varias: la caja comun, en 2D)
  function resize(e, hx, hy) {
    e.stopPropagation();
    const its = selItems().filter(it => !it.l.locked && !it.group);
    if (!its.length) return;
    live.begin();
    const [cx0, cy0] = pointer(e);
    const [wx0, wy0] = toW(cx0, cy0);
    if (its.length === 1) {
      const it = its[0], L0 = it.l, s0 = stateAt(doc, L0, clip, t);
      const r = it.rot * Math.PI / 180, c = Math.cos(r), sn = Math.sin(r);
      const tg = snapTargets(new Set([L0.id]));
      follow(ev => {
        const [cx, cy] = pointer(ev); const [wx, wy] = toW(cx, cy);
        let dX = wx - wx0, dY = wy - wy0;
        // ajuste del borde que se mueve (solo sin giro)
        const lines = [];
        if (opts.snap && !ev.altKey && Math.abs(it.rot % 360) < 0.01) {
          const b = bboxOf(corners(it)), tol = 6 / z;
          if (hx) { const sx = snap1([(hx > 0 ? b.x1 : b.x0) + dX], tg.xs, tol); if (sx) { dX += sx.d; lines.push({ x: sx.at }); } }
          if (hy) { const sy = snap1([(hy > 0 ? b.y1 : b.y0) + dY], tg.ys, tol); if (sy) { dY += sy.d; lines.push({ y: sy.at }); } }
        }
        setDrag({ kind: 'resize', lines });
        const lx = (dX * c + dY * sn) / it.k, ly = (-dX * sn + dY * c) / it.k;     // en unidades de la capa
        let w = s0.w + lx * hx, h = s0.h + ly * hy;
        if (ev.altKey) { w = s0.w + 2 * lx * hx; h = s0.h + 2 * ly * hy; }
        if ((ev.shiftKey || L0.type === 'image' && !ev.ctrlKey) && hx && hy) {     // proporcional
          const f = Math.max(w / s0.w, h / s0.h); w = s0.w * f; h = s0.h * f;
        }
        w = Math.max(1, w); h = Math.max(1, h);
        const dw = w - s0.w, dh = h - s0.h;
        // el borde contrario se queda quieto: el ancla se mueve
        let ox = 0, oy = 0;
        if (!ev.altKey) {
          ox = hx > 0 ? s0.ax * dw : hx < 0 ? -(1 - s0.ax) * dw : 0;
          oy = hy > 0 ? s0.ay * dh : hy < 0 ? -(1 - s0.ay) * dh : 0;
        }
        const sx = (ox * c - oy * sn) * it.k, sy = (ox * sn + oy * c) * it.k;
        const [wdx, wdy] = screenToWorldDelta(it, sx, sy);
        live.set(d => {
          const L = d.layers.find(q => q.id === L0.id);
          if (L.type === 'text' && (hx === 0 || hy !== 0)) {          // texto: esquinas y arriba/abajo = tamano de letra
            const f = h / s0.h;
            L.text.px = Math.max(4, Math.round((L0.text.px || 48) * f * 10) / 10);
            if (L.text.box === 'fixed') L.base.w = Math.max(10, s0.w * f);
            fitText(L);
          } else if (L.type === 'text') {                              // izquierda/derecha = caja de parrafo
            L.text.box = 'fixed'; L.base.w = Math.round(w); fitText(L);
          } else {
            setProp(d, clip, L, 'w', Math.round(w), t); setProp(d, clip, L, 'h', Math.round(h), t);
          }
          setProp(d, clip, L, 'x', Math.round(s0.x + wdx), t); setProp(d, clip, L, 'y', Math.round(s0.y + wdy), t);
        });
      }, () => setDrag(null));
      return;
    }
    if (mode === '25d') return;
    const box = unionBox(its.map(it => bboxOf(corners(it))));
    const fxX = hx > 0 ? box.x0 : hx < 0 ? box.x1 : null, fxY = hy > 0 ? box.y0 : hy < 0 ? box.y1 : null;
    const st0 = new Map(its.map(it => [it.l.id, stateAt(doc, it.l, clip, t)]));
    follow(ev => {
      const [cx, cy] = pointer(ev); const [wx, wy] = toW(cx, cy);
      const bw = box.x1 - box.x0 || 1, bh = box.y1 - box.y0 || 1;
      let fx = hx ? Math.max(0.02, (bw + (wx - wx0) * hx) / bw) : 1, fy = hy ? Math.max(0.02, (bh + (wy - wy0) * hy) / bh) : 1;
      if (ev.shiftKey || (hx && hy)) { const f = hx && hy ? Math.max(fx, fy) : hx ? fx : fy; fx = fy = f; }
      live.set(d => {
        for (const it of its) {
          const L = d.layers.find(q => q.id === it.l.id), s0 = st0.get(it.l.id);
          const nx = fxX === null ? it.x : fxX + (it.x - fxX) * fx, ny = fxY === null ? it.y : fxY + (it.y - fxY) * fy;
          setProp(d, clip, L, 'x', Math.round(s0.x + nx - it.x), t); setProp(d, clip, L, 'y', Math.round(s0.y + ny - it.y), t);
          if (L.type === 'text') { L.text.px = Math.max(4, Math.round(it.l.text.px * Math.min(fx, fy) * 10) / 10); fitText(L); }
          else { setProp(d, clip, L, 'w', Math.max(1, Math.round(s0.w * fx)), t); setProp(d, clip, L, 'h', Math.max(1, Math.round(s0.h * fy)), t); }
        }
      });
    });
  }

  function rotate(e) {
    e.stopPropagation();
    const its = selItems().filter(it => !it.l.locked && !it.group);
    if (its.length !== 1) return;
    const it = its[0], s0 = stateAt(doc, it.l, clip, t);
    const [ax, ay] = toS(it.x, it.y);
    const [cx0, cy0] = pointer(e);
    const a0 = Math.atan2(cy0 - ay, cx0 - ax);
    live.begin();
    follow(ev => {
      const [cx, cy] = pointer(ev);
      let r = s0.rot + (Math.atan2(cy - ay, cx - ax) - a0) * 180 / Math.PI;
      r = ev.shiftKey ? Math.round(r / 15) * 15 : Math.round(r * 10) / 10;
      setDrag({ kind: 'rotate', angle: r });
      live.set(d => setProp(d, clip, d.layers.find(q => q.id === it.l.id), 'rot', r, t));
    }, () => setDrag(null));
  }

  // ------------------------------------------------------------------ creacion
  const nextOrder = () => Math.max(0, ...doc.layers.filter(l => !l.parent).map(l => l.base.order || 0)) + 1;
  function startText(e, wx, wy) {
    const hit = [...items].reverse().find(it => it.l.type === 'text' && !it.l.locked && hits(it, wx, wy));
    if (hit) { setSel([hit.l.id]); setEditingText(hit.l.id); return; }
    const [cx0, cy0] = pointer(e);
    follow(ev => {
      const [cx, cy] = pointer(ev);
      setDrag({ kind: 'box', b: { x0: Math.min(cx0, cx), y0: Math.min(cy0, cy), x1: Math.max(cx0, cx), y1: Math.max(cy0, cy) } });
    }, ev => {
      setDrag(null);
      const [cx, cy] = pointer(ev); const [wx1] = toW(cx, cy);
      const box = Math.abs(cx - cx0) > 8;
      const l = newLayer(mode, 'text', { name: 'Texto', text: { ...opts.text, value: '', box: box ? 'fixed' : 'auto', align: box ? opts.text.align : 'left' } });
      Object.assign(l.base, { x: Math.round(Math.min(wx, wx1)), y: Math.round(wy), ax: 0, ay: 0, order: nextOrder(), w: box ? Math.round(Math.abs(wx1 - wx)) : 10 });
      l.text.value = '';
      fitText(l);
      edit(d => { d.layers.push(l); });
      setSel([l.id]); setEditingText(l.id);
    });
  }

  function startShape(e, wx0, wy0, kind) {
    follow(ev => {
      const [cx, cy] = pointer(ev); let [wx, wy] = toW(cx, cy);
      setDrag({ kind: 'shape', shape: kind, a: [wx0, wy0], b: shapeEnd(wx0, wy0, wx, wy, ev, kind) });
    }, ev => {
      setDrag(null);
      const [cx, cy] = pointer(ev); const [wx1, wy1] = shapeEnd(wx0, wy0, ...toW(cx, cy), ev, kind);
      if (Math.abs(wx1 - wx0) * z < 3 && Math.abs(wy1 - wy0) * z < 3) return;
      const shape = { ...JSON.parse(JSON.stringify(opts.shape)), kind };
      const l = newLayer(mode, 'shape', { name: { rect: 'Rectangulo', ellipse: 'Elipse', polygon: 'Poligono', star: 'Estrella', line: 'Linea' }[kind], shape });
      if (kind === 'line') {
        const len = Math.hypot(wx1 - wx0, wy1 - wy0), sw = Math.max(1, shape.stroke.width || 4);
        if (!shape.stroke.width) l.shape.stroke = { ...l.shape.stroke, width: 4, color: [...(shape.fill.color || [255, 255, 255])] };
        Object.assign(l.base, { x: Math.round((wx0 + wx1) / 2), y: Math.round((wy0 + wy1) / 2), w: Math.round(len), h: Math.ceil(sw + 2),
          rot: Math.round(Math.atan2(wy1 - wy0, wx1 - wx0) * 1800 / Math.PI) / 10 });
      } else {
        const ex = ev.altKey ? 2 : 1;
        const x0 = ev.altKey ? wx0 - (wx1 - wx0) : Math.min(wx0, wx1), y0 = ev.altKey ? wy0 - (wy1 - wy0) : Math.min(wy0, wy1);
        const w = Math.abs(wx1 - wx0) * ex, h = Math.abs(wy1 - wy0) * ex;
        Object.assign(l.base, { x: Math.round(x0 + w / 2), y: Math.round(y0 + h / 2), w: Math.round(w), h: Math.round(h) });
        if (ev.altKey) Object.assign(l.base, { x: Math.round(wx0), y: Math.round(wy0) });
      }
      l.base.order = nextOrder();
      edit(d => { d.layers.push(l); });
      setSel([l.id]);
    });
  }
  const shapeEnd = (x0, y0, x1, y1, ev, kind) => {
    if (!ev.shiftKey) return [x1, y1];
    if (kind === 'line') {
      const a = Math.round(Math.atan2(y1 - y0, x1 - x0) / (Math.PI / 4)) * (Math.PI / 4), r = Math.hypot(x1 - x0, y1 - y0);
      return [x0 + Math.cos(a) * r, y0 + Math.sin(a) * r];
    }
    const m = Math.max(Math.abs(x1 - x0), Math.abs(y1 - y0));
    return [x0 + Math.sign(x1 - x0 || 1) * m, y0 + Math.sign(y1 - y0 || 1) * m];
  };

  // pluma: clic = punto de esquina, arrastrar = punto suave con tiradores, clic en el primero = cerrar
  function penDown(e, wx, wy, cx, cy) {
    const pts = pen?.pts || [];
    if (pts.length > 2) {
      const [fx, fy] = toS(pts[0].x, pts[0].y);
      if (Math.hypot(fx - cx, fy - cy) < 9) { finishPen(true); return; }
    }
    const p = { x: wx, y: wy, hx: 0, hy: 0 };
    const next = [...pts, p];
    setPen({ pts: next });
    follow(ev => {
      const [x, y] = toW(...pointer(ev));
      p.hx = x - wx; p.hy = y - wy;
      setPen({ pts: [...next] });
    });
  }
  function finishPen(closed) {
    const pts = pen?.pts || [];
    setPen(null);
    if (pts.length < 2) return;
    const all = pts.flatMap(p => [[p.x, p.y], [p.x + p.hx, p.y + p.hy], [p.x - p.hx, p.y - p.hy]]);
    const b = bboxOf(all);
    const w = Math.max(1, b.x1 - b.x0), h = Math.max(1, b.y1 - b.y0);
    const shape = { ...JSON.parse(JSON.stringify(opts.shape)), kind: 'path', closed,
      points: pts.map(p => [(p.x - b.x0) / w, (p.y - b.y0) / h, -p.hx / w, -p.hy / h, p.hx / w, p.hy / h]) };
    if (!closed) { shape.fill = { ...shape.fill, type: 'none' }; if (!shape.stroke.width) shape.stroke = { ...shape.stroke, width: 4 }; }
    const l = newLayer(mode, 'shape', { name: 'Trazado', shape });
    Object.assign(l.base, { x: Math.round(b.x0 + w / 2), y: Math.round(b.y0 + h / 2), w: Math.round(w), h: Math.round(h), order: nextOrder() });
    edit(d => { d.layers.push(l); });
    setSel([l.id]);
  }

  // ------------------------------------------------------------------ guias (arrastrar desde las reglas)
  function dragGuide(axis, index) {
    live.begin();
    let i = index;
    follow(ev => {
      const [cx, cy] = pointer(ev); const [wx, wy] = toW(cx, cy);
      const v = Math.round(axis === 'x' ? wx : wy);
      live.setProj(p => {
        p.guides ??= { x: [], y: [] };
        if (i == null) { p.guides[axis].push(v); i = p.guides[axis].length - 1; } else p.guides[axis][i] = v;
      });
    }, ev => {
      const [cx, cy] = pointer(ev);
      if ((axis === 'x' ? cy : cx) < RULER && i != null) live.setProj(p => { p.guides[axis].splice(i, 1); });
      setHoverGuide(null);
    });
  }
  const onMoveHover = e => {
    if (readOnly) {
      const [cx, cy] = pointer(e); const [wx, wy] = toW(cx, cy);
      const hit = hitPreview(wx, wy);
      if (hit) onPick?.(hit.l.id, 'hover');
      return;
    }
    if (tool !== 'move' || drag) return;
    const [cx, cy] = pointer(e);
    let g = null;
    (proj.guides?.x || []).forEach((v, i) => { if (Math.abs(toS(v, 0)[0] - cx) < 4) g = { axis: 'x', i }; });
    (proj.guides?.y || []).forEach((v, i) => { if (Math.abs(toS(0, v)[1] - cy) < 4) g = { axis: 'y', i }; });
    if (g?.axis !== hoverGuide?.axis || g?.i !== hoverGuide?.i) setHoverGuide(g);
  };

  // ------------------------------------------------------------------ dibujo
  const one = sel.length === 1 ? items.find(it => it.l.id === sel[0]) : null;
  const selBoxes = selItems().map(it => ({ it, pts: it.group ? null : corners(it), box: itemBox(items, it, doc) }));
  const multiBox = sel.length > 1 ? unionBox(selBoxes.map(b => b.box)) : null;
  const cursor = space || tool === 'hand' ? 'grab' : tool === 'zoom' ? 'zoom-in' : tool === 'text' ? 'text'
    : SHAPE_TOOLS[tool] || tool === 'pen' ? 'crosshair' : hoverGuide ? (hoverGuide.axis === 'x' ? 'col-resize' : 'row-resize') : 'default';
  const S = ([x, y]) => toS(x, y);
  const poly = pts => pts.map(p => S(p).join(',')).join(' ');
  const editing = editingText && items.find(it => it.l.id === editingText);

  return (
    <section className={'ue-view' + (readOnly ? ' ro' : '')} ref={wrap} style={{ cursor }}
      onPointerDown={onDown} onDoubleClick={onDouble} onPointerMove={onMoveHover} onContextMenu={e => e.preventDefault()}>
      <div className={'ue-board bg-' + (opts.bg || 'dark') + (!readOnly && opts.outside !== false ? ' open' : '')} style={{ left: view.x, top: view.y, width: W * z, height: H * z }}>
        {refImg && <img className="ue-ref" src={refImg} alt="" style={{ opacity: refAlpha }} />}
        {items.map(it => {
          if (it.group || it.l.id === editingText) return null;
          const ov = overrides?.(it) || null;
          if (ov?.hide) return null;
          const { l, s } = it;
          const k = it.k * z;
          const alpha = Math.max(0, Math.min(1, it.alpha * (ov?.alpha ?? 1)));
          if (alpha <= 0.002 && !readOnly) return <div key={l.id} />;
          // logica (wui): visible, opacidad, tinte, texto y efectos
          const vm = ov?.vm || null;
          if (vm && !vm.visible) return null;
          let tint = ov?.tint || s.color;
          if (vm?.tint) {
            const h = vm.tint.replace('#', ''), m = [0, 2, 4].map(i => (parseInt(h.slice(i, i + 2), 16) || 0) / 255);
            tint = (tint || [255, 255, 255]).map((v, i) => v * m[i]);
          }
          const lv = vm?.text != null && l.type === 'text' ? { ...l, text: { ...l.text, value: vm.text } } : l;
          const css = vm ? lookCss(vm, z) : null;
          // grano, parpadeo y temblor: cambian 24 veces por segundo (como en el juego)
          const seed = Math.floor((t || 0) * 24);
          const flick = s.e_flicker > 0 ? 1 - s.e_flicker * rnd(l.id, seed, 1) : 1;
          const shake = s.e_shake > 0 ? [(rnd(l.id, seed, 2) * 2 - 1) * s.e_shake * k, (rnd(l.id, seed, 3) * 2 - 1) * s.e_shake * k] : null;
          const grainId = s.e_grain > 0 ? `gr_${l.id}` : null;
          const filt = [liveCss(s, k), grainId && `url(#${grainId})`].filter(Boolean).join(' ') || undefined;
          const blend = BLEND_CSS[l.blend] || undefined;
          const el = (
            <div className="ue-layer" style={{ left: it.x * z, top: it.y * z, opacity: alpha * (vm ? vm.alpha : 1) * flick,
              transform: `${shake ? `translate(${shake[0]}px, ${shake[1]}px) ` : ''}rotate(${it.rot}deg)`, transformOrigin: '0 0', filter: filt,
              mixBlendMode: !ov?.fx && !vm ? blend : undefined }}>
              {grainId && <svg width="0" height="0" style={{ position: 'absolute' }}><filter id={grainId} x="0" y="0" width="100%" height="100%">
                <feTurbulence type="fractalNoise" baseFrequency="0.85" numOctaves="1" seed={seed} result="n" />
                <feColorMatrix in="n" type="saturate" values="0" result="g" />
                <feComposite in="g" in2="SourceAlpha" operator="in" result="m" />
                <feComponentTransfer in="m" result="a"><feFuncA type="linear" slope={Math.min(1, s.e_grain)} /></feComponentTransfer>
                <feMerge><feMergeNode in="SourceGraphic" /><feMergeNode in="a" /></feMerge>
              </filter></svg>}
              <div style={{ position: 'absolute', left: -(s.ax ?? 0.5) * s.w * k, top: -(s.ay ?? 0.5) * s.h * k, width: s.w * k, height: s.h * k,
                clipPath: css?.clipPath }}>
                <LayerCanvas l={lv} w={s.w} h={s.h} scale={k} tint={tint} urlOf={urlOf} />
              </div>
            </div>);
          const cl = ov?.clip;   // caja de la lista: recorta lo que sale
          const clipCss = cl ? `polygon(${cl.x0 * z}px ${cl.y0 * z}px, ${cl.x1 * z}px ${cl.y0 * z}px, ${cl.x1 * z}px ${cl.y1 * z}px, ${cl.x0 * z}px ${cl.y1 * z}px)` : null;
          const clipWrap = node => clipCss ? <div key={l.id} className="ue-fxwrap" style={{ clipPath: clipCss, mixBlendMode: blend }}>{node}</div> : node;
          if (!ov?.fx && !vm) return clipCss ? clipWrap(el) : <React.Fragment key={l.id}>{el}</React.Fragment>;
          const c = bboxOf(corners(it));
          const origin = `${(c.x0 + c.x1) / 2 * z}px ${(c.y0 + c.y1) / 2 * z}px`;
          const focusFx = ov?.fx ? `translate(${ov.fx.dx * z}px, ${ov.fx.dy * z}px) scale(${ov.fx.s})` : '';
          return clipWrap(<div key={l.id} className="ue-fxwrap" style={{ transformOrigin: origin, mixBlendMode: clipCss ? undefined : blend,
            transform: [focusFx, css?.transform].filter(Boolean).join(' ') || undefined, filter: css?.filter }}>{el}</div>);
        })}
        {!readOnly && opts.outside !== false && <div className="ue-outside" title="" />}
        {opts.grid && !readOnly && <div className="ue-grid" style={{ backgroundSize: `${opts.gridSize * z}px ${opts.gridSize * z}px` }} />}
      </div>
      {!readOnly && <svg className="ue-overlay" width={size.w} height={size.h}>
        {opts.guides !== false && (proj.guides?.x || []).map((v, i) => <line key={'gx' + i} className="ue-guide" x1={toS(v, 0)[0]} x2={toS(v, 0)[0]} y1={0} y2={size.h} />)}
        {opts.guides !== false && (proj.guides?.y || []).map((v, i) => <line key={'gy' + i} className="ue-guide" y1={toS(0, v)[1]} y2={toS(0, v)[1]} x1={0} x2={size.w} />)}
        {listBoxes.map(L => { const [x0, y0] = S([L.box.x0, L.box.y0]), [x1, y1] = S([L.box.x1, L.box.y1]);
          return <g key={'ls' + L.id} className="ue-listbox"><rect x={x0} y={y0} width={x1 - x0} height={y1 - y0} />
            <text x={x0 + 4} y={y0 - 5}>{`${L.name || 'Lista'}: ${L.cfg.view} de ${L.n}${L.cfg.loop ? ', infinita' : ''}`}</text></g>; })}
        {opts.showButtons && items.filter(it => it.l.ui?.focus && !it.group).map(it => <polygon key={'b' + it.l.id} className="ue-btnmark" points={poly(corners(it))} />)}
        {selBoxes.map(({ it, pts, box }) => pts
          ? <polygon key={it.l.id} className="ue-selpoly" points={poly(pts)} />
          : <rect key={it.l.id} className="ue-selpoly" x={S([box.x0, 0])[0]} y={S([0, box.y0])[1]} width={(box.x1 - box.x0) * z} height={(box.y1 - box.y0) * z} />)}
        {one && !one.group && !one.l.locked && (() => {
          const pts = corners(one);
          const mid = (a, b) => [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2];
          const hp = [pts[0], mid(pts[0], pts[1]), pts[1], mid(pts[1], pts[2]), pts[2], mid(pts[2], pts[3]), pts[3], mid(pts[3], pts[0])];
          return <>
            {[0, 2, 4, 6].map(i => { const [x, y] = S(hp[i]); return <circle key={'r' + i} className="ue-rot" cx={x} cy={y} r={14} onPointerDown={rotate} />; })}
            {hp.map((p, i) => { const [x, y] = S(p); const [hx, hy] = HANDLES[i];
              const rotIdx = ((Math.round(one.rot / 45) % 8) + 8) % 8, ci = HANDLES[(i + rotIdx) % 8].join(',');
              return <rect key={'h' + i} className="ue-handle" x={x - 4} y={y - 4} width={8} height={8} style={{ cursor: CURSOR[ci] }} onPointerDown={e => resize(e, hx, hy)} />; })}
            {(() => { const [x, y] = toS(one.x, one.y); return <g className="ue-anchor"><circle cx={x} cy={y} r={4} /><line x1={x - 7} x2={x + 7} y1={y} y2={y} /><line x1={x} x2={x} y1={y - 7} y2={y + 7} /></g>; })()}
          </>;
        })()}
        {multiBox && <>
          <rect className="ue-multibox" x={S([multiBox.x0, 0])[0]} y={S([0, multiBox.y0])[1]} width={(multiBox.x1 - multiBox.x0) * z} height={(multiBox.y1 - multiBox.y0) * z} />
          {mode !== '25d' && HANDLES.map(([hx, hy], i) => {
            const x = toS(hx < 0 ? multiBox.x0 : hx > 0 ? multiBox.x1 : (multiBox.x0 + multiBox.x1) / 2, 0)[0];
            const y = toS(0, hy < 0 ? multiBox.y0 : hy > 0 ? multiBox.y1 : (multiBox.y0 + multiBox.y1) / 2)[1];
            return <rect key={'m' + i} className="ue-handle" x={x - 4} y={y - 4} width={8} height={8} style={{ cursor: CURSOR[`${hx},${hy}`] }} onPointerDown={e => resize(e, hx, hy)} />;
          })}
        </>}
        {drag?.kind === 'marquee' && <rect className="ue-marquee" x={drag.b.x0} y={drag.b.y0} width={drag.b.x1 - drag.b.x0} height={drag.b.y1 - drag.b.y0} />}
        {drag?.kind === 'box' && <rect className="ue-marquee" x={drag.b.x0} y={drag.b.y0} width={drag.b.x1 - drag.b.x0} height={drag.b.y1 - drag.b.y0} />}
        {drag?.kind === 'shape' && (() => {
          const [ax, ay] = S(drag.a), [bx, by] = S(drag.b);
          if (drag.shape === 'line') return <line className="ue-draft" x1={ax} y1={ay} x2={bx} y2={by} />;
          if (drag.shape === 'ellipse') return <ellipse className="ue-draft" cx={(ax + bx) / 2} cy={(ay + by) / 2} rx={Math.abs(bx - ax) / 2} ry={Math.abs(by - ay) / 2} />;
          return <rect className="ue-draft" x={Math.min(ax, bx)} y={Math.min(ay, by)} width={Math.abs(bx - ax)} height={Math.abs(by - ay)} />;
        })()}
        {(drag?.lines || []).map((g, i) => g.x !== undefined
          ? <line key={'s' + i} className="ue-snap" x1={toS(g.x, 0)[0]} x2={toS(g.x, 0)[0]} y1={0} y2={size.h} />
          : <line key={'s' + i} className="ue-snap" y1={toS(0, g.y)[1]} y2={toS(0, g.y)[1]} x1={0} x2={size.w} />)}
        {drag?.kind === 'rotate' && one && (() => { const [x, y] = toS(one.x, one.y); return <text className="ue-angle" x={x + 12} y={y - 12}>{drag.angle} grados</text>; })()}
        {pen && pen.pts.length > 0 && <>
          <path className="ue-draft" d={penPath(pen.pts, S)} />
          {pen.pts.map((p, i) => { const [x, y] = S([p.x, p.y]); const [hx, hy] = S([p.x + p.hx, p.y + p.hy]); const [ix, iy] = S([p.x - p.hx, p.y - p.hy]);
            return <g key={i}>{(p.hx || p.hy) ? <><line className="ue-penh" x1={ix} y1={iy} x2={hx} y2={hy} /><circle className="ue-penhd" cx={hx} cy={hy} r={3} /><circle className="ue-penhd" cx={ix} cy={iy} r={3} /></> : null}
              <rect className={'ue-penpt' + (i === 0 ? ' first' : '')} x={x - 4} y={y - 4} width={8} height={8} /></g>; })}
        </>}
      </svg>}
      {!readOnly && <Rulers view={view} size={size} sel={multiBox || (one && bboxOf(corners(one)))} onGuide={axis => dragGuide(axis, null)} />}
      {editing && <TextEditor it={editing} z={z} view={view} edit={edit} done={() => setEditingText(null)} />}
    </section>
  );
}

const penPath = (pts, S) => pts.map((p, i) => {
  const [x, y] = S([p.x, p.y]);
  if (!i) return `M${x},${y}`;
  const a = pts[i - 1];
  const [c1x, c1y] = S([a.x + a.hx, a.y + a.hy]), [c2x, c2y] = S([p.x - p.hx, p.y - p.hy]);
  return `C${c1x},${c1y} ${c2x},${c2y} ${x},${y}`;
}).join(' ');

export function fitView(size, tight) {
  const m = tight ? 0 : 40, r = tight ? 0 : RULER;
  const z = Math.max(0.05, Math.min((size.w - r - m * 2) / W, (size.h - r - m * 2) / H));
  return { z, x: r + (size.w - r - W * z) / 2, y: r + (size.h - r - H * z) / 2, fit: true };
}

// ------------------------------------------------------------------ reglas
function Rulers({ view, size, sel, onGuide }) {
  const top = useRef(null), left = useRef(null);
  useEffect(() => {
    const dpr = window.devicePixelRatio || 1;
    const css = getComputedStyle(document.documentElement);
    const fg = css.getPropertyValue('--faint') || '#666', bg = css.getPropertyValue('--panel') || '#111', hl = 'rgba(227,160,67,.25)';
    const steps = [1, 2, 5, 10, 20, 50, 100, 200, 500, 1000];
    const step = steps.find(s => s * view.z >= 50) || 1000;
    const draw = (c, horiz) => {
      if (!c) return;
      const len = horiz ? size.w : size.h;
      c.width = (horiz ? len : RULER) * dpr; c.height = (horiz ? RULER : len) * dpr;
      const g = c.getContext('2d');
      g.scale(dpr, dpr);
      if (!horiz) g.transform(0, 1, 1, 0, 0, 0);   // dibuja la vertical como la horizontal con los ejes cambiados
      g.fillStyle = bg; g.fillRect(0, 0, len, RULER);
      const o = horiz ? view.x : view.y;
      if (sel) { const a = (horiz ? sel.x0 : sel.y0) * view.z + o, b = (horiz ? sel.x1 : sel.y1) * view.z + o; g.fillStyle = hl; g.fillRect(a, 0, b - a, RULER); }
      g.strokeStyle = fg; g.fillStyle = fg; g.font = '9px Inter Variable, sans-serif'; g.lineWidth = 1;
      const from = Math.floor(-o / view.z / step) * step, to = (len - o) / view.z;
      for (let v = from; v <= to; v += step / 5) {
        const p = Math.round(o + v * view.z) + 0.5;
        const major = Math.abs(v / step - Math.round(v / step)) < 1e-6;
        g.beginPath(); g.moveTo(p, RULER); g.lineTo(p, major ? 4 : RULER - 5); g.stroke();
        if (major) {
          if (horiz) g.fillText(String(Math.round(v)), p + 3, 10);
          else { g.save(); g.setTransform(dpr, 0, 0, dpr, 0, 0); g.translate(10, p - 3); g.rotate(-Math.PI / 2); g.fillText(String(Math.round(v)), 0, 0); g.restore(); }
        }
      }
    };
    draw(top.current, true);
    draw(left.current, false);
  }, [view, size, sel]);
  return <>
    <canvas ref={top} className="ue-ruler top" style={{ width: size.w, height: RULER }} onPointerDown={e => { e.stopPropagation(); onGuide('y'); }} title="Arrastra hacia el lienzo para crear una guia" />
    <canvas ref={left} className="ue-ruler left" style={{ width: RULER, height: size.h }} onPointerDown={e => { e.stopPropagation(); onGuide('x'); }} title="Arrastra hacia el lienzo para crear una guia" />
    <div className="ue-rulercorner" />
  </>;
}

// ------------------------------------------------------------------ edicion de texto en el lienzo
function TextEditor({ it, z, view, edit, done }) {
  const ref = useRef(null);
  const t = it.l.text;
  const [val, setVal] = useState(t.value);
  useEffect(() => { ref.current?.focus(); ref.current?.select(); }, []);
  const commit = () => {
    edit(d => { const L = d.layers.find(q => q.id === it.l.id); if (L) { const auto = !L.name || L.name === 'Texto' || L.name === (L.text.value || '').split('\n')[0].slice(0, 32); L.text.value = val; if (auto && val.trim()) L.name = val.trim().split('\n')[0].slice(0, 32); fitText(L); } });
    done();
  };
  const k = it.k * z;
  return <textarea ref={ref} className="ue-textedit" value={val} spellCheck={false}
    onChange={e => setVal(e.target.value)}
    onPointerDown={e => e.stopPropagation()}
    onBlur={commit}
    onKeyDown={e => { e.stopPropagation(); if (e.key === 'Escape' || (e.key === 'Enter' && (e.ctrlKey || e.metaKey))) { e.preventDefault(); commit(); } }}
    style={{ left: view.x + it.x * z, top: view.y + it.y * z, transform: `rotate(${it.rot}deg) translate(${-(it.s.ax ?? 0) * 100}%, ${-(it.s.ay ?? 0) * 100}%)`,
      transformOrigin: '0 0', font: fontCss(t, t.px * k), letterSpacing: `${(t.spacing || 0) * k}px`, lineHeight: t.line || 1.15,
      color: `rgb(${(t.color || [255, 255, 255]).join(',')})`, textAlign: t.align, textTransform: t.upper ? 'uppercase' : 'none',
      width: t.box === 'fixed' ? it.s.w * k : (textSize({ ...t, value: val || ' ' })[0] + t.px) * k,
      height: (textSize({ ...t, value: val || ' ' }, t.box === 'fixed' ? it.s.w : 0)[1] + 4) * k }} />;
}
