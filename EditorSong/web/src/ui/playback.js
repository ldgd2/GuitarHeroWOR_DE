// Reproduccion (avance del tiempo) y atajos de teclado globales.
import { emit, redo, S, undo } from '../core/state.js';
import { deleteSel } from '../timeline/edit.js';
import { follow } from '../timeline/coords.js';
import { onFrame } from '../viewport/scene.js';
import { save } from './actions.js';
import { audioClock, audioEnded, audioPlay, audioRate, audioSeek } from '../audio/player.js';

S.speed = 1; S.snap = true; S.follow = true;
export function setPlaying(p) { S.playing = p; audioPlay(p); lastT = S.t; emit('play'); }
export function seek(t) { S.t = Math.max(0, Math.min(t, S.proj?.length ?? t)); audioSeek(S.t); lastT = S.t; follow(); emit('timeline'); }
export function setSpeed(v) { S.speed = v; audioRate(v); emit('timeline'); }

// con audio, el reloj es el del audio; si alguien movio el cursor (arrastrando en la linea de tiempo), el
// audio salta alli. Sin audio, el tiempo avanza con los fotogramas.
let lastT = 0;
onFrame(dt => {
  if (!S.playing || !S.proj) { lastT = S.t; return; }
  if (Math.abs(S.t - lastT) > 1) audioSeek(S.t);
  const clock = audioClock();
  S.t = clock !== null ? clock : S.t + dt * 1000 * S.speed;
  if (S.t > S.proj.length || audioEnded()) { S.t = Math.min(S.t, S.proj.length); setPlaying(false); }
  lastT = S.t;
  if (S.follow) follow();
  emit('timeline');
});

addEventListener('keydown', e => {
  if (['INPUT', 'SELECT', 'TEXTAREA'].includes(document.activeElement?.tagName) || S.modal) return;
  if (e.code === 'Space') { e.preventDefault(); setPlaying(!S.playing); }
  else if (e.key === 'Delete') deleteSel();
  else if (e.key === 'Home') seek(0);
  else if (e.key === 'End' && S.proj) seek(S.proj.length);
  else if (e.ctrlKey && e.key === 'z') { e.preventDefault(); undo(); }
  else if (e.ctrlKey && e.key === 'y') { e.preventDefault(); redo(); }
  else if (e.ctrlKey && e.key === 's') { e.preventDefault(); save(); }
});
addEventListener('beforeunload', e => { if (S.dirty) e.preventDefault(); });
