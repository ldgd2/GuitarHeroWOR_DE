// EditorSong - editor de puesta en escena de canciones de Warriors of Rock.
// Punto de entrada: carga los modulos (cada uno registra sus eventos) y arranca.
//   core/      qbkeys, estado, bus de eventos, opciones de vista
//   song/      lectura de clips, loops y tempo de la cancion
//   timeline/  pistas, dibujo, edicion, raton (canvas)
//   viewport/  escena 3D: escenario, banda (animacion + IK), camaras (three.js)
//   band/      configuracion de la banda de la vista previa
//   ui/        interfaz en React 18 (JSX, compilado con Vite) y animaciones con anime.js
import './css/base.css';
import './css/layout.css';
import './css/timeline.css';
import './css/panel.css';
import { $, api, emit, esc, refresh, S, store } from './core/state.js';
import './audio/player.js';
import { registerName } from './core/qb.js';
import { ROLES } from './core/roles.js';
import { start } from './viewport/scene.js';
import { shotFront, shotRole } from './viewport/controls.js';
import { loadBand } from './viewport/band.js';
import './viewport/program.js';
import './ui/playback.js';
import { mount } from './ui/react.jsx';
import { openSong, setVenue } from './ui/actions.js';
import { dialog, toast } from './ui/overlay.js';
import { TopBar } from './ui/components/TopBar.jsx';
import { Panel } from './ui/components/Panel.jsx';
import { Transport } from './ui/components/Transport.jsx';
import { ViewTools } from './ui/components/ViewTools.jsx';
import { Overlays } from './ui/components/Overlays.jsx';
import { Timeline } from './ui/timeline/Timeline.jsx';
import { KfDock, KfSide } from './keyframes/KeyframeEditor.jsx';
import './css/keyframes.css';
import './viewport/props.js';
import { on } from './core/state.js';
import { VIEW } from './core/view.js';

S.venues = [];
mount('ui-top', TopBar); mount('ui-panel', Panel); mount('ui-transport', Transport);
mount('ui-viewtools', ViewTools); mount('ui-overlays', Overlays); mount('ui-timeline', Timeline);
mount('ui-kfdock', KfDock); mount('ui-kfside', KfSide);
// pruebas automaticas sin ventana (?debug): acceso al estado y a las acciones principales
if (new URLSearchParams(location.search).has('debug')) import('./keyframes/kf.js').then(kf => { window.__ES = { S, openSong, kf }; });
start();

// vista 3D ampliada: oculta el monitor y estira la vista principal
const applyMax = () => document.body.classList.toggle('maxView', !!VIEW.maxView);

// altura de la linea de tiempo: separador arrastrable (se recuerda)
{
  const h = store.get('tlH'); if (h) document.documentElement.style.setProperty('--tlH', h + 'px');
  const bar = document.getElementById('tlResize');
  bar?.addEventListener('pointerdown', e => {
    bar.setPointerCapture(e.pointerId); bar.classList.add('drag');
    const move = ev => { const v = Math.max(180, Math.min(innerHeight - 200, innerHeight - ev.clientY)); document.documentElement.style.setProperty('--tlH', v + 'px'); store.set('tlH', v); };
    const up = () => { bar.classList.remove('drag'); bar.removeEventListener('pointermove', move); bar.removeEventListener('pointerup', up); };
    bar.addEventListener('pointermove', move); bar.addEventListener('pointerup', up);
  });
}
on('view', k => k === 'maxView' && applyMax()); applyMax();

async function loadModels() {
  try {
    const [catalog, anims] = await Promise.all([api('/api/band/catalog'), api('/api/band/anims')]);
    S.anims = anims;
    for (const a of anims) { S.animById.set(a.id, a.name); if (!a.name.startsWith('0x')) registerName(parseInt(a.id, 16), a.name); }
    $('animNames').innerHTML = anims.filter(a => !a.additive && !a.name.startsWith('0x')).map(a => `<option value="${esc(a.name)}">`).join('');
    S.catalog = catalog; emit('catalog');
    loadBand(); refresh();
  } catch (err) { toast('No se pudieron cargar los modelos de la banda: ' + err.message, 'err', 8000); }
}

const qs = new URLSearchParams(location.search);

(async () => {
  try {
    [S.songs, S.venues] = await Promise.all([api('/api/songs'), api('/api/venues')]);
    emit('songs');
    loadModels();
    let last = qs.get('song');
    try { last ||= JSON.parse(localStorage.getItem('lastSong')); } catch { /* sin almacenamiento */ }
    if (last && S.songs.some(s => s.id === last)) {
      await openSong(last);
      if (+qs.get('t')) { S.t = +qs.get('t'); emit('timeline'); }
      if (qs.get('venue') && S.venues.includes(qs.get('venue'))) await setVenue(qs.get('venue'), false);
    } else await setVenue(S.venues.includes('Z_Barbarian') ? 'Z_Barbarian' : S.venues[0], false);
    if (qs.get('tab')) { S.tab = qs.get('tab'); refresh(); }
    if (ROLES.includes(qs.get('cam'))) shotRole(qs.get('cam')); else shotFront();
  } catch (err) { dialog(`<p class="err">No se pudo conectar con el servidor: ${esc(err.message)}</p>`); }
  document.getElementById('boot').classList.add('done');
})();
