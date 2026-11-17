// Seccion "Captura": cuerpo, cabeza y rostro capturados con el movil, aplicados al rig del juego.
import { emit, fmt, S } from '../../core/state.js';
import { ROLE_ES, ROLES } from '../../core/roles.js';
import { VCAM } from '../../vcam/vcam.js';
import { FACE_METERS, LM_BONES } from '../../capture/channels.js';
import { bodyNow, CAP, MIC, calibrateNeutral, capCfg, capDoc, capKind, deleteTake, faceNow, setCapCfg, startRec, stopRec, takes } from '../../capture/capture.js';
import { seek } from '../playback.js';
import { PcCamera } from './PcCamera.jsx';
import { CAM } from '../../capture/webcam.js';
import { Card, Check, edit, Field, Note, Select, Slider } from '../components/fields.jsx';
import { Icon, useBus } from '../react.jsx';

const MODE_ES = { body: 'Cuerpo', head: 'Cabeza y rostro', face: 'Rostro (detalle)' };

function FaceMeters() {
  const v = faceNow();
  return <div className="faceMeters">{FACE_METERS.map(([label, keys]) => {
    const x = v ? keys.reduce((s, k) => s + (v[k] || 0), 0) / keys.length : 0;
    return [<span key={label + 'l'}>{label}</span>, <i key={label + 'b'} style={{ '--v': Math.max(0, Math.min(1, x)) }} />,
      <output key={label + 'o'}>{v ? Math.round(x * 100) + '%' : '-'}</output>];
  })}</div>;
}

// esqueleto en vivo, de frente
function BodyView() {
  const lm = bodyNow();
  if (!lm) return <p className="muted small">Sin datos del cuerpo.</p>;
  const st = lm.length >= 33 * 4 ? 4 : 3, X = i => 60 - lm[i * st] * 55, Y = i => 62 + lm[i * st + 1] * 55;
  return <svg className="bodyView" viewBox="0 0 120 130" aria-label="Postura capturada">
    {LM_BONES.map(([a, b]) => <line key={a + '-' + b} x1={X(a)} y1={Y(a)} x2={X(b)} y2={Y(b)} />)}
    <circle cx={X(0)} cy={Y(0)} r="3.2" />
  </svg>;
}

function Takes({ kind }) {
  const list = takes(kind);
  if (!list.length) return <p className="muted small">Aun no hay tomas de {kind === 'body' ? 'cuerpo' : 'cara'}.</p>;
  return <ul className="list">{list.map((tk, i) => <li key={tk.t0} onClick={() => { seek(tk.t0); emit('timeline'); }}>
    <b><Icon n={kind === 'body' ? 'user' : 'face'} s={15} /></b><span className="mono">{fmt(tk.t0).slice(0, -4)}</span>
    <span>{((tk.t1 - tk.t0) / 1000).toFixed(1)} s</span>
    <span><button className="ghost small icon" title="Borrar esta toma" onClick={e => { e.stopPropagation(); deleteTake(kind, i); }}><Icon n="trash" s={13} /></button></span>
  </li>)}</ul>;
}

const MIC_OPTS = [['auto', 'Automatico (segun las manos)'], ['R', 'Siempre en la mano derecha'], ['L', 'Siempre en la mano izquierda'], ['stand', 'Siempre en el pie'], ['none', 'No tocar (el del juego)']];
function MicState() {
  const s = MIC.holder === 'R' ? 'en la mano derecha' : MIC.holder === 'L' ? 'en la mano izquierda' : 'en el pie';
  return CAP.mode === 'body' && CAP.ok ? <span className="mono"> Ahora: {s}.</span> : null;
}

