// Evento de la cancion: Band_PlayClip, Band_PlayLoop y el resto (tabla de parametros).
import { select, S } from '../../core/state.js';
import { evParams, evScr, evTime, hx, K, keyOf, nameOf, qbkey, setVal, val } from '../../core/qb.js';
import { ROLE_ES, ROLES, roleOfKey } from '../../core/roles.js';
import { clipSpan } from '../../song/clips.js';
import { deleteSel } from '../../timeline/edit.js';
import { AnimIn, edit, Field, Note, Num, Select, Text } from '../components/fields.jsx';

function Members({ st }) {
  return (<table className="members"><tbody>{st.struct.map((m, i) => (<tr key={i}>
    <td title={m.n}>{m.label || m.n}</td><td className="muted small">{m.k}</td>
    <td>{['struct', 'array'].includes(m.k) ? (<span className="muted small">{m.k === 'array' ? m.v.values.length + ' elementos' : 'struct'}</span>)
      : (<Text value={m.k === 'qbkey' ? hx(m.v) : m.v} onCommit={edit(v => {
        if (m.k.startsWith('qbkey')) m.v = v.startsWith('0x') ? keyOf(v) : qbkey(v);
        else if (m.k === 'int') m.v = parseInt(v) | 0; else if (m.k === 'float') m.v = parseFloat(v); else m.v = v;
      })} />)}</td></tr>))}</tbody></table>);
}

export function EventItem({ sel }) {
  const P = S.proj, ev = P.events[sel.i]; if (!ev) return null;
  const scr = evScr(ev), p = evParams(ev);
  let body = null;
  if (scr === K.Band_PlayClip) {
    const key = hx(val(p, 'clip', 0)), sp = clipSpan(ev);
    const opts = Object.keys(P.clips).map(k => [k, nameOf(parseInt(k, 16))]);
    if (!P.clips[key]) opts.push([key, key + ' (sin definicion)']);
    body = (<><Field label="Clip"><Select value={key} options={opts} onChange={edit(v => setVal(p, 'clip', 'qbkey', parseInt(v, 16)))} /></Field>
      <Field label="Fotograma inicio"><Num value={sp.sf} onCommit={edit(v => setVal(p, 'startframe', 'int', v))} /></Field>
      <Field label="Fotograma fin"><Num value={sp.ef} onCommit={edit(v => setVal(p, 'endframe', 'int', v))} /></Field>
      <Note>Duracion {(sp.ms / 1000).toFixed(2)} s (30 fotogramas por segundo).</Note>
      {P.clips[key] && (<button onClick={() => select({ type: 'clipdef', key })}>Editar definicion del clip</button>)}</>);
  } else if (scr === K.Band_PlayLoop) {
    const who = roleOfKey(val(p, 'name', 0)) || 'guitarist';
    body = (<><Field label="Musico"><Select value={who} options={ROLES.map(r => [r, ROLE_ES[r]])} onChange={edit(v => setVal(p, 'name', 'qbkey', K[v]))} /></Field>
      <Field label="Anim. hombre"><AnimIn value={val(p, 'male', 0)} onCommit={edit(v => setVal(p, 'male', 'qbkey', v))} /></Field>
      <Field label="Anim. mujer"><AnimIn value={val(p, 'female', 0)} onCommit={edit(v => setVal(p, 'female', 'qbkey', v))} /></Field>
      <Note>Loop de banda (version hombre y mujer). Escribe para buscar entre las animaciones del juego; el sufijo _075, _150... es su tempo y la vista lo ajusta al de la cancion.</Note></>);
  }
  return (<div className="fade">
    <h3>{nameOf(scr)}</h3>
    <Field label="Tiempo (ms)"><Num value={evTime(ev)} onCommit={edit(v => setVal(ev, 'time', 'int', v))} /></Field>
    {body}
    {p && (<><h3>Parametros</h3><Members st={p} /></>)}
    <p><button className="danger" onClick={deleteSel}>Borrar evento</button></p>
  </div>);
}
