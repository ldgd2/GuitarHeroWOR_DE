// Definicion de un clip: musicos que participan y camaras por slot.
import { S } from '../../core/state.js';
import { arr, hx, K, mkStruct, nameOf, setVal, val } from '../../core/qb.js';
import { NODE_OF, ROLE_COLOR, ROLE_ES, ROLES, roleOfKey } from '../../core/roles.js';
import { clipCams, clipChars } from '../../song/clips.js';
import { deleteSel, newChar } from '../../timeline/edit.js';
import { Card, edit, Field, Note, Num, Select } from '../components/fields.jsx';

const skaOpts = (camera, cur) => {
  const list = S.proj.skas.filter(s => s.camera === camera).map(s => [s.id, `${S.animById.get(s.id) || s.label} · ${s.duration.toFixed(1)}s${s.imported ? ' (importado)' : ''}`]);
  return list.some(([id]) => id === hx(cur)) ? list : [[hx(cur), hx(cur) + ' (no esta en el PAK)'], ...list];
};
const roleOpts = ROLES.map(r => [r, ROLE_ES[r]]);

function Musician({ c, i, charsArr }) {
  const role = roleOfKey(val(c, 'name', 0)) || 'vocalist';
  const ik = val(c, 'ik_targetl') === K.slave ? 'slave' : 'guitar';
  const strum = val(c, 'strum') === K.false ? 'false' : 'true';
  return (<Card title={ROLE_ES[role]} color={ROLE_COLOR[role]}
      actions={(<button className="danger small" onClick={edit(() => { charsArr.values.splice(i, 1); if (!charsArr.values.length) charsArr.elem = 'empty'; })}>Quitar</button>)}>
    <Field label="Musico"><Select value={role} options={roleOpts} onChange={edit(v => setVal(c, 'name', 'qbkey', K[v]))} /></Field>
    <Field label="Nodo de salida"><Select value={val(c, 'startnode', role + '_start')} options={ROLES.map(r => [r + '_start', ROLE_ES[r]])}
      onChange={edit(v => setVal(c, 'startnode', 'string', v))} /></Field>
    <Field label="Animacion"><Select value={hx(val(c, 'anim', 0))} options={skaOpts(false, val(c, 'anim', 0))}
      onChange={edit(v => setVal(c, 'anim', 'qbkey', parseInt(v, 16) >>> 0))} /></Field>
    <Field label="Fotogramas"><div className="row2"><Num value={val(c, 'startframe', 0)} onCommit={edit(v => setVal(c, 'startframe', 'int', v))} />
      <Num value={val(c, 'endframe', 0)} onCommit={edit(v => setVal(c, 'endframe', 'int', v))} /></div></Field>
    <Field label="Manos al instrumento" hint="ik_targetl / ik_targetr"><Select value={ik} options={[['guitar', 'si (IK al instrumento)'], ['slave', 'no (manos libres)']]}
      onChange={edit(v => { setVal(c, 'ik_targetl', 'qbkey', K[v]); setVal(c, 'ik_targetr', 'qbkey', K[v]); })} /></Field>
    {role !== 'drummer' && (<Field label="Rasgueo/trastes"><Select value={strum} options={[['true', 'automatico'], ['false', 'lo hace la animacion']]}
      onChange={edit(v => ['strum', 'fret', 'chord'].forEach(k => setVal(c, k, 'qbkey', K[v])))} /></Field>)}
  </Card>);
}

function Camera({ c, i, camsArr }) {
  return (<Card title={'Camara slot ' + val(c, 'slot', i)} color="#9d8cff"
      actions={(<button className="danger small" onClick={edit(() => { camsArr.values.splice(i, 1); if (!camsArr.values.length) camsArr.elem = 'empty'; })}>Quitar</button>)}>
    <Field label="Slot"><Num value={val(c, 'slot', i)} onCommit={edit(v => setVal(c, 'slot', 'int', v))} /></Field>
    <Field label="Nodo"><Select value={val(c, 'name', NODE_OF.vocalist)} options={ROLES.map(r => [NODE_OF[r], ROLE_ES[r]])}
      onChange={edit(v => setVal(c, 'name', 'string', v))} /></Field>
    <Field label="Animacion"><Select value={hx(val(c, 'anim', 0))} options={skaOpts(true, val(c, 'anim', 0))}
      onChange={edit(v => setVal(c, 'anim', 'qbkey', parseInt(v, 16) >>> 0))} /></Field>
  </Card>);
}

export function ClipDef({ sel }) {
  const def = S.proj.clips[sel.key]; if (!def) return null;
  const charsArr = arr(def, 'characters'), camsArr = val(def, 'cameras');
  const addCam = edit(() => {
    const cam = S.proj.skas.find(s => s.camera); if (!cam) return;
    camsArr.elem = 'struct';
    camsArr.values.push(mkStruct([['int', 'slot', camsArr.values.length], ['string', 'name', NODE_OF.vocalist], ['qbkey', 'anim', parseInt(cam.id, 16)]]));
  });
  return (<div className="fade">
    <h3>Clip {nameOf(parseInt(sel.key, 16))}</h3>
    <Note>Que musico participa, desde que nodo, que animacion y que tramo; y las camaras de cada slot (la nota de momento 3+slot las activa).</Note>
    {clipChars(def).map((c, i) => (<Musician key={'c' + i} c={c} i={i} charsArr={charsArr} />))}
    <button onClick={edit(() => { charsArr.elem = 'struct'; charsArr.values.push(newChar('guitarist')); })}>+ Musico</button>
    <h3>Camaras</h3>
    {clipCams(def).map((c, i) => (<Camera key={'k' + i} c={c} i={i} camsArr={camsArr} />))}
    <button onClick={addCam}>+ Camara</button>
    <p><button className="danger" onClick={deleteSel}>Borrar definicion</button></p>
  </div>);
}
