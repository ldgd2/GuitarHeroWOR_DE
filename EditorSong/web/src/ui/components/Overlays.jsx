// Ventana modal y avisos.
import { S } from '../../core/state.js';
import { closeModal } from '../overlay.js';
import { useBus, useEffect, useRef, useState  } from '../react.jsx';

function Modal({ m }) {
  const [v, setV] = useState(m.input ?? '');
  const ref = useRef();
  useEffect(() => { (ref.current?.querySelector('input') || ref.current?.querySelector('button.primary') || ref.current?.querySelector('button'))?.focus(); }, []);
  const key = e => { if (e.key === 'Escape') closeModal('cancel'); if (e.key === 'Enter' && m.input !== undefined) closeModal('ok', v); };
  return (<div className="backdrop" onMouseDown={e => e.target === e.currentTarget && closeModal('cancel')} onKeyDown={key}>
    <div className="modal" ref={ref}>
      <div dangerouslySetInnerHTML={{ __html: m.html }}></div>
      {m.input !== undefined && (<input className="wide" value={v} onChange={e => setV(e.target.value)} />)}
      <menu>{m.buttons.map(([val, label, cls]) => (<button key={val} className={cls || ''} onClick={() => closeModal(val, v)}>{label}</button>))}</menu>
    </div></div>);
}

export function Overlays() {
  useBus('modal', 'toast');
  return (<>{S.modal && (<Modal key={S.modal.html} m={S.modal} />)}
    <div className="toasts">{S.toasts.map(t => (<div key={t.id} className={'toast ' + t.kind}>{t.text}</div>))}</div></>);
}
