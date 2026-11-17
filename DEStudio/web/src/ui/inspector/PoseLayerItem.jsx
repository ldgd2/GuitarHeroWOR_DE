// Ficha de un retoque de pose (pista "Retoques de pose" de cada musico).
import { S } from '../../core/state.js';
import { ROLE_ES, ROLES } from '../../core/roles.js';
import { EASES } from '../../keyframes/easing.js';
import { kfOpenPose } from '../../keyframes/kf.js';
import { boneName, layerBones, layerOf } from '../../rig/poseLayer.js';
import { seek } from '../playback.js';
import { deleteSel } from '../../timeline/edit.js';
import { Card, edit, Field, Note, Num, Select, Text } from '../components/fields.jsx';
import { Icon } from '../react.jsx';

export function PoseLayerItem({ sel }) {
  const L = layerOf(sel);
  if (!L) return null;
  const bones = [...layerBones(L)];
  return (<div className="fade">
    <Card title={L.name} icon="bone" actions={<button className="primary small" onClick={() => kfOpenPose(L)}><Icon n="keyframe" s={13} />Editar animacion</button>}>
      <Field label="Nombre"><Text value={L.name} onCommit={edit(v => { L.name = v || L.name; })} /></Field>
      <Field label="Musico"><Select value={L.role} options={ROLES.map(r => [r, ROLE_ES[r]])} onChange={edit(v => { L.role = v; delete L.track; })} /></Field>
      <Field label="Empieza (s)"><Num value={+(L.t / 1000).toFixed(3)} step={0.1} min={0} onCommit={edit(v => { L.t = Math.max(0, Math.round(v * 1000)); })} /></Field>
      <Field label="Dura (s)"><Num value={+(L.dur / 1000).toFixed(3)} step={0.1} min={0.1} onCommit={edit(v => { L.dur = Math.max(100, Math.round(v * 1000)); for (const k of L.keys) k.t = Math.min(k.t, L.dur); })} /></Field>
      <Field label="Fundido (ms)" hint="Entrada y salida suaves del retoque sobre la animacion"><Num value={L.fade ?? 200} step={50} min={0} onCommit={edit(v => { L.fade = Math.max(0, Math.round(v)); })} /></Field>
      <Field label="Transicion"><Select value={L.interp || 'sineInOut'} options={EASES.filter(e => e[0] !== 'auto').map(e => [e[0], e[1]])} onChange={edit(v => { L.interp = v; })} /></Field>
      <p className="muted small">{L.keys.length} clave(s){bones.length ? ` · toca: ${bones.slice(0, 6).map(boneName).join(', ')}${bones.length > 6 ? ` y ${bones.length - 6} mas` : ''}` : ''}</p>
      <div className="row">
        <button onClick={() => seek(L.t)}><Icon n="play" s={12} />Ir al inicio</button>
        <span className="sep" /><button className="danger" onClick={deleteSel}>Borrar</button>
      </div>
    </Card>
    <Note>El retoque se suma encima de lo que ya hace el musico (su loop, clip o captura). Al compilar se hornea la
      pose final en clips del juego. Doble clic en el bloque de la linea de tiempo tambien abre el editor.</Note>
    {S.proj && !bones.length && <Note kind="warn">Aun no tiene claves: abre el editor, elige una articulacion en la vista 3D y girala con el gizmo.</Note>}
  </div>);
}
