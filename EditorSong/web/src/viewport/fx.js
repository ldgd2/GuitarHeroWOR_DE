// Efectos de imagen: los de la pista "Efectos" (S.proj.effects, con intensidad y ajustes) y los marcados en una toma.
// Solo vista previa: se dibujan con CSS encima del monitor (y de la vista 3D si se mira por la camara de programa);
// el glow usa el bloom real del monitor (VIEW.fxBloom). En el juego los hace el motor (native_fx.cpp).
import { S } from '../core/state.js';
import { VIEW } from '../core/view.js';
import { ownTrack, trackActive } from '../timeline/layout.js';
import { drawFx2D, FX2D } from './fx2d.js';
import { fxDefaults } from '../camera/presets.js';

// Looks de color: aproximacion CSS de la matriz de color que usa el juego
const LOOK_CSS = {
  teal: k => `sepia(${0.2 * k}) hue-rotate(${-15 * k}deg) saturate(${1 + 0.25 * k}) contrast(${1 + 0.12 * k})`,
  blockbuster: k => `hue-rotate(${-10 * k}deg) saturate(${1 - 0.15 * k}) contrast(${1 + 0.2 * k}) brightness(${1 - 0.03 * k})`,
  bleach: k => `saturate(${1 - 0.55 * k}) contrast(${1 + 0.4 * k}) brightness(${1 + 0.05 * k})`,
  noir: k => `grayscale(${k}) contrast(${1 + 0.55 * k}) brightness(${1 - 0.05 * k})`,
  dramatic: k => `saturate(${1 - 0.2 * k}) contrast(${1 + 0.45 * k}) brightness(${1 - 0.05 * k})`,
  film: k => `sepia(${0.12 * k}) contrast(${1 + 0.1 * k}) saturate(${1 + 0.1 * k})`,
  kodak: k => `sepia(${0.15 * k}) saturate(${1 + 0.25 * k}) contrast(${1 + 0.12 * k})`,
  polaroid: k => `sepia(${0.1 * k}) saturate(${1 - 0.1 * k}) brightness(${1 + 0.06 * k}) contrast(${1 - 0.05 * k})`,
  retro70: k => `sepia(${0.3 * k}) saturate(${1 - 0.25 * k}) contrast(${1 - 0.12 * k}) brightness(${1 + 0.04 * k})`,
  technicolor: k => `saturate(${1 + 0.5 * k}) contrast(${1 + 0.15 * k})`,
  lomo: k => `saturate(${1 + 0.4 * k}) contrast(${1 + 0.3 * k})`,
  vaporwave: k => `hue-rotate(${-25 * k}deg) saturate(${1 + 0.25 * k}) sepia(${0.12 * k}) brightness(${1 + 0.05 * k})`,
  miami: k => `hue-rotate(${-20 * k}deg) saturate(${1 + 0.35 * k}) brightness(${1 + 0.04 * k})`,
  cyberpunk: k => `hue-rotate(${-35 * k}deg) saturate(${1 + 0.3 * k}) contrast(${1 + 0.2 * k})`,
  sunset: k => `sepia(${0.35 * k}) saturate(${1 + 0.15 * k}) brightness(${1 + 0.04 * k})`,
  golden: k => `sepia(${0.4 * k}) saturate(${1 + 0.05 * k}) brightness(${1 + 0.08 * k})`,
  desert: k => `sepia(${0.55 * k}) saturate(${1 + 0.1 * k}) contrast(${1 + 0.1 * k})`,
  ice: k => `hue-rotate(${10 * k}deg) saturate(${1 - 0.3 * k}) brightness(${1 + 0.08 * k}) contrast(${1 + 0.1 * k})`,
  toxic: k => `sepia(${0.3 * k}) hue-rotate(${45 * k}deg) saturate(${1 + 0.3 * k}) contrast(${1 + 0.2 * k})`,
  blood: k => `sepia(${0.4 * k}) hue-rotate(${-25 * k}deg) saturate(${1 + 0.6 * k}) contrast(${1 + 0.3 * k}) brightness(${1 - 0.05 * k})`,
  matrix: k => `sepia(${0.3 * k}) hue-rotate(${65 * k}deg) saturate(${1 + 0.3 * k}) contrast(${1 + 0.15 * k})`,
  night: k => `grayscale(${k}) sepia(${k}) hue-rotate(${60 * k}deg) saturate(${1 + 2 * k}) brightness(${1 + 0.25 * k})`,
  infrared: k => `hue-rotate(${150 * k}deg) saturate(${1 + 0.2 * k})`,
  pastel: k => `saturate(${1 - 0.4 * k}) brightness(${1 + 0.12 * k}) contrast(${1 - 0.15 * k})`,
  dream: k => `brightness(${1 + 0.12 * k}) contrast(${1 - 0.2 * k}) saturate(${1 - 0.2 * k})`,
  sepia: k => `sepia(${0.9 * k})`,
};
const hexRgb = h => { const n = parseInt(String(h || '#000000').slice(1), 16) || 0; return [(n >> 16) & 255, (n >> 8) & 255, n & 255]; };