export function CapturePanel() {
  useBus('capture', 'changed', 'song', 'inspector');
  if (!S.proj) return <div className="empty"><p>Abre una cancion para capturar la interpretacion.</p></div>;
  capDoc();
  const kind = capKind(), fc = capCfg('face'), bc = capCfg('body');
  const src = CAM.on ? 'Camara del PC' : 'Telefono';
  const status = !CAM.on && !VCAM.phone ? 'Ni camara del PC ni telefono conectados' : !CAP.mode ? 'Telefono en modo camara'
    : `${src} · ${MODE_ES[CAP.mode]}: ${CAP.ok ? 'siguiendo' : 'buscando'}${CAP.ok && CAP.rate ? ` · ${Math.round(CAP.rate)} datos/s` : ''}`;
  const roleOpts = ROLES.map(r => [r, ROLE_ES[r]]);
  return <div className="fade">
    <PcCamera />
    <Card title="Captura en vivo" icon="phone">
      <p className="small"><i className={'led' + (CAP.ok ? ' on' : '')} />{status}</p>
      {!CAP.mode && <Note>Enciende la camara del PC (arriba) o, en la app del telefono, elige un modo: <b>Cuerpo</b> (cuerpo entero, sin cara), <b>Cabeza y rostro</b> o <b>Rostro</b>
        (solo la cara, con mas detalle). Conecta el telefono desde la seccion Camaras si aun no lo esta.</Note>}
      {kind === 'face' && <FaceMeters />}
      {kind === 'body' && <BodyView />}
      {kind && <div className="row">
        <button disabled={!CAP.ok} onClick={() => calibrateNeutral()} title={kind === 'body' ? 'De pie, mirando al telefono' : 'Cara relajada, boca cerrada, mirando al frente'}>
          <Icon n="focus" s={14} />Calibrar reposo</button>
        {CAP.rec
          ? <button className="danger" onClick={stopRec}><Icon n="stop" s={12} />Parar</button>
          : <button className="primary" disabled={!CAP.ok} onClick={startRec} title="La cancion suena desde el cursor y se graba">
            <Icon n="record" s={12} />Grabar desde {fmt(S.t).slice(0, -4)}</button>}
      </div>}
      {CAP.rec && <p className="recline small"><Icon n="record" s={12} />Grabando {CAP.rec === 'body' ? 'cuerpo' : 'cara'}: {CAP.take?.length || 0} fotogramas</p>}
      <Note>Una toma nueva solo sustituye el tramo que cubre. Pausar en el PC tambien termina la toma.</Note>
    </Card>

    <Card title="Tomas" icon="mic">
      <h4 className="subhead">Cara</h4><Takes kind="face" />
      <h4 className="subhead">Cuerpo</h4><Takes kind="body" />
    </Card>

    <Card title="Rostro en el personaje" icon="face">
      <Field label="Musico"><Select value={fc.role} options={roleOpts} onChange={edit(v => setCapCfg('face', 'role', v))} /></Field>
      <Slider label="Contencion" value={fc.soft ?? 0.6} min={0} max={1} step={0.05} onChange={v => setCapCfg('face', 'soft', v)} hint="Mas alto: gestos limpios sin deformar la cara. 0 = tal cual llega" />
      <Slider label="Simetria de la boca" value={fc.sym ?? 0.4} min={0} max={1} step={0.05} onChange={v => setCapCfg('face', 'sym', v)} hint="Iguala en parte los dos lados (sonrisas torcidas por la deteccion)" />
      <Slider label="Mandibula" value={fc.jaw} min={0} max={2} step={0.05} onChange={v => setCapCfg('face', 'jaw', v)} />
      <Slider label="Apertura maxima" value={fc.jawMax} min={8} max={36} step={1} digits={0} unit="°" onChange={v => setCapCfg('face', 'jawMax', v)} hint="Las caras del juego llegan a unos 23-35 grados" />
      <Slider label="Labios y mejillas" value={fc.mouth} min={0} max={2} step={0.05} onChange={v => setCapCfg('face', 'mouth', v)} />
      <Slider label="Cejas" value={fc.brow} min={0} max={2} step={0.05} onChange={v => setCapCfg('face', 'brow', v)} />
      <Slider label="Parpados" value={fc.lids} min={0} max={1.5} step={0.05} onChange={v => setCapCfg('face', 'lids', v)} />
      <Slider label="Mirada" value={fc.eyes} min={0} max={1.5} step={0.05} onChange={v => setCapCfg('face', 'eyes', v)} />
      <Slider label="Cabeza" value={fc.head} min={0} max={1.5} step={0.05} onChange={v => setCapCfg('face', 'head', v)} />
      <Check checked={fc.swapLR} onChange={edit(v => setCapCfg('face', 'swapLR', v))}>Intercambiar izquierda y derecha</Check>
      <Check checked={fc.mirrorHead} onChange={edit(v => setCapCfg('face', 'mirrorHead', v))}>Giro de cabeza en espejo</Check>
      <Check checked={fc.invPitch} onChange={edit(v => setCapCfg('face', 'invPitch', v))}>Invertir arriba / abajo</Check>
      <Check checked={fc.exportHead} onChange={edit(v => setCapCfg('face', 'exportHead', v))}>Incluir cuello y cabeza en la animacion facial</Check>
      <Note>Guina un ojo: si el personaje guina el otro, marca <b>Intercambiar izquierda y derecha</b>. Se mueve el rig facial del
        juego (30 huesos: mandibula, ojos, parpados, cejas, mejillas, labios, nariz y lengua).</Note>
    </Card>

    <Card title="Cuerpo en el personaje" icon="user">
      <Field label="Musico"><Select value={bc.role} options={roleOpts} onChange={edit(v => setCapCfg('body', 'role', v))} /></Field>
      <Check checked={bc.head} onChange={edit(v => setCapCfg('body', 'head', v))}>Mover cuello y cabeza con el cuerpo</Check>
      <Check checked={bc.ground} onChange={edit(v => setCapCfg('body', 'ground', v))}>Pies en el suelo (agacharse baja la cadera)</Check>
      <Check checked={bc.swapLR} onChange={edit(v => setCapCfg('body', 'swapLR', v))}>Intercambiar izquierda y derecha</Check>
      <Check checked={bc.fingers !== false} onChange={edit(v => setCapCfg('body', 'fingers', v))}>Dedos (si la camara ve las manos)</Check>
      {bc.role === 'vocalist' && <Field label="Microfono"><Select value={bc.mic || 'auto'} options={MIC_OPTS} onChange={edit(v => setCapCfg('body', 'mic', v))} /></Field>}
      {bc.role === 'vocalist' && <Note>Automatico: cierra la mano junto a la boca y el cantante coge el micro del pie; abrela y lo
        devuelve. Con un micro de verdad en la mano funciona solo. El pie se coloca delante de la boca del personaje.
        <MicState /></Note>}
    </Card>

    <Card title="En el juego" icon="build">
      <p className="small">Al pulsar <b>Compilar al juego</b>:</p>
      <ul className="small plain">
        <li>la cara se convierte en animaciones faciales del juego (trozos de 10 s) con sus eventos <span className="mono">Band_PlayFacialAnim</span>, que sustituyen a los de ese musico en lo grabado;</li>
        <li>el cuerpo se convierte en clips con la animacion y los objetivos de IK de manos y pies, como los clips del juego.</li>
      </ul>
      <Note>El modelo del musico tiene que estar cargado en el visor: se usa su esqueleto para calcular las claves.</Note>
    </Card>
  </div>;
}
