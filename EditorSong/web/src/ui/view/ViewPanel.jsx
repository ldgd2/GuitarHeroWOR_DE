// Pestana "Vista": que se muestra en 3D, texturas, luz, camara de edicion y ayuda.
import { S } from '../../core/state.js';
import { setView, VIEW } from '../../core/view.js';
import { reloadAssets } from '../../viewport/venue.js';
import { useBus  } from '../react.jsx';
import { Check, Field, Select } from '../components/fields.jsx';

const C = ({ k, children }) => (<Check checked={VIEW[k]} onChange={v => setView(k, v)}>{children}</Check>);

export function ViewPanel() {
  useBus('view', 'loading');
  const L = S.loading;
  return (<div className="fade">
    <h3>Escena</h3>
    <C k="geometry">Escenario del juego (geometria)</C>
    <C k="fx">Humo y haces de luz del escenario</C>
    <C k="crowd">Publico (tarjetas sin animar)</C>
    <C k="band">Banda (modelos animados)</C>
    <C k="markers">Nodos y marcas de los musicos</C>
    <C k="grid">Rejilla</C>
    <h3>Texturas e iluminacion</h3>
    <C k="textures">Mostrar texturas</C>
    <C k="flat">Luz horneada del escenario (sin luces dinamicas)</C>
    <C k="shadows">Sombras de la banda (con luces dinamicas)</C>
    <Field label="Calidad de textura"><Select value={String(VIEW.texSize)}
      options={[['256', 'Baja (256)'], ['512', 'Media (512)'], ['1024', 'Alta (1024)'], ['2048', 'Maxima (2048)']]}
      onChange={v => setView('texSize', +v)} /></Field>
    <div className="row"><button onClick={reloadAssets}>Recargar modelos y texturas</button>
      {L && L.total ? (<span className="muted small">{L.done}/{L.total}{L.errors ? ` · ${L.errors} con error` : ''}</span>) : null}</div>
    <h3>Aspecto del juego</h3>
    <C k="post">Tonemapping filmico + bloom (como WoR)</C>
    {VIEW.post && <>
      <C k="postEdit">Tambien en la vista de edicion (mas lento)</C>
      <C k="fog">Niebla de profundidad</C>
      <Field label={`Bloom (${VIEW.bloom.toFixed(2)})`}><input type="range" min="0" max="2" step="0.05" value={VIEW.bloom} onInput={e => setView('bloom', +e.target.value)} /></Field>
      <Field label={`Exposicion (${VIEW.exposure.toFixed(2)})`}><input type="range" min="0.4" max="2.5" step="0.05" value={VIEW.exposure} onInput={e => setView('exposure', +e.target.value)} /></Field>
    </>}
    <h3>Animacion</h3>
    <C k="ik">IK de manos y pies (como el juego)</C>
    <C k="skeleton">Ver esqueleto (rig)</C>
    <h3>Camaras</h3>
    <C k="through">Ver por la camara de programa</C>
    <Field label="Ejes camara .ska"><Select value={VIEW.axis}
      options={[['flip', 'mira a +Z (verificado)'], ['data', 'datos sin girar'], ['look', 'apuntar al musico']]} onChange={v => setView('axis', v)} /></Field>
    <Field label={`Lente edicion (${VIEW.fov}°)`}><input type="range" min="20" max="100" value={VIEW.fov} onInput={e => setView('fov', +e.target.value)} /></Field>
    <Field label={`Vuelo (${VIEW.flySpeed} m/s)`}><input type="range" min="1" max="20" value={VIEW.flySpeed} onInput={e => setView('flySpeed', +e.target.value)} /></Field>
    <h3>Controles</h3>
    <ul className="help">
      <li><kbd>Arrastrar</kbd> girar · <kbd>Clic der.</kbd> mover · <kbd>Rueda</kbd> zoom</li>
      <li><kbd>W A S D</kbd> volar · <kbd>Q E</kbd> bajar/subir · <kbd>Shift</kbd> rapido</li>
      <li><kbd>1</kbd>-<kbd>4</kbd> musicos · <kbd>0</kbd> frente · <kbd>9</kbd> arriba · <kbd>C</kbd> copiar camara de programa</li>
      <li><kbd>Doble clic</kbd> centrar · <kbd>Clic</kbd> en un musico: su pestana de banda</li>
      <li><kbd>Espacio</kbd> reproducir · <kbd>Inicio</kbd>/<kbd>Fin</kbd> · <kbd>Ctrl+Z/Y/S</kbd></li>
    </ul>
  </div>);
}