const layers = [];
function layer(wrapId, canvasId) {
  const wrap = document.getElementById(wrapId), canvas = document.getElementById(canvasId);
  if (!wrap || !canvas) return null;
  const el = document.createElement('div');
  el.className = 'fxLayer';
  el.innerHTML = '<i class="bar top"></i><i class="bar bot"></i><i class="vig"></i><i class="chroma"></i><i class="flash"></i><i class="fade"></i><canvas class="fx2d"></canvas>';
  wrap.appendChild(el);
  const [top, bot, vig, chroma, flash, fade, c2d] = el.children;
  return { el, canvas, top, bot, vig, chroma, flash, fade, c2d };
}
function ensure() {
  if (layers.length) return;
  for (const [w, c, main] of [['monWrap', 'monitor', false], ['viewWrap', 'view', true]]) { const L = layer(w, c); if (L) layers.push({ ...L, main }); }
}

const smooth = k => { k = Math.min(1, Math.max(0, k)); return k * k * (3 - 2 * k); };
// efectos activos en t -> { tipo: {k: intensidad con fundidos, el: s desde el inicio, left: s hasta el final, p: ajustes, e} }
export function activeFx(t, shot) {
  const out = {};
  const add = (type, amount, t0, dur, e) => {
    const el = (t - t0) / 1000, left = (t0 + dur - t) / 1000;
    const p = { ...fxDefaults(type), ...(e?.params || {}) };
    let k = amount;
    if (p.fin > 0) k *= smooth(el / p.fin);
    if (p.fout > 0) k *= smooth(left / p.fout);
    const prev = out[type];
    if (!prev || k > prev.k) out[type] = { k, el, left, p, e };
  };
  for (const e of S.proj?.effects || []) {
    if (t < e.t || t >= e.t + e.dur || !trackActive(ownTrack(e, 'fx'))) continue;
    add(e.type, e.amount ?? 1, e.t, e.dur, e);
  }
  if (shot) for (const [k, on] of Object.entries(shot.fx || {})) if (on) add(k, 1, shot.t, shot.dur, null);
  return out;
}

