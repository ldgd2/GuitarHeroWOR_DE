// Efecto de seleccion de un boton: una imagen (forma generada en SVG, o imagen/SVG propio) que aparece solo
// cuando el boton tiene el foco ("visible solo con el foco de"), colocada detras, delante o a un lado, con una
// animacion en el clip de bucle (latido, giro, parpadeo, vaiven). Es una capa normal: funciona igual en el juego
// y se puede seguir editando (y pasar por el editor de imagen) como cualquier otra.
import React, { useState } from 'react';
import { Icon } from '../ui/icons.jsx';
import { Num } from '../studio/fields.jsx';
import { api } from '../studio/files.js';
import { setKey } from '../studio/keys.js';
import { fileToBase64 } from '../studio/AssetBrowser.jsx';
import { newLayer } from './model.js';
import { Row, Section } from './panels.jsx';

export const SHAPES = [
  ['frame', 'Marco'], ['roundframe', 'Marco redondeado'], ['brackets', 'Esquinas'], ['glowbox', 'Halo difuso'],
  ['radial', 'Brillo radial'], ['underline', 'Subrayado'], ['arrowL', 'Flecha a la izquierda'], ['arrowR', 'Flecha a la derecha'],
  ['circle', 'Circulo'], ['bar', 'Barra de fondo'],
];
const PLACES = [['behind', 'Detras'], ['front', 'Delante'], ['left', 'A la izquierda'], ['right', 'A la derecha'], ['below', 'Debajo']];
const ANIMS = [['none', 'Sin animacion'], ['pulse', 'Latido'], ['spin', 'Giro'], ['blink', 'Parpadeo'], ['bob', 'Vaiven'], ['breathe', 'Respirar (opacidad)']];

