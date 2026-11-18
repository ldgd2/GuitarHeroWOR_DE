// Propiedades de un grupo como lista con desplazamiento (ver lists.js)
import React from 'react';
import { Icon } from '../ui/icons.jsx';
import { Num } from '../studio/fields.jsx';
import { childrenOf, descendants } from './model.js';
import { LIST_DEFAULT, boxOf, listCfg, listsOf } from './lists.js';
import { Check, Row, Section } from './panels.jsx';

export function ListProps({ doc, layer, setL, edit }) {
  const cfg = listCfg(layer);
  const set = (k, v) => setL(L => { L.list = { ...LIST_DEFAULT, ...(L.list || {}), [k]: v }; });
  const L = cfg.on ? listsOf(doc).find(q => q.id === layer.id) : null;
  const noFocus = L ? L.items.filter(it => it.focus < 0).length : 0;
  // repartir los elementos a la misma distancia (en el eje de la lista), desde el primero
  const spread = () => edit(d => {
    const ax = cfg.axis === 'x' ? 0 : 1;
    const kids = childrenOf(d, layer.id).filter(q => q.visible !== false)
      .map(q => { const b = boxOf(d, q); return { q, c: ax ? (b.y0 + b.y1) / 2 : (b.x0 + b.x1) / 2 }; }).sort((a, b) => a.c - b.c);
    if (kids.length < 2) return;
    const step = +cfg.step > 0 ? +cfg.step : L?.step || 60;
    kids.forEach((k, i) => { const dv = kids[0].c + i * step - k.c; if (ax) k.q.base.y += dv; else k.q.base.x += dv; });
  });
  // elementos sin boton: el fondo del elemento (la capa visual mas grande) pasa a ser su boton
  const makeButtons = () => edit(d => {
    for (const it of L.items) {
      if (it.focus >= 0) continue;
      const leaves = [...descendants(d, it.id)].map(id => d.layers.find(q => q.id === id))
        .filter(q => q && ['image', 'shape', 'text'].includes(q.type) && q.visible !== false)
        .sort((a, b) => (b.type !== 'text') - (a.type !== 'text') || b.base.w * b.base.h - a.base.w * a.base.h);
      if (leaves[0]) leaves[0].ui = { ...(leaves[0].ui || {}), focus: true };
    }
  });
  return (
    <Section title="Lista con desplazamiento" icon="flow" open={cfg.on}>
      <Check v={cfg.on} label="Los elementos de este grupo forman una lista" onChange={v => set('on', v)} />
      {!cfg.on && <div className="st-muted">Cada hijo directo del grupo es un elemento (una opcion). Solo se ven unos cuantos dentro de una caja; al mover el foco la lista se desplaza y, si es infinita, los de arriba vuelven a entrar por abajo.</div>}
      {cfg.on && <>
        <Row label="Direccion"><select value={cfg.axis} onChange={e => set('axis', e.target.value)}>
          <option value="y">Vertical</option><option value="x">Horizontal</option></select></Row>
        <Row label="Visibles a la vez"><Num v={cfg.view} step={1} min={1} max={50} onChange={v => set('view', Math.round(v))} /></Row>
        <Row label="Separacion (px)" title="0 = la que hay entre los elementos"><Num v={cfg.step} step={1} min={0} max={2000} onChange={v => set('step', v)} /></Row>
        {L && !(+cfg.step > 0) && <div className="st-muted">Separacion automatica: {Math.round(L.step)} px</div>}
        <Check v={cfg.loop} label="Infinita (da la vuelta)" onChange={v => set('loop', v)} />
        <Check v={cfg.center} label="La seleccion se queda en el centro" onChange={v => set('center', v)} />
        <Check v={cfg.fade} label="Desvanecer al salir de la caja" onChange={v => set('fade', v)} />
        <Check v={cfg.clip} label="Recortar a la caja" onChange={v => set('clip', v)} />
        <Check v={cfg.wheel} label="La rueda del raton la mueve" onChange={v => set('wheel', v)} />
        {L && <div className="st-muted">{L.n} elementos, se ven {L.cfg.view}. La caja punteada del lienzo es lo visible.</div>}
        {L && L.cfg.loop && L.n <= L.cfg.view && <div className="ue-warn"><Icon n="warn" s={13} />Para que de la vuelta sin huecos hacen falta mas elementos que visibles.</div>}
        {noFocus > 0 && <div className="ue-warn"><Icon n="warn" s={13} />{noFocus} elementos no tienen boton, asi que el foco no llega a ellos.
          <button onClick={makeButtons}><Icon n="target" s={13} />Hacer botones</button></div>}
        <div className="ue-btnrow"><button onClick={spread} title="Pone los elementos a la misma distancia, desde el primero"><Icon n="distv" s={13} />Repartir elementos</button></div>
      </>}
    </Section>
  );
}
