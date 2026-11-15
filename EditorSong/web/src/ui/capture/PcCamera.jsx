// Tarjeta "Camara del PC": eleccion de camara, modo y arranque; video con lo detectado encima.
import { askPermission, CAM, checkPermission, ensureVideo, listCameras, parkVideo, startWebcam, stopWebcam } from '../../capture/webcam.js';
import { Card, Check, Field, Note, Select, Slider } from '../components/fields.jsx';
import { Icon, useEffect, useRef } from '../react.jsx';

const MODES = [['body', 'Cuerpo'], ['head', 'Cabeza y rostro'], ['face', 'Rostro (detalle)']];

function Live() {
  const box = useRef(), cv = useRef();
  useEffect(() => {
    const v = ensureVideo();
    box.current.prepend(v);
    CAM.overlay = cv.current;
    return () => { CAM.overlay = null; parkVideo(); };
  }, []);
  return (<div className={'camLive' + (CAM.mirror ? ' mirror' : '')} ref={box}>
    <canvas ref={cv} className="camOverlay" />
    {!CAM.on && <span className="camIdle">{CAM.starting ? 'Iniciando...' : 'Camara apagada'}</span>}
  </div>);
}

// estado del permiso: boton para pedirlo y, si esta bloqueado, como desbloquearlo
function Permission() {
  if (CAM.perm === 'granted') return null;
  if (CAM.perm === 'insecure') return <Note kind="warn">Esta pagina no puede usar camaras: abre el editor en <b className="mono">http://127.0.0.1:8765</b> (no con la IP del PC).</Note>;
  const denied = CAM.perm === 'denied';
  return (<div className="camPerm">
    <p className="small">{denied ? 'El navegador tiene la camara BLOQUEADA para el editor.' : 'El editor necesita permiso para usar la camara del PC.'}</p>
    <button className="primary" disabled={CAM.asking} onClick={askPermission}><Icon n="cam" s={14} />{CAM.asking ? 'Esperando respuesta del navegador...' : 'Permitir la camara'}</button>
    {(denied || CAM.asking) && <ol className="small camSteps">
      <li>Pulsa el icono a la izquierda de <span className="mono">127.0.0.1:8765</span> en la barra de direcciones (camara tachada, candado o ajustes).</li>
      <li>En <b>Camara</b> elige <b>Permitir</b>.</li>
      <li>Recarga la pagina (F5). Si no aparece, en Configuracion del navegador: Privacidad, Configuracion de sitios, Camara, y quita 127.0.0.1 de "No permitido".</li>
      <li>En Windows: Configuracion, Privacidad y seguridad, Camara: activa "Acceso a la camara" y "Permitir que las aplicaciones de escritorio accedan".</li>
    </ol>}
  </div>);
}

export function PcCamera() {
  useEffect(() => { checkPermission().then(() => listCameras()); }, []);
  const restart = patch => { Object.assign(CAM, patch); if (CAM.on) startWebcam(); };
  return (<Card title="Camara del PC" icon="cam" actions={CAM.on
    ? <button className="danger small" onClick={() => stopWebcam()}><Icon n="stop" s={11} />Apagar</button>
    : <button className="primary small" disabled={CAM.starting || CAM.perm === 'insecure'} onClick={() => startWebcam()}><Icon n="play" s={11} />Encender</button>}>
    <Permission />
    <Live />
    <p className="small camStat">
      <i className={'led' + (CAM.on && CAM.last ? ' on' : '')} />
      {CAM.on ? (CAM.last ? 'Siguiendo' : (CAM.mode === 'body' ? 'Buscando un cuerpo' : 'Buscando una cara')) : 'Apagada'}
      {CAM.on && <span className="mono muted"> · {CAM.fps.toFixed(0)} fps · {CAM.dev} · {CAM.size}</span>}
    </p>
    {CAM.error && <Note kind="warn">{CAM.error}</Note>}
    <Field label="Camara">
      <div className="row tight">
        <Select value={CAM.deviceId} options={CAM.devices.length ? CAM.devices.map(d => [d.id, d.label]) : [['', 'Sin camaras']]} onChange={v => restart({ deviceId: v })} />
        <button className="icon ghost" title="Volver a buscar camaras" onClick={listCameras}><Icon n="reload" s={14} /></button>
      </div>
    </Field>
    <Field label="Modo"><Select value={CAM.mode} options={MODES} onChange={v => restart({ mode: v })} /></Field>
    <Check checked={CAM.precise} onChange={v => restart({ precise: v })}>Precision alta (720p; cuerpo con el modelo pesado)</Check>
    {CAM.mode === 'body' && <Check checked={CAM.hands} onChange={v => restart({ hands: v })}>Manos y dedos (y agarre del micro)</Check>}
    <Check checked={CAM.mirror} onChange={v => { CAM.mirror = v; restart({}); }}>Ver en espejo</Check>
    <Slider label="Suavizado" value={CAM.smooth} min={0} max={1} step={0.05} onChange={v => { CAM.smooth = v; }} hint="Se aplica al volver a encender" />
    <Note>Corre en el PC con su tarjeta grafica, a la velocidad de la camara. Lo detectado va al mismo sitio que lo del
      telefono: calibrar, grabar y exportar funcionan igual (botones de arriba).</Note>
  </Card>);
}