export function applyFx(shot, t) {
  ensure();
  const fx = activeFx(t, shot), any = Object.keys(fx).length > 0;
  const g = k => fx[k]?.k || 0, P = (k, n) => fx[k]?.p?.[n];
  const T = t / 1000;
  const lbRatio = P('letterbox', 'ratio') || 2.39;
  const bar = fx.letterbox ? 100 * Math.min(0.45, Math.max(0, (1 - (16 / 9) / lbRatio) / 2)) * g('letterbox') * smooth(Math.min(1, fx.letterbox.el / 0.4, fx.letterbox.left / 0.4)) : 0;
  let flash = 0, strobe = 0, white = 0;
  if (fx.flash) flash = Math.min(1, g('flash') * P('flash', 'power') / 2.5) * Math.max(0, 1 - fx.flash.el / Math.max(0.05, P('flash', 'len')));
  if (fx.strobe) strobe = g('strobe') * (Math.floor(fx.strobe.el * 2 * P('strobe', 'rate')) % 2 ? 0 : 0.85);
  if (fx.lightning) { const ph = (fx.lightning.el / Math.max(0.2, P('lightning', 'every'))) % 1; if (ph < 0.12) flash = Math.max(flash, g('lightning') * (0.12 - ph) / 0.12); }
  if (fx.fadewhite) white = g('fadewhite') * smooth(fx.fadewhite.el / Math.max(0.05, fx.fadewhite.el + fx.fadewhite.left));
  let fade = g('blackout');
  const red = fx.redflash ? g('redflash') * Math.max(0, 1 - fx.redflash.el / 0.6) : 0;
  if (fx.fadein) fade = Math.max(fade, g('fadein') * (1 - Math.min(1, fx.fadein.el / 0.8)));
  if (fx.fadeout) fade = Math.max(fade, g('fadeout') * (1 - Math.min(1, fx.fadeout.left / 0.8)));
  let tint = null;
  if (fx.tint) { const [r, gg, b] = hexRgb(P('tint', 'col')); tint = `rgba(${r},${gg},${b},${0.45 * g('tint') * P('tint', 'mix')})`; }
  else if (fx.cold) tint = `rgba(40,110,255,${0.16 * g('cold') * P('cold', 'amount')})`;
  else if (fx.warm) tint = `rgba(255,130,30,${0.15 * g('warm') * P('warm', 'amount')})`;
  const filt = [fx.bw && `grayscale(${g('bw')}) contrast(${1 + P('bw', 'contrast') * g('bw')})`,
    fx.exposure && `brightness(${Math.pow(2, P('exposure', 'stops') * g('exposure'))})`,
    fx.saturate && `saturate(${Math.max(0, 1 + P('saturate', 'amount') * g('saturate'))})`,
    fx.contrast && `contrast(${1 + P('contrast', 'amount') * g('contrast')})`,
    fx.blur && `blur(${2 * g('blur')}px)`,
    fx.glow && P('glow', 'bright') > 0 && `brightness(${1 + 0.25 * g('glow') * P('glow', 'bright')})`,
    (fx.motionblur || fx.tiltshift || fx.rackfocus) && `blur(${0.8 * Math.max(g('motionblur'), g('tiltshift'), g('rackfocus'))}px)`,
    fx.hueshift && `hue-rotate(${P('hueshift', 'deg') * g('hueshift')}deg)`,
    fx.rainbow && `hue-rotate(${(T * 72 * P('rainbow', 'speed')) % 360 * g('rainbow')}deg)`,
    fx.flicker && `brightness(${1 - 0.35 * g('flicker') * (0.5 + 0.5 * Math.sin(T * 47 * P('flicker', 'speed')) * Math.sin(T * 13.3 * P('flicker', 'speed')))})`,
    fx.godrays && `brightness(${1 + 0.12 * g('godrays')})`,
    ...Object.entries(LOOK_CSS).map(([k, f]) => fx[k] && f(g(k))),
    fx.grade && (() => { const p = fx.grade.p, k = g('grade');
      return `brightness(${Math.pow(2, p.exposure * k)}) contrast(${1 + (p.contrast - 1) * k}) saturate(${1 + (p.saturation - 1) * k}) hue-rotate(${p.hue * k + 12 * p.tint * k}deg) sepia(${Math.max(0, 0.3 * p.temperature * k)})`; })(),
    fx.focus && `blur(${1.2 * g('focus') * P('focus', 'strength')}px)`].filter(Boolean).join(' ');
  // glow: el bloom real del monitor (si el aspecto del juego esta activo)
  const gl = Math.max(fx.glow ? g('glow') * P('glow', 'intensity') : 0, fx.bloom ? g('bloom') * P('bloom', 'intensity') * 0.7 : 0,
    0.6 * Math.max(g('vaporwave'), g('cyberpunk'), g('miami'), g('golden'), g('dream')));
  VIEW.fxBloom = gl;
  VIEW.fxBloomThreshold = fx.glow ? P('glow', 'threshold') : fx.bloom ? P('bloom', 'threshold') : null;
  // lente y camara (en el juego: la camara real)
  let rot = 0, sc = 1, tx = 0, ty = 0;
  if (fx.dutch) rot += P('dutch', 'angle') * g('dutch') * smooth(Math.min(1, fx.dutch.el / 0.5, fx.dutch.left / 0.5));
  if (fx.handheld) { const s = P('handheld', 'speed'); rot += 0.7 * g('handheld') * Math.sin(T * s * 0.9 + 6); tx += 0.6 * g('handheld') * Math.sin(T * s * 1.3 + 4); ty += 0.5 * g('handheld') * Math.sin(T * s * 1.1 + 5); }
  if (fx.zoompunch) { const z = fx.zoompunch, e = z.el < 0.06 ? z.el / 0.06 : Math.max(0, 1 - (z.el - 0.06) / Math.max(0.05, P('zoompunch', 'len'))); sc *= 1 + P('zoompunch', 'zoom') * g('zoompunch') * e; }
  if (fx.vertigo) sc *= 1 - 0.55 * P('vertigo', 'amount') * g('vertigo') * Math.min(1, fx.vertigo.el / Math.max(0.3, fx.vertigo.el + fx.vertigo.left));
  const xf = rot || sc !== 1 || tx || ty ? `translate(${tx}%, ${ty}%) rotate(${rot}deg) scale(${sc})` : '';
  // vineta
  let vig = Math.max(g('vignette'), g('night'), 0.5 * g('dream'), P('contrast', 'vig') * g('contrast') || 0, 0.7 * g('bleach'), 0.85 * g('noir'),
    0.95 * g('lomo'), 0.8 * g('blood'), 0.7 * g('dramatic'));
  const vsize = fx.vignette ? P('vignette', 'size') : 0.5, [vr, vg, vb] = hexRgb(fx.vignette ? P('vignette', 'col') : '#000000');
  const vsoft = fx.vignette ? P('vignette', 'soft') : 0.5;
  const dg = g('danger') * (0.6 + 0.4 * Math.sin(T * 6));
  for (const L of layers) {
    const on = (!L.main || VIEW.through) && any;
    L.el.style.display = on ? '' : 'none';
    L.canvas.style.filter = on ? filt : '';
    L.canvas.style.transform = on ? xf : '';
    if (!on) continue;
    L.top.style.height = L.bot.style.height = bar + '%';
    L.chroma.style.opacity = g('chroma');
    L.flash.style.opacity = Math.max(flash, strobe, red, white);
    L.flash.style.background = red > Math.max(flash, strobe, white) ? '#ff1830' : '#fff';
    const inner = Math.max(5, 70 - 60 * vsize), outer = Math.min(160, inner + 20 + 60 * vsoft);
    L.vig.style.background = dg > 0 ? 'radial-gradient(ellipse at center, transparent 45%, rgba(220,0,25,0.9) 100%)'
      : `radial-gradient(ellipse at center, transparent ${inner}%, rgba(${vr},${vg},${vb},0.95) ${outer}%)`;
    L.vig.style.opacity = Math.max(vig, dg);
    L.el.style.background = tint || '';
    L.fade.style.opacity = fade;
    const c = L.c2d, has2d = FX2D.some(k => fx[k]);
    c.style.display = has2d ? '' : 'none';
    if (has2d) {
      const W = L.el.clientWidth, H = L.el.clientHeight;
      if (c.width !== W || c.height !== H) { c.width = W; c.height = H; }
      const x = c.getContext('2d'); x.clearRect(0, 0, W, H); drawFx2D(x, fx, t / 1000, W, H);
    }
  }
  return fx;
}

// temblor de imagen (efecto "shake"): desplazamiento angular pequeno para la camara de programa
export function shakeOf(fx, t) {
  const k = fx.shake?.k || 0; if (!k) return null;
  const s = t / 1000 * (fx.shake.p?.speed || 1);
  return [Math.sin(s * 31) * Math.cos(s * 17), Math.sin(s * 27 + 1) * Math.cos(s * 13), Math.sin(s * 23 + 2)].map(v => v * 0.012 * k);
}
