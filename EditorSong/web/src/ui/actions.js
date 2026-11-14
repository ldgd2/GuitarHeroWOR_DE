// Acciones de la aplicacion (abrir, guardar, compilar, copias, importar, nuevo clip). Sin DOM.
import { api, changed, emit, esc, pushUndo, refresh, S, store } from '../core/state.js';
import { hx, mkStruct, qbkey, registerName } from '../core/qb.js';
import { HEAD } from '../timeline/lanes.js';
import { newChar } from '../timeline/edit.js';
import { clearSongAnims } from '../viewport/anim.js';
import { loadVenue } from '../viewport/venue.js';
import { ask, confirmBox, dialog, toast } from './overlay.js';
import { materialize } from '../camera/shots.js';

export async function openSong(id, preferSaved = true) {
  if (S.dirty && !await confirmBox('Hay cambios sin guardar. Abrir otra cancion igualmente?', 'Abrir', 'danger')) return;
  S.loadingSong = true; emit('song');
  try {
    const P = await api(`/api/project?id=${encodeURIComponent(id)}${preferSaved ? '&saved=1' : ''}`);
    // nombres legibles de clips y animaciones de la cancion (diccionario del Guitar Hero SDK)
    for (const [k, n] of Object.entries(P.names || {})) { registerName(parseInt(k, 16), n); if (!S.animById.has(k)) S.animById.set(k, n); }
    for (const s of P.skas || []) if (s.label && !s.label.startsWith('0x') && !S.animById.has(s.id)) S.animById.set(s.id, s.label);
    Object.assign(S, { proj: P, sel: null, undo: [], redo: [], dirty: false, t: 0, scroll: 0 });
    clearSongAnims();
    const w = document.getElementById('tlScroll')?.clientWidth || 1000;
    S.zoom = Math.max(0.005, (w - HEAD - 20) / Math.max(1, P.length));
    store.set('lastSong', id);
    const v = store.get('venue:' + id) || store.get('venue') || S.venueName || 'Z_Barbarian';
    if (S.venues.includes(v) && v !== S.venueName) await setVenue(v, false);
  } finally { S.loadingSong = false; }
  emit('song'); refresh();
}

export async function setVenue(zone, remember = true) {
  S.venueName = zone;
  if (remember) { store.set('venue', zone); if (S.proj) store.set('venue:' + S.proj.song.id, zone); }
  emit('song');
  await loadVenue(zone);
}

export async function save() {
  if (!S.proj) return;
  const r = await api('/api/save', { method: 'POST', body: JSON.stringify(S.proj) });
  S.dirty = false; emit('song');
  toast(`Proyecto guardado (${r.saved.split(/[\\/]/).pop()}). El juego no se ha tocado.`);
}

export async function reload() {
  if (S.proj && await confirmBox('Volver a leer la cancion del juego? Se pierden los cambios no compilados.', 'Releer', 'danger')) {
    S.dirty = false; await openSong(S.proj.song.id, false);
  }
}

