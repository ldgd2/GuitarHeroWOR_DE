// Vista de las interfaces 3D: la pantalla en la Viewport3D compartida. Cada capa es un "soporte" (posicion, giro en
// grados XYZ, escala) con un plano dentro (ancho x alto + el margen de sus estilos, desplazado segun el ancla). La
// imagen de cada plano la dibuja studio/raster.js (la misma que recibe el juego). Las camaras son PerspectiveCamera;
// la activa (cortes) se ve en la vista pequena o a pantalla completa (C).
// Unidades: 1 = 1 px de una pantalla de 1280 x 720 en z = 0 vista con la camara por defecto. Igual que modes.py.
import React, { useEffect, useRef, useState } from 'react';
import * as THREE from 'three';
import { Icon } from '../ui/icons.jsx';
import { Viewport3D } from '../studio/Viewport3D.js';
import { useRasterVersion, cachedRaster } from '../studio/LayerCanvas.jsx';
import { activeCamAt, ancestors, setProp, stateAt } from './model.js';
import { liveCss } from './logic.js';
import { layerUrl } from './view2d.js';

const D2R = Math.PI / 180, R2D = 180 / Math.PI;
const EDIT_LAYER = 1;   // capa de three.js solo para la camara de edicion (iconos de camara, ayudas)

