// Inspector con varios elementos elegidos en la linea de tiempo.
import { changed, pushUndo, S } from '../../core/state.js';
import { clearSelection, selList } from '../../core/selection.js';
import { copySelection, deleteSelection, duplicateSelection, getLen, getT, movable, nudgeSelection, setLen, setT } from '../../timeline/edit.js';
import { Card, Field, Note, Num } from '../components/fields.jsx';
import { toast } from '../overlay.js';
import { Icon } from '../react.jsx';

const TYPE_ES = { shot: 'toma(s) de camara', fx: 'efecto(s)', prop: 'objeto(s) 3D', pose: 'retoque(s) de pose', cam: 'corte(s) de camara del juego',
  note: 'nota(s)', event: 'evento(s)' };
const RESIZABLE = new Set(['shot', 'fx', 'prop', 'pose', 'cam', 'note']);

export function MultiSel() {
  const list = selList(), mv = movable();
  const by = {};
  for (const x of list) by[x.type] = (by[x.type] || 0) + 1;
  const t0 = Math.min(...list.map(getT)), t1 = Math.max(...list.map(x => getT(x) + getLen(x)));
  const all = fn => () => { pushUndo(); fn(); changed(); };
  return (<div className="fade">
    <Card title={`${list.length} elementos elegidos`} icon="copy" actions={<button className="small" onClick={clearSelection}>Soltar</button>}>
      <ul className="plain">{Object.entries(by).map(([k, n]) => <li key={k}><b>{n}</b> {TYPE_ES[k] || k}</li>)}</ul>
      <p className="muted small">Desde {(t0 / 1000).toFixed(2)} s hasta {(t1 / 1000).toFixed(2)} s{mv.length < list.length ? ` · ${list.length - mv.length} en pistas bloqueadas (no se mueven)` : ''}</p>
      <Field label="Mover (ms)" hint="Positivo: mas tarde. Tambien Alt + flechas (Mayus: de 100 en 100)">
        <Num value={0} step={10} onCommit={v => v && nudgeSelection(v)} /></Field>
      <Field label="Empezar en (s)" hint="Lleva el grupo entero para que el primero empiece aqui">
        <Num value={+(t0 / 1000).toFixed(3)} step={0.1} min={0} onCommit={v => nudgeSelection(Math.round(v * 1000) - t0)} /></Field>
      <Field label="Igualar duracion (s)" hint="Misma duracion para todos los que tienen duracion">
        <Num value="" step={0.1} min={0.05} onCommit={v => v > 0 && all(() => { for (const x of mv) if (RESIZABLE.has(x.type)) setLen(x, v * 1000); })()} /></Field>
      <div className="row">
        <button title="Coloca cada uno justo despues del anterior, sin huecos" onClick={all(() => {
          const ord = [...mv].sort((a, b) => getT(a) - getT(b)); let t = getT(ord[0]);
          for (const x of ord) { setT(x, Math.round(t)); t += getLen(x) || 0; }
        })}>Encadenar</button>
        <button title="Reparte los inicios por igual entre el primero y el ultimo" disabled={mv.length < 3} onClick={all(() => {
          const ord = [...mv].sort((a, b) => getT(a) - getT(b)), a = getT(ord[0]), b = getT(ord.at(-1));
          ord.forEach((x, i) => setT(x, Math.round(a + (b - a) * i / (ord.length - 1))));
        })}>Repartir</button>
        <button title="Todos empiezan en el cursor" onClick={all(() => { for (const x of mv) setT(x, Math.round(S.t)); })}>Al cursor</button>
      </div>
      <div className="row">
        <button onClick={() => { const n = copySelection(); toast(`${n} elemento(s) copiados. Ctrl+V los pega en el cursor.`); }}><Icon n="copy" s={13} />Copiar</button>
        <button onClick={duplicateSelection}><Icon n="duplicate" s={13} />Duplicar despues</button>
        <span className="sep" /><button className="danger" onClick={deleteSelection}><Icon n="trash" s={13} />Borrar {mv.length}</button>
      </div>
    </Card>
    <Note>Ctrl o Mayus + clic: anadir o quitar. Mayus + arrastrar en una zona vacia: recuadro por varias pistas.
      Ctrl+A: todo lo visible. Arrastra cualquiera de los elegidos para mover el grupo. Ctrl+C / Ctrl+V / Ctrl+D,
      Supr para borrar, Esc para soltar.</Note>
  </div>);
}