export async function build() {
  if (!S.proj) return;
  const st = await api('/api/status');
  const ok = await dialog(`<h3>Compilar al juego</h3><p>Se escribira <code>${esc(S.proj.song.file)}</code>. Antes se guarda una copia en <code>EditorSong/backups</code>.</p>
    <p class="note">Solo cambia el contenido de esta cancion (no la lista de canciones), asi que la partida guardada no se ve afectada.</p>
    ${st.game_running ? '<p class="warn">El juego esta abierto: el cambio se vera la proxima vez que cargue la cancion.</p>' : ''}`, [['cancel', 'Cancelar'], ['go', 'Compilar', 'primary']]);
  if (ok !== 'go') return;
  try {
    // las tomas de camara del editor se convierten en camaras .ska + clips + cortes (en una copia)
    const n = (S.proj.shots || []).length;
    if (n) toast(`Generando ${n} camara(s) a partir de las tomas...`);
    const { proj, warnings, edfx } = await materialize(S.proj);
    const r = await api('/api/build', { method: 'POST', body: JSON.stringify(proj) });
    // el proyecto fuente (con tomas y objetos editables) se guarda aparte: al reabrir se sigue editando igual
    await api('/api/save', { method: 'POST', body: JSON.stringify(S.proj) });
    r.checks.push(...warnings.map(text => ({ level: 'warn', text })));
    const bad = r.checks.filter(c => !['ok', 'info'].includes(c.level));
    S.dirty = false; emit('song');
    if (!bad.length) toast('Compilado y validado sin errores.');
    dialog(`<h3>Compilado</h3><p>Escrito en <code>${esc(r.written)}</code><br>Copia: <code>${esc(r.backup)}</code></p>
      ${n ? `<p>${n} toma(s) de camara convertidas en camaras del juego (clip + corte de momento).</p>` : ''}
      ${r.added_skas.length ? `<p>${r.added_skas.length} animacion(es) nuevas anadidas al PAK.</p>` : ''}
      ${r.edfx ? `<p>Para re-wor (<code>${esc(r.edfx)}</code>): ${edfx.fx} efecto(s) de imagen, ${edfx.props} objeto(s)/texto(s) en ${edfx.shots} toma(s), ${edfx.sync} eventos de sincronia.</p>
        <p class="note">Estos los dibuja re-wor encima del juego (el juego original no los conoce). Los filtros de color (B/N, sepia...) son aproximados.</p>` : ''}
      ${edfx.pyro ? `<p>${edfx.pyro} disparo(s) de pirotecnia nativa (nota 78 de luces) a partir de las columnas de fuego.</p>` : ''}
      <h3>Validacion</h3>${bad.length ? `<ul>${bad.map(c => `<li class="${c.level === 'error' ? 'err' : 'warn'}">${esc(c.text)}</li>`).join('')}</ul>` : '<p class="okc">Sin errores ni avisos.</p>'}`);
  } catch (err) { toast(err.message, 'err', 6000); }
}

export async function backups() {
  if (!S.proj) return;
  const list = await api('/api/backups?id=' + encodeURIComponent(S.proj.song.id));
  const r = await dialog(list.length
    ? `<h3>Copias de seguridad</h3><p class="note">Restaurar devuelve el archivo del juego a ese momento.</p>${list.map(b => `<div class="row"><code class="small">${esc(b)}</code></div>`).join('')}`
    : '<p>No hay copias de esta cancion todavia.</p>', [['ok', 'Cerrar'], ...(list.length ? [['restore', 'Restaurar la mas reciente', 'danger']] : [])]);
  if (r === 'restore') {
    await api('/api/restore', { method: 'POST', body: JSON.stringify({ id: S.proj.song.id, backup: list[0] }) });
    S.dirty = false; await openSong(S.proj.song.id, false); toast('Copia restaurada.');
  }
}

export async function importSka(file) {
  if (!S.proj || !file) return;
  const name = await ask('Nombre interno de la animacion (letras, numeros, _):', file.name.replace(/\..*$/, ''));
  if (!name) return;
  try {
    const s = await api('/api/import_ska?name=' + encodeURIComponent(name), { method: 'POST', body: await file.arrayBuffer() });
    s.imported = true; pushUndo();
    S.proj.skas = S.proj.skas.filter(x => x.id !== s.id).concat([s]);
    clearSongAnims(); changed();
    toast(`Importada ${name} (${s.bones} huesos, ${s.duration.toFixed(2)} s). Se anade al PAK al compilar.`);
  } catch (err) { toast(err.message, 'err', 6000); }
}

export async function newClip() {
  if (!S.proj) return;
  const name = await ask('Nombre del clip nuevo (letras, numeros, _):', 'clip_' + Object.keys(S.proj.clips).length);
  if (!name) return;
  const key = hx(qbkey(name));
  if (S.proj.clips[key]) return toast('Ya existe un clip con ese nombre.', 'warn');
  pushUndo(); registerName(qbkey(name), name);
  S.proj.clips[key] = mkStruct([['array', 'characters', { elem: 'struct', values: [newChar('vocalist')] }],
    ['array', 'cameras', { elem: 'empty', values: [] }], ['array', 0x65C1ECD3, { elem: 'empty', values: [] }]]);
  S.sel = { type: 'clipdef', key }; S.tab = 'insp'; changed();
}
if (new URLSearchParams(location.search).has('debug')) window.__materialize = materialize;   // pruebas automaticas
