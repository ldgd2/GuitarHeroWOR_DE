// Barra flotante de la vista 3D: encuadres, opciones rapidas y progreso de carga.
import { S } from '../../core/state.js';
import { setView, VIEW } from '../../core/view.js';
import { ROLE_COLOR, ROLE_ES, ROLES } from '../../core/roles.js';
import { copyProgram, shotFront, shotRole, shotTop } from '../../viewport/controls.js';
import { Icon, useBus  } from '../react.jsx';

const T = ({ k, icon, title }) => (<button className={'tool' + (VIEW[k] ? ' on' : '')} title={title} onClick={() => setView(k, !VIEW[k])}><Icon n={icon} /></button>);

export function ViewTools() {
  useBus('view', 'loading');
  const L = S.loading;
  const pct = L && L.total ? Math.round(100 * L.done / L.total) : 100;
  return (<><div className="tools">
      <button className="tool" title="Frente (0)" onClick={shotFront}><Icon n="front" /></button>
      <button className="tool" title="Desde arriba (9)" onClick={shotTop}><Icon n="top" /></button>
      {ROLES.map((r, i) => (<button key={r} className="tool role" title={`${ROLE_ES[r]} (${i + 1})`} style={{ '--c': ROLE_COLOR[r] }}
        onClick={() => shotRole(r)}>{i + 1}</button>))}
      <button className="tool" title="Copiar la camara de programa (C)" onClick={copyProgram}><Icon n="cam" /></button>
      <i className="div"></i>
      <T k="through" icon="eye" title="Ver por la camara de programa" />
      <T k="textures" icon="tex" title="Texturas" />
      <T k="flat" icon="sun" title="Luz horneada del escenario / luces dinamicas" />
      <T k="skeleton" icon="bone" title="Ver esqueleto (rig)" />
      <T k="maxView" icon="max" title="Ampliar la vista 3D" />
    </div>
    <div className={'loadbar' + (pct < 100 ? ' on' : '')}><i style={{ width: pct + '%' }}></i><span>Cargando modelos y texturas {pct}%</span></div></>);
}
