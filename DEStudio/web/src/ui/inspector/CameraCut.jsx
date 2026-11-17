// Corte de camara (pistas autocut / momento) y nota de luces / banda / publico.
import { S } from '../../core/state.js';
import { val } from '../../core/qb.js';
import { AUTO_OK, clipSlotOf, MOMENT_OK } from '../../core/roles.js';
import { activeClips, clipCams } from '../../song/clips.js';
import { deleteSel } from '../../timeline/edit.js';
import { lightTrackOf, trackById } from '../../timeline/layout.js';
import { CUT_OPTIONS, cutName } from '../../core/cutnames.js';
import { edit, Field, Note, Num } from '../components/fields.jsx';

const Del = ({ label = 'Borrar' }) => (<button className="danger" onClick={deleteSel}>{label}</button>);

export function CameraCut({ sel }) {
  const c = S.proj.cameras[sel.track][sel.i]; if (!c) return null;
  const slot = sel.track === 'moment' ? clipSlotOf(c[2]) : null;
  const ok = (sel.track === 'auto' ? AUTO_OK : MOMENT_OK).has(c[2]);
  const orphan = slot !== null && !activeClips(c[0] + c[1] / 2).some(a => clipCams(a.def).some(cm => val(cm, 'slot', -1) === slot));
  return (<div className="fade">
    <h3>Corte de camara · {sel.track === 'auto' ? 'autocut' : 'momento'}</h3>
    <Field label="Inicio (ms)"><Num value={c[0]} onCommit={edit(v => { c[0] = v; })} /></Field>
    <Field label="Duracion (ms)"><Num value={c[1]} onCommit={edit(v => { c[1] = Math.min(65535, v); })} /></Field>
    <Field label="Camara"><select value={c[2]} onChange={edit(e => { c[2] = +e.target.value & 255; })}>
      {[...new Set([...CUT_OPTIONS, c[2]])].sort((a, b) => a - b).map(n => <option key={n} value={n}>{n} · {cutName(n)}</option>)}</select></Field>
    <Field label="Numero"><Num value={c[2]} min={0} max={255} onCommit={edit(v => { c[2] = v & 255; })} /></Field>
    <Note>{sel.track === 'auto' ? 'Corte de camara de escenario: el escenario define cada numero (posicion, lente, movimiento).'
      : slot !== null ? (<>Camara del <b>slot {slot}</b> del clip que suene en ese momento.</>) : 'Plano de escenario dentro de la pista de momento.'}</Note>
    {orphan && (<Note kind="err">No hay un clip con ese slot sonando: en el juego la camara cae bajo el escenario.</Note>)}
    {!ok && (<Note kind="err">Ese numero no existe en esta pista de WoR.</Note>)}
    <Del />
  </div>);
}

export function NoteItem({ sel }) {
  const n = S.proj.notes[sel.track][sel.i]; if (!n) return null;
  return (<div className="fade">
    <h3>Nota · {sel.track === 'lightshow_notes' ? 'Luces · ' + (trackById(lightTrackOf(n[1]))?.name || '') : sel.track === 'anim_notes' ? 'Ordenes a la banda' : 'Publico'}</h3>
    <Field label="Inicio (ms)"><Num value={n[0]} onCommit={edit(v => { n[0] = v; })} /></Field>
    <Field label="Duracion (ms)"><Num value={n[2]} onCommit={edit(v => { n[2] = v; })} /></Field>
    <Field label="Nota"><Num value={n[1]} onCommit={edit(v => { n[1] = v; })} /></Field>
    <Field label="Velocidad"><Num value={n[3]} onCommit={edit(v => { n[3] = v; })} /></Field>
    <Note>Cada nota es una orden del escenario (color, foco, humo, fuego...). El significado lo define el escenario; usa canciones parecidas como referencia.</Note>
    <Del />
  </div>);
}