/** SVG de la forma, del tamano dado (px) */
export function shapeSvg(kind, w, h, color, stroke) {
  const c = color, s = stroke, r = Math.min(w, h);
  const body = {
    frame: `<rect x="${s / 2}" y="${s / 2}" width="${w - s}" height="${h - s}" fill="none" stroke="${c}" stroke-width="${s}"/>`,
    roundframe: `<rect x="${s / 2}" y="${s / 2}" width="${w - s}" height="${h - s}" rx="${r * 0.25}" fill="none" stroke="${c}" stroke-width="${s}"/>`,
    brackets: (() => { const L = r * 0.28, o = s / 2;
      return `<path d="M${o} ${L}V${o}H${L} M${w - L} ${o}H${w - o}V${L} M${w - o} ${h - L}V${h - o}H${w - L} M${L} ${h - o}H${o}V${h - L}" fill="none" stroke="${c}" stroke-width="${s}" stroke-linecap="square"/>`; })(),
    glowbox: `<defs><filter id="b" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="${r * 0.08}"/></filter></defs><rect x="${r * 0.18}" y="${r * 0.18}" width="${w - r * 0.36}" height="${h - r * 0.36}" rx="${r * 0.1}" fill="${c}" opacity="0.8" filter="url(#b)"/>`,
    radial: `<defs><radialGradient id="g"><stop offset="0" stop-color="${c}" stop-opacity="0.9"/><stop offset="1" stop-color="${c}" stop-opacity="0"/></radialGradient></defs><ellipse cx="${w / 2}" cy="${h / 2}" rx="${w / 2}" ry="${h / 2}" fill="url(#g)"/>`,
    underline: `<rect x="0" y="0" width="${w}" height="${h}" rx="${h / 2}" fill="${c}"/>`,
    arrowL: `<path d="M0 0L${w} ${h / 2}L0 ${h}Z" fill="${c}"/>`,
    arrowR: `<path d="M${w} 0L0 ${h / 2}L${w} ${h}Z" fill="${c}"/>`,
    circle: `<ellipse cx="${w / 2}" cy="${h / 2}" rx="${w / 2 - s / 2}" ry="${h / 2 - s / 2}" fill="none" stroke="${c}" stroke-width="${s}"/>`,
    bar: `<defs><linearGradient id="h"><stop offset="0" stop-color="${c}" stop-opacity="0"/><stop offset="0.5" stop-color="${c}" stop-opacity="0.85"/><stop offset="1" stop-color="${c}" stop-opacity="0"/></linearGradient></defs><rect width="${w}" height="${h}" fill="url(#h)"/>`,
  }[kind];
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="0 0 ${w} ${h}">${body}</svg>`;
}
const b64 = t => btoa(unescape(encodeURIComponent(t)));

export function SelFx({ proj, doc, l, edit }) {
  const mine = doc.layers.filter(q => q.showOnFocus === l.id);
  const [o, setO] = useState({ src: 'shape', shape: 'brackets', color: '#ffd060', stroke: 6, place: 'behind', margin: 16, anim: 'pulse', amount: 1, period: 1 });
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState('');
  const set = (k, v) => setO(x => ({ ...x, [k]: v }));

  const add = async file => {
    setBusy(true); setErr('');
    try {
      const B = l.base, side = ['left', 'right'].includes(o.place), under = o.place === 'below';
      // tamano del efecto
      let w, h;
      if (side) { h = Math.round(B.h * 0.6); w = Math.round(h * 0.8); }
      else if (under) { w = Math.round(B.w); h = Math.max(4, Math.round(o.stroke)); }
      else { w = Math.round(B.w + o.margin * 2); h = Math.round(B.h + o.margin * 2); }
      let image;
      if (o.src === 'shape') {
        const kind = side && !o.shape.startsWith('arrow') ? (o.place === 'left' ? 'arrowL' : 'arrowR') : o.shape;
        const svg = shapeSvg(kind, w, h, o.color, o.stroke);
        const r = await api('/api/ui/upload', { pid: proj.id, filename: `sel_${kind}_${w}x${h}_${o.color.slice(1)}_${o.stroke}.svg`, data: b64(svg) });
        image = { custom: true, file: r.file, name: r.file, svg: true, w, h };
      } else {
        const r = await api('/api/ui/upload', { pid: proj.id, filename: file.name, data: await fileToBase64(file) });
        image = { custom: true, file: r.file, name: r.file, svg: r.kind === 'svg', w: r.w, h: r.h };
        if (r.w && r.h) { const k = side ? h / r.h : Math.max(w / r.w, h / r.h); if (!under) { w = Math.round(r.w * k); h = Math.round(r.h * k); } }
      }
      const gap = 8;
      const x = o.place === 'left' ? B.x - B.w / 2 - gap - w / 2 : o.place === 'right' ? B.x + B.w / 2 + gap + w / 2 : B.x;
      const y = under ? B.y + B.h / 2 + gap + h / 2 : B.y;
      edit(d => {
        const L = newLayer('2d', 'image', { name: `${l.name} seleccion`, parent: l.parent || null, image, showOnFocus: l.id,
          base: { x, y, w, h, order: (l.base.order || 0) + (o.place === 'behind' ? -0.5 : 0.5) } });
        d.layers.push(L);
        const loop = d.clips.loop;
        if (loop && o.anim !== 'none') {
          const T = loop.duration || 2, n = Math.max(1, Math.round(T / Math.max(0.1, o.period))), p = T / n, a = o.amount;
          const key = (prop, vals) => { for (let i = 0; i <= n; i++) vals.forEach(([f, v]) => { const t = i * p + f * p; if (t <= T + 1e-6) setKey(loop, L.id, prop, Math.min(T, t), v); }); };
          if (o.anim === 'pulse') key('scale', [[0, 1], [0.5, 1 + 0.08 * a]]);
          if (o.anim === 'blink') key('alpha', [[0, 1], [0.5, Math.max(0, 1 - 0.8 * a)]]);
          if (o.anim === 'breathe') key('alpha', [[0, 1], [0.5, Math.max(0, 1 - 0.5 * a)]]);
          if (o.anim === 'bob') key(side ? 'x' : 'y', [[0, side ? x : y], [0.5, (side ? x : y) + (o.place === 'left' ? -8 : 8) * a]]);
          if (o.anim === 'spin') { setKey(loop, L.id, 'rot', 0, 0); setKey(loop, L.id, 'rot', T, 360 * n); }
          for (const ks of Object.values(loop.tracks[L.id] || {})) for (const k of ks) k.ease = o.anim === 'spin' ? 'linear' : 'sineInOut';
        }
      });
    } catch (e) { setErr(e.message); } finally { setBusy(false); }
  };
  const pickFile = () => {
    const i = document.createElement('input');
    i.type = 'file'; i.accept = '.png,.jpg,.jpeg,.webp,.svg';
    i.onchange = () => i.files[0] && add(i.files[0]);
    i.click();
  };
  const removeOne = id => edit(d => { d.layers = d.layers.filter(q => q.id !== id); for (const c of Object.values(d.clips)) delete c.tracks[id]; });

  return (
    <Section title="Efecto de seleccion" icon="target" open={mine.length > 0}>
      <div className="st-muted ue-scripthelp">Una imagen que aparece cuando este boton tiene el foco. Es una capa mas: puedes moverla, editarla o animarla luego.</div>
      {mine.map(q => <div key={q.id} className="ue-selfxitem"><Icon n="tex" /><span>{q.name}</span>
        <button className="st-icon" title="Quitar el efecto" onClick={() => removeOne(q.id)}><Icon n="trash" /></button></div>)}
      <Row label="Origen"><select value={o.src} onChange={e => set('src', e.target.value)}>
        <option value="shape">Forma (SVG generado)</option><option value="file">Imagen o SVG propio</option></select></Row>
      {o.src === 'shape' && <>
        <Row label="Forma"><select value={o.shape} onChange={e => set('shape', e.target.value)}>{SHAPES.map(([k, v]) => <option key={k} value={k}>{v}</option>)}</select></Row>
        <div className="ue-pair">
          <Row label="Color"><input type="color" value={o.color} onChange={e => set('color', e.target.value)} /></Row>
          <Row label="Grosor"><Num v={o.stroke} min={1} max={60} onChange={v => set('stroke', v)} /></Row>
        </div>
      </>}
      <div className="ue-pair">
        <Row label="Posicion"><select value={o.place} onChange={e => set('place', e.target.value)}>{PLACES.map(([k, v]) => <option key={k} value={k}>{v}</option>)}</select></Row>
        <Row label="Margen"><Num v={o.margin} min={-200} max={300} onChange={v => set('margin', v)} /></Row>
      </div>
      <Row label="Animacion"><select value={o.anim} onChange={e => set('anim', e.target.value)}>{ANIMS.map(([k, v]) => <option key={k} value={k}>{v}</option>)}</select></Row>
      {o.anim !== 'none' && <div className="ue-pair">
        <Row label="Intensidad"><Num v={o.amount} step={0.1} min={0.1} max={5} onChange={v => set('amount', v)} /></Row>
        <Row label="Ciclo (s)"><Num v={o.period} step={0.1} min={0.2} max={10} onChange={v => set('period', v)} /></Row>
      </div>}
      <button className="primary" disabled={busy} onClick={() => (o.src === 'file' ? pickFile() : add())}>
        <Icon n={o.src === 'file' ? 'upload' : 'plus'} />{busy ? 'Creando...' : o.src === 'file' ? 'Elegir imagen y crear' : 'Crear efecto'}</button>
      {err && <div className="ue-imgederr"><Icon n="warn" />{err}</div>}
    </Section>
  );
}
