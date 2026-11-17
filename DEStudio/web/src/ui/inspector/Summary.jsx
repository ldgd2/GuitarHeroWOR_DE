// Resumen de la cancion y revision de errores (sin nada seleccionado).
import { select, S } from '../../core/state.js';
import { evScr, K, nameOf, val } from '../../core/qb.js';
import { AUTO_OK, clipSlotOf, MOMENT_OK, ROLE_COLOR, ROLE_ES, roleOfKey } from '../../core/roles.js';
import { activeClips, clipCams, clipChars, clipDef } from '../../song/clips.js';

const Check = ({ n, children }) => (<li className={n ? 'err' : 'okc'}><b>{n}</b> {children}</li>);

export function Summary() {
  const P = S.proj;
  const nClip = P.events.filter(ev => evScr(ev) === K.Band_PlayClip).length;
  const orphan = P.cameras.moment.filter(c => {
    const sl = clipSlotOf(c[2]);
    return sl !== null && !activeClips(c[0] + c[1] / 2).some(a => clipCams(a.def).some(cm => val(cm, 'slot', -1) === sl));
  }).length;
  const bad = P.cameras.auto.filter(c => !AUTO_OK.has(c[2])).length + P.cameras.moment.filter(c => !MOMENT_OK.has(c[2])).length;
  const noDef = P.events.filter(ev => evScr(ev) === K.Band_PlayClip && !clipDef(ev)).length;
  return (<div className="fade">
    <h3>{P.song.name}</h3>
    <p className="note">{P.song.editable ? 'Editable (mod)' : (<span className="warn">Juego base: solo referencia, no se puede compilar.</span>)}</p>
    <div className="stats">
      <div><b>{P.events.length}</b><span>eventos</span></div><div><b>{nClip}</b><span>clips</span></div>
      <div><b>{P.cameras.auto.length + P.cameras.moment.length}</b><span>cortes</span></div><div><b>{P.notes.lightshow_notes.length}</b><span>luces</span></div>
    </div>
    <h3>Revision</h3>
    <ul className="checks">
      <Check n={orphan}>camara(s) de clip sin clip sonando (camara bajo el escenario)</Check>
      <Check n={bad}>numero(s) de camara que no existen en su pista</Check>
      <Check n={noDef}>clip(s) sin definicion</Check>
    </ul>
    <h3>Definiciones de clip</h3>
    <div className="list">{Object.keys(P.clips).map(k => (<button key={k} className="item" onClick={() => select({ type: 'clipdef', key: k })}>
      <span>{nameOf(parseInt(k, 16))}</span>
      <span className="tags">{clipChars(P.clips[k]).map((c, i) => { const r = roleOfKey(val(c, 'name', 0)); return (<i key={i} style={{ background: ROLE_COLOR[r] }} title={ROLE_ES[r]}></i>); })}
        <small>{clipCams(P.clips[k]).length} cam</small></span></button>))}</div>
  </div>);
}