export function Stage3D({ proj, doc, clip, t, sel, setSel, live, preview }) {
  const canvas = useRef(null);
  const V = useRef(null);
  const objs = useRef(new Map());
  const tex = useRef(new Map());          // rasterKey -> {tx, raster}
  const cur = useRef({});                 // ultimo estado (los callbacks de three no ven el de React)
  const [tool, setTool] = useState('translate');
  const [look, setLook] = useState(preview ? 'program' : 'edit');
  const ver = useRasterVersion();
  const selId = sel?.[0] || null;
  cur.current = { doc, clip, t, selId, setSel, sel: sel || [] };

  // ---- montaje de la vista
  useEffect(() => {
    const v = new Viewport3D(canvas.current, {
      onPick: (o, e) => {
        if (preview) return;
        const id = o?.userData?.lid, cs = cur.current.sel;
        if (e && (e.shiftKey || e.ctrlKey || e.metaKey)) {       // Mayus o Ctrl: anadir / quitar
          if (id) cur.current.setSel(cs.includes(id) ? cs.filter(x => x !== id) : [...cs, id]);
          return;
        }
        cur.current.setSel(id ? [id] : []);
      },
      onGizmoStart: () => live?.begin(),
      onGizmo: o => {
        if (!o || !live) return;
        const { clip: c, t: tt } = cur.current;
        const lid = o.userData.lid;
        live.set(d => {
          const L = d.layers.find(x => x.id === lid);
          if (!L) return;
          const r = x => Math.round(x * 100) / 100;
          const set = (k, val) => setProp(d, c, L, k, r(val), tt);
          set('x', o.position.x); set('y', o.position.y); set('z', o.position.z);
          set('rx', o.rotation.x * R2D); set('ry', o.rotation.y * R2D); set('rz', o.rotation.z * R2D);
          if (L.type !== 'camera') set('scale', Math.max(0.01, o.scale.x));
        });
      },
      onGizmoEnd: () => {},
    });
    v.editCam.layers.enable(EDIT_LAYER);
    v.root = new THREE.Group(); v.scene.add(v.root);
    const frame = new THREE.LineSegments(new THREE.EdgesGeometry(new THREE.PlaneGeometry(1280, 720)), new THREE.LineBasicMaterial({ color: 0x3a4656 }));
    frame.layers.set(EDIT_LAYER); v.scene.add(frame);
    V.current = v;
    v.orbit.target.set(0, 0, 0); v.editCam.position.set(700, 350, 1700);
    if (preview) { v.look = 'program'; v.inset = false; }
    return () => { v.dispose(); objs.current.clear(); for (const e of tex.current.values()) e.tx.dispose(); tex.current.clear(); };
  }, []);

  // ---- teclado de la vista (G/R/S, F encuadrar, C camara)
  useEffect(() => {
    if (preview) return;
    const onKey = e => {
      if (/INPUT|SELECT|TEXTAREA/.test(e.target.tagName) || e.ctrlKey || e.metaKey || e.altKey) return;
      const k = e.key.toLowerCase();
      if (k === 'g') setTool('translate'); else if (k === 'r') setTool('rotate'); else if (k === 's') setTool('scale');
      else if (k === 'c') setLook(l => (l === 'edit' ? 'program' : 'edit'));
      else if (k === 'f') frameSel();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  });
  useEffect(() => { V.current?.setMode(tool); }, [tool]);
  useEffect(() => { if (V.current) V.current.look = look; }, [look]);

  const frameSel = () => {
    const o = objs.current.get(cur.current.selId);
    if (o) V.current.frame(o.holder.getWorldPosition(new THREE.Vector3()), 1400);
    else V.current.frame(new THREE.Vector3(0, 0, 0), 1700);
  };

  const texFor = (l, w, h) => {
    const r = cachedRaster(l, w, h, 2, x => layerUrl(proj, x));
    let e = tex.current.get(r);
    if (!e) {
      const tx = new THREE.CanvasTexture(r.canvas); tx.colorSpace = THREE.SRGBColorSpace;
      e = { tx, r }; tex.current.set(r, e);
      if (tex.current.size > 300) { const [k, old] = tex.current.entries().next().value; old.tx.dispose(); tex.current.delete(k); }
    }
    return e;
  };

  // con efectos en vivo (e_*): la textura pasada por el mismo filtro CSS que el lienzo 2D (y que re-wor)
  const fxTex = useRef(new Map());
  const texLive = (l, w, h, s) => {
    const base = texFor(l, w, h);
    const f = liveCss(s, 2);
    if (!f || !base.r.ready && l.type === 'image') return base;
    const key = `${l.id}|${w}x${h}|${f}`;
    let e = fxTex.current.get(key);
    if (e && e.src === base.r.canvas) return e;
    const extra = Math.ceil((s.e_glow || 0) * 1.5 + (s.e_blur || 0) + (s.e_chroma || 0)) + 2;
    const src = base.r.canvas, c = document.createElement('canvas');
    c.width = src.width + extra * 4; c.height = src.height + extra * 4;
    const g = c.getContext('2d');
    g.filter = f;
    g.drawImage(src, extra * 2, extra * 2);
    const tx = new THREE.CanvasTexture(c); tx.colorSpace = THREE.SRGBColorSpace;
    e?.tx.dispose();
    e = { tx, r: { ...base.r, pad: base.r.pad + extra }, src };
    fxTex.current.set(key, e);
    if (fxTex.current.size > 120) { const [k, old] = fxTex.current.entries().next().value; old.tx.dispose(); fxTex.current.delete(k); }
    return e;
  };

  // ---- pantalla -> escena (cada cambio de pantalla, clip, tiempo o imagen cargada)
  useEffect(() => {
    const v = V.current;
    if (!v) return;
    const seen = new Set();
    const make = l => {
      let o = objs.current.get(l.id);
      if (o && o.type !== l.type) { o.holder.removeFromParent(); o.helper?.removeFromParent(); objs.current.delete(l.id); o = null; }
      if (o) return o;
      if (l.type === 'camera') {
        const cam = new THREE.PerspectiveCamera(50, 16 / 9, 1, 5000);
        const icon = new THREE.Mesh(new THREE.ConeGeometry(28, 60, 4), new THREE.MeshBasicMaterial({ color: 0xe3a043, wireframe: true }));
        icon.rotation.x = Math.PI / 2; icon.layers.set(EDIT_LAYER); icon.userData.owner = cam;
        cam.add(icon);
        const helper = new THREE.CameraHelper(cam); helper.layers.set(EDIT_LAYER);
        v.scene.add(helper);
        o = { type: 'camera', holder: cam, mesh: icon, helper };
      } else if (l.type === 'group') {
        const g = new THREE.Group();
        const icon = new THREE.Mesh(new THREE.OctahedronGeometry(14), new THREE.MeshBasicMaterial({ color: 0x6aa0d8, wireframe: true }));
        icon.layers.set(EDIT_LAYER); icon.userData.owner = g; g.add(icon);
        o = { type: 'group', holder: g, mesh: icon };
      } else {
        const g = new THREE.Group();
        const m = new THREE.Mesh(new THREE.PlaneGeometry(1, 1), new THREE.MeshBasicMaterial({ transparent: true, side: THREE.DoubleSide, depthWrite: false }));
        m.userData.owner = g; g.add(m);
        o = { type: l.type, holder: g, mesh: m };
      }
      o.holder.userData.lid = l.id;
      objs.current.set(l.id, o);
      return o;
    };
    const sorted = [...doc.layers].sort((a, b) => ancestors(doc, a).length - ancestors(doc, b).length);
    v.pickables = [];
    for (const l of sorted) {
      const o = make(l);
      seen.add(l.id);
      const par = l.parent && objs.current.get(l.parent);
      const want = par ? par.holder : v.root;
      if (o.holder.parent !== want) want.add(o.holder);
      const s = stateAt(doc, l, clip, t);
      o.holder.position.set(s.x, s.y, s.z);
      o.holder.rotation.set(s.rx * D2R, s.ry * D2R, s.rz * D2R, 'XYZ');
      o.holder.visible = l.visible !== false;
      if (l.type === 'camera') {
        o.holder.scale.set(1, 1, 1);
        if (o.holder.fov !== s.fov) { o.holder.fov = s.fov; o.holder.updateProjectionMatrix(); }
        o.helper.visible = l.visible !== false && !preview; o.helper.update();
      } else o.holder.scale.setScalar(Math.max(0.0001, s.scale));
      if (o.type !== 'camera' && o.type !== 'group') {
        let alpha = s.alpha;
        for (const a of ancestors(doc, l)) alpha *= stateAt(doc, a, clip, t).alpha;
        const w = Math.max(1, s.w), h = Math.max(1, s.h);
        const { tx, r } = texLive(l, w, h, s);
        o.mesh.scale.set(w + r.pad * 2, h + r.pad * 2, 1);
        o.mesh.position.set((0.5 - s.ax) * w, (s.ay - 0.5) * h, 0);
        const mat = o.mesh.material;
        if (mat.map !== tx) { mat.map = tx; mat.needsUpdate = true; }
        tx.needsUpdate = true;
        mat.color.setRGB(s.color[0] / 255, s.color[1] / 255, s.color[2] / 255, THREE.SRGBColorSpace);
        mat.opacity = Math.max(0, Math.min(1, alpha));
        o.mesh.renderOrder = s.order || 0;
        o.mesh.visible = r.ready || l.type !== 'image';
      }
      v.pickables.push(o.mesh);
    }
    for (const [id, o] of objs.current) if (!seen.has(id)) { o.holder.removeFromParent(); o.helper?.removeFromParent(); objs.current.delete(id); }
    const ac = activeCamAt(doc, clip, t);
    v.program = ac ? objs.current.get(ac.id)?.holder || null : null;
    const so = objs.current.get(selId);
    const L = doc.layers.find(l => l.id === selId);
    if (!preview) v.attach(so && L && !L.locked && !v.tc.dragging ? so.holder : v.tc.dragging ? v.tc.object : null);
  }, [doc, clip, t, selId, ver]);

  const ac = activeCamAt(doc, clip, t);
  return (
    <section className={'ue-view ue-3d' + (preview ? ' ro' : '')}>
      <canvas ref={canvas} />
      {!preview && <>
        <div className="st-3dtools">
          {[['translate', 'G', 'Mover'], ['rotate', 'R', 'Girar'], ['scale', 'S', 'Escalar']].map(([m, key, label]) =>
            <button key={m} className={tool === m ? 'on' : ''} onClick={() => setTool(m)} title={`${label} (${key})`}>{label}</button>)}
          <span className="st-sep" />
          <button className={look === 'edit' ? 'on' : ''} onClick={() => setLook('edit')} title="Vista libre para editar (C alterna)">Edicion</button>
          <button className={look === 'program' ? 'on' : ''} onClick={() => setLook('program')} title="Lo que vera el juego: la camara activa (C alterna)"><Icon n="cam" s={14} />Camara</button>
          <button onClick={frameSel} title="Encuadrar la capa elegida (F)"><Icon n="fit" s={14} /></button>
        </div>
        <div className="st-stageinfo">{ac ? `Camara activa: ${ac.name}` : 'Sin camara: anade una'} &nbsp; {clip === 'base' ? 'Base: los cambios son el estado fijo' : 'Clip: mover o girar crea claves en el cursor'}</div>
      </>}
    </section>
  );
}
