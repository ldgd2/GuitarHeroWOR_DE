// Icono de cada efecto (nombres de ui/icons.jsx). Los looks de color no llevan icono: se muestra una muestra del
// propio look (su filtro aplicado a un degradado de escenario, ver ui/camera/FxPanel.jsx).
export const FX_ICON = {
  grade: 'sliders', exposure: 'sun', contrast: 'contrast', saturate: 'droplet', bw: 'contrast', sepia: 'palette',
  warm: 'thermo', cold: 'snow', tint: 'palette', hueshift: 'hue',
  glow: 'glow', bloom: 'glow', godrays: 'rays', flash: 'flash', strobe: 'zap', lightning: 'bolt', flicker: 'flicker',
  redflash: 'alert', heartbeat: 'heart', danger: 'alert',
  vignette: 'vignette', focus: 'focus', rackfocus: 'lens', blur: 'blur', tiltshift: 'tilt', motionblur: 'motion', letterbox: 'letterbox',
  shake: 'shake', handheld: 'hand', dutch: 'dutch', zoompunch: 'zoompunch', vertigo: 'vertigo',
  fadein: 'fade', fadeout: 'fade', fadewhite: 'fade', blackout: 'stop', iris: 'iris', spotlight: 'spotlight',
  grain: 'grain', scanlines: 'scanlines', vhs: 'tape', chroma: 'chroma',
  confetti: 'confetti', sparks: 'spark', snow: 'snow', rain: 'rain', smoke: 'cloud', lasers: 'laser',
  split: 'split',
  fisheye: 'lens', wobble: 'wobble', shockwave: 'shock', sharpen: 'sharpen', emboss: 'edges', edges: 'edges', comic: 'comic',
  matrix: 'code', night: 'moon', rainbow: 'rainbow', dream: 'cloud',
};
export const CAT_ICON = { ajustes: 'sliders', look: 'look', luz: 'light', lente: 'lens', camara: 'cam', transicion: 'fade',
  textura: 'film', particulas: 'particles', edicion: 'split', experimental: 'warn' };
export const fxIcon = (type, cat) => FX_ICON[type] || CAT_ICON[cat] || 'fx';
