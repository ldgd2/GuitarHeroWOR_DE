// Panel derecho: riel vertical de secciones (iconos) y la seccion activa con su cabecera.
import anime from 'animejs';
import { emit, S } from '../../core/state.js';
import { BandPanel } from '../band/BandPanel.jsx';
import { CameraCut, NoteItem } from '../inspector/CameraCut.jsx';
import { ClipDef } from '../inspector/ClipDef.jsx';
import { EventItem } from '../inspector/EventItem.jsx';
import { Summary } from '../inspector/Summary.jsx';
import { ViewPanel } from '../view/ViewPanel.jsx';
import { CamerasPanel } from '../camera/CamerasPanel.jsx';
import { FxPanel } from '../camera/FxPanel.jsx';
import { PropsPanel } from '../props/PropsPanel.jsx';
import { CapturePanel } from '../capture/CapturePanel.jsx';
import { CAP } from '../../capture/capture.js';
import { PoseLayerItem } from '../inspector/PoseLayerItem.jsx';
import { MultiSel } from '../inspector/MultiSel.jsx';
import { selList } from '../../core/selection.js';
import { Icon, useBus, useLayoutEffect, useRef } from '../react.jsx';

// secciones del riel: [clave, titulo, icono]; null = separador
const TABS = [['insp', 'Inspector', 'inspector'], null, ['cams', 'Camaras', 'cam'], ['fx', 'Efectos', 'fx'], ['props', 'Objetos 3D', 'cube'],
  null, ['band', 'Banda', 'band'], ['face', 'Captura', 'face'], null, ['view', 'Vista', 'view']];
const BODY = { band: BandPanel, view: ViewPanel, cams: CamerasPanel, fx: FxPanel, props: PropsPanel, face: CapturePanel };
const ITEM = { cam: CameraCut, note: NoteItem, event: EventItem, clipdef: ClipDef, pose: PoseLayerItem };

function Inspector() {
  if (!S.proj) return <div className="empty"><p>Abre una cancion con el buscador de arriba.</p></div>;
  const s = S.sel, C = s && ITEM[s.type];
  if (selList().length > 1) return <MultiSel />;
  if (s?.type === 'shot') return <CamerasPanel />;
  if (s?.type === 'fx') return <FxPanel />;
  if (s?.type === 'prop') return <PropsPanel />;
  const node = C ? <C key={JSON.stringify(s)} sel={s} /> : null;
  return node && (s.type !== 'clipdef' || S.proj.clips[s.key]) ? node : <Summary />;
}

// contador que acompana al titulo de la seccion
function countOf(tab) {
  const P = S.proj; if (!P) return '';
  if (tab === 'cams') return `${(P.shots || []).length} tomas`;
  if (tab === 'fx') return `${(P.effects || []).length} efectos`;
  if (tab === 'props') return `${(P.props || []).length} objetos`;
  if (tab === 'face') { const n = (P.capture?.face?.frames?.length || 0) + (P.capture?.body?.frames?.length || 0) || P.face?.frames?.length || 0; return n ? `${(n / 30).toFixed(1)} s grabados` : ''; }
  return '';
}

export function Panel() {
  useBus('inspector', 'changed', 'song', 'capture');
  const tab = S.tab || 'insp', cur = TABS.find(t => t && t[0] === tab) || TABS[0];
  const body = useRef(), prev = useRef(tab);
  useLayoutEffect(() => {
    if (prev.current !== tab) {
      anime({ targets: body.current, opacity: [0, 1], translateY: [4, 0], duration: 160, easing: 'easeOutQuad' });
      body.current.scrollTop = 0;
      prev.current = tab;
    }
  }, [tab]);
  const B = BODY[tab] || Inspector;
  return <>
    <nav className="rail" aria-label="Secciones">{TABS.map((t, i) => t ? <button key={t[0]} className={tab === t[0] ? 'on' : ''} title={t[1]}
      aria-label={t[1]} onClick={() => { S.tab = t[0]; emit('inspector'); }}><Icon n={t[2]} s={18} />
      {t[0] === 'face' && CAP.rec && <i className="badgeDot" />}</button> : <i key={'s' + i} className="railSep" />)}</nav>
    <div className="pane">
      <header className="paneHead"><h2>{cur[1]}</h2><small>{countOf(cur[0])}</small></header>
      <div className="panelBody" ref={body}><B /></div>
    </div>
  </>;
}
