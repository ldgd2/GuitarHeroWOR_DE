// Panel derecho con pestanas (la barra activa y el contenido se animan con anime.js).
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
import { Icon, useBus, useLayoutEffect, useRef } from '../react.jsx';

const TABS = [['insp', 'Inspector', 'edit'], ['cams', 'Camaras', 'cam'], ['fx', 'Efectos', 'fx'], ['props', 'Objetos', 'cube'],
  ['band', 'Banda', 'user'], ['view', 'Vista', 'eye']];
const BODY = { band: BandPanel, view: ViewPanel, cams: CamerasPanel, fx: FxPanel, props: PropsPanel };
const ITEM = { cam: CameraCut, note: NoteItem, event: EventItem, clipdef: ClipDef };

function Inspector() {
  if (!S.proj) return <div className="empty"><p>Abre una cancion con el buscador de arriba.</p></div>;
  const s = S.sel, C = s && ITEM[s.type];
  if (s?.type === 'shot') return <CamerasPanel />;
  if (s?.type === 'fx') return <FxPanel />;
  if (s?.type === 'prop') return <PropsPanel />;
  const node = C ? <C key={JSON.stringify(s)} sel={s} /> : null;
  return node && (s.type !== 'clipdef' || S.proj.clips[s.key]) ? node : <Summary />;
}

export function Panel() {
  useBus('inspector', 'changed', 'song');
  const tab = S.tab, idx = Math.max(0, TABS.findIndex(t => t[0] === tab));
  const bar = useRef(), body = useRef(), prev = useRef(tab);
  useLayoutEffect(() => {
    anime({ targets: bar.current, translateX: `${idx * 100}%`, duration: 380, easing: 'easeOutElastic(1, .75)' });
    if (prev.current !== tab) {
      const dir = TABS.findIndex(t => t[0] === prev.current) < idx ? 1 : -1;
      anime({ targets: body.current, opacity: [0, 1], translateX: [24 * dir, 0], duration: 300, easing: 'easeOutCubic' });
      prev.current = tab;
    }
  }, [tab]);
  const B = BODY[tab] || Inspector;
  return <>
    <nav className="tabs">{TABS.map(([k, l, ic]) => <button key={k} className={tab === k ? 'on' : ''} title={l}
      onClick={() => { S.tab = k; emit('inspector'); }}><Icon n={ic} s={14} /><span>{l}</span></button>)}
      <i className="bar" ref={bar} style={{ width: `calc((100% - 6px) / ${TABS.length})` }}></i></nav>
    <div className="panelBody" ref={body}><B /></div>
  </>;
}
