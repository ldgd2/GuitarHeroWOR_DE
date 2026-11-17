// Rasterizador de capas del estudio: convierte una capa (imagen PNG/SVG, texto, forma vectorial) con sus estilos
// (sombra, resplandor, trazo, desenfoque) en un lienzo a la resolucion pedida. Lo usan:
//   - la vista del editor (a la resolucion del zoom: los SVG y las formas siguen nitidos al acercar),
//   - la vista 3D (texturas), la vista previa interactiva y
//   - la exportacion al juego (bake.js: PNG a la calidad elegida).
// Asi el editor, la vista previa y el juego ven exactamente la misma imagen.
//
// Unidades: las de la capa (1 = 1 px de una pantalla 1280 x 720). pad = margen que anaden los estilos alrededor.

const imgCache = new Map();          // url -> {img, ok}
const listeners = new Set();
let version = 0;
export const rasterVersion = () => version;
export const onRasterChange = fn => { listeners.add(fn); return () => listeners.delete(fn); };
const bump = () => { version++; for (const f of listeners) f(version); };

/** imagen ya cargada o null (y la pide; al llegar avisa a onRasterChange) */
export function getImage(url) {
  if (!url) return null;
  let e = imgCache.get(url);
  if (!e) {
    const img = new Image();
    e = { img, ok: false, failed: false };
    imgCache.set(url, e);
    img.onload = () => { e.ok = true; bump(); };
    img.onerror = () => { e.failed = true; bump(); };
    img.src = url;
  }
  return e.ok ? e.img : null;
}
export const imageFailed = url => !!imgCache.get(url)?.failed;

/** espera a que esten cargadas todas las imagenes y fuentes de unas capas (antes de exportar) */
export async function preload(layers, urlOf) {
  const waits = [];
  for (const l of layers) {
    const u = urlOf(l);
    if (!u) continue;
    getImage(u);
    const e = imgCache.get(u);
    if (!e.ok && !e.failed) waits.push(new Promise(res => { e.img.addEventListener('load', res, { once: true }); e.img.addEventListener('error', res, { once: true }); }));
  }
  for (const l of layers) if (l.type === 'text' && isWorFont(l.text?.font)) waits.push(loadWorFont(l.text.font));
  await Promise.all(waits);
  if (document.fonts?.ready) await document.fonts.ready;
}

// ------------------------------------------------------------------ fuentes nativas de Warriors of Rock
// Las .fnt del juego (pak/ui_shared) decodificadas por el servidor: atlas PNG + metricas por glifo
// glifo = [x, y, w, h (en el atlas), ancho, alto, bajada bajo la linea base, desplazamiento x, avance extra]
const worFonts = new Map();          // nombre -> {m, img, ok, p}
export const isWorFont = f => typeof f === 'string' && f.startsWith('wor:');
export function loadWorFont(font) {
  const name = String(font).slice(4);
  let e = worFonts.get(name);
  if (!e) {
    e = { m: null, img: null, ok: false };
    e.p = (async () => {
      try {
        const q = encodeURIComponent(name);
        const m = await (await fetch(`/api/ui/worfont?name=${q}`)).json();
        if (!m.glyphs) throw new Error(m.error || 'sin glifos');
        const img = new Image();
        await new Promise((res, rej) => { img.onload = res; img.onerror = rej; img.src = `/api/ui/worfont?name=${q}&png=1`; });
        Object.assign(e, { m, img, ok: true });
      } catch { e.failed = true; }
      bump();
    })();
    worFonts.set(name, e);
  }
  return e.p;
}
/** {m, img} si ya esta cargada (si no, la pide y avisa al llegar) */
export function worFont(font) {
  const name = String(font).slice(4);
  if (!worFonts.has(name)) loadWorFont(font);
  const e = worFonts.get(name);
  return e.ok ? e : null;
}
function worGlyph(m, ch) {
  const c = ch.codePointAt(0);
  return m.glyphs[c] || m.glyphs[ch.toUpperCase().codePointAt(0)] || m.glyphs[ch.toLowerCase().codePointAt(0)] || null;
}
function worMeasure(F, t, px, s) {
  const k = px / F.m.line;
  let w = 0;
  for (const ch of s) {
    const g = worGlyph(F.m, ch);
    w += ((g ? g[4] + (g[8] || 0) : F.m.space) + F.m.spacing) * k + (t.spacing || 0);
  }
  return Math.max(0, w);
}
/** dibuja una linea con la fuente del juego (blanca en el atlas: se tine con multiplicar) */
function worDrawLine(g, F, t, px, s, x, top) {
  const m = F.m, k = px / m.line;
  const base = top + (px - (m.ascent + m.descent) * k) / 2 + m.ascent * k;
  for (const ch of s) {
    const gl = worGlyph(m, ch);
    if (gl) {
      const [sx, sy, sw, sh, w, h, down, ox] = gl;
      g.drawImage(F.img, sx, sy, sw, sh, x + (ox || 0) * k, base - (h - down) * k, w * k, h * k);
      x += (w + (gl[8] || 0) + m.spacing) * k + (t.spacing || 0);
    } else x += (m.space + m.spacing) * k + (t.spacing || 0);
  }
}

// ------------------------------------------------------------------ fuentes del sistema (las de este equipo)
let sysList = null;
/** {wor: [{id, label}], system: [{family, faces: [{file, style, weight, italic}]}]} (se pide una vez) */
export function fontCatalog() {
  sysList ??= fetch('/api/ui/fonts').then(r => r.json()).catch(() => ({ wor: [], system: [] }));
  return sysList;
}
/** registra todas las caras (normal, negrita, cursiva...) de una familia del sistema */
export async function registerSystemFont(family) {
  const cat = await fontCatalog();
  const fam = (cat.system || []).find(f => f.family.toLowerCase() === family.toLowerCase());
  if (!fam) return false;
  await Promise.all(fam.faces.filter(x => !/\.ttc$/i.test(x.file)).map(x =>
    registerFont(family, `/api/ui/sysfont?file=${encodeURIComponent(x.file)}`, { weight: String(x.weight), style: x.italic ? 'italic' : 'normal' })));
  return true;
}

// ------------------------------------------------------------------ fuentes
export const SYSTEM_FONTS = ['Inter Variable', 'JetBrains Mono Variable', 'Segoe UI', 'Bahnschrift', 'Arial', 'Arial Black', 'Impact',
  'Georgia', 'Times New Roman', 'Trebuchet MS', 'Verdana', 'Tahoma', 'Courier New', 'Consolas', 'Comic Sans MS', 'Franklin Gothic Medium', 'Palatino Linotype'];
const loadedFonts = new Set();
/** registra una fuente propia (TTF/OTF/WOFF subida al proyecto) */
export async function registerFont(family, url, desc) {
  const key = family + '|' + url;
  if (loadedFonts.has(key)) return;
  loadedFonts.add(key);
  try { const f = new FontFace(family, `url("${url}")`, desc || {}); await f.load(); document.fonts.add(f); bump(); }
  catch { loadedFonts.delete(key); }
}

// ------------------------------------------------------------------ texto
export function fontCss(t, px) {
  return `${t.italic ? 'italic ' : ''}${t.weight || 400} ${px}px "${t.font || 'Inter Variable'}", "Segoe UI", sans-serif`;
}
let measureCtx = null;
/** lineas ya cortadas (si la caja es fija, se parte por palabras) y medidas */
export function layoutText(t, boxW) {
  measureCtx ??= document.createElement('canvas').getContext('2d');
  const g = measureCtx;
  const px = Math.max(1, t.px || 48);
  g.font = fontCss(t, px);
  if ('letterSpacing' in g) g.letterSpacing = `${t.spacing || 0}px`;
  const F = isWorFont(t.font) ? worFont(t.font) : null;
  const measure = F ? (s => worMeasure(F, t, px, s)) : (s => g.measureText(s).width);
  const raw = String(t.value ?? '').split('\n').map(l => (t.upper ? l.toUpperCase() : l));
  const lines = [];
  for (const line of raw) {
    if (t.box !== 'fixed' || !boxW) { lines.push(line); continue; }
    let cur = '';
    for (const word of line.split(/(\s+)/)) {
      const tryLine = cur + word;
      if (cur && measure(tryLine) > boxW) { lines.push(cur.trimEnd()); cur = word.trimStart(); }
      else cur = tryLine;
    }
    lines.push(cur);
  }
  const widths = lines.map(measure);
  const lh = px * (t.line || 1.15);
  return { lines, widths, lh, px, w: Math.max(1, ...widths) + px * 0.1, h: lh * lines.length + px * 0.15 };
}
/** tamano automatico de un texto de punto (lo guarda el editor en base.w / base.h) */
export function textSize(t, boxW) {
  const L = layoutText(t, boxW);
  return [Math.ceil(L.w), Math.ceil(L.h)];
}

// ------------------------------------------------------------------ formas
/** Path2D de una forma en una caja w x h */
export function shapePath(sh, w, h) {
  const p = new Path2D();
  const k = sh.kind || 'rect';
  if (k === 'rect') {
    const r = Math.max(0, Math.min(sh.radius || 0, w / 2, h / 2));
    if (p.roundRect && r > 0) p.roundRect(0, 0, w, h, r); else p.rect(0, 0, w, h);
  } else if (k === 'ellipse') {
    p.ellipse(w / 2, h / 2, Math.max(0.01, w / 2), Math.max(0.01, h / 2), 0, 0, Math.PI * 2);
  } else if (k === 'line') {
    p.moveTo(0, h / 2); p.lineTo(w, h / 2);
  } else if (k === 'polygon' || k === 'star') {
    const n = Math.max(3, Math.round(sh.sides || 5)), pts = k === 'star' ? n * 2 : n;
    for (let i = 0; i < pts; i++) {
      const a = -Math.PI / 2 + (i / pts) * Math.PI * 2;
      const rr = k === 'star' && i % 2 ? (sh.inner ?? 0.5) : 1;
      const x = w / 2 + Math.cos(a) * (w / 2) * rr, y = h / 2 + Math.sin(a) * (h / 2) * rr;
      if (i) p.lineTo(x, y); else p.moveTo(x, y);
    }
    p.closePath();
  } else if (k === 'path') {
    const P = sh.points || [];
    // punto: [x, y, entrada_dx, entrada_dy, salida_dx, salida_dy] normalizados a la caja (0..1)
    P.forEach((q, i) => {
      if (!i) { p.moveTo(q[0] * w, q[1] * h); return; }
      const a = P[i - 1];
      p.bezierCurveTo((a[0] + (a[4] || 0)) * w, (a[1] + (a[5] || 0)) * h, (q[0] + (q[2] || 0)) * w, (q[1] + (q[3] || 0)) * h, q[0] * w, q[1] * h);
    });
    if (sh.closed && P.length > 2) {
      const a = P[P.length - 1], q = P[0];
      p.bezierCurveTo((a[0] + (a[4] || 0)) * w, (a[1] + (a[5] || 0)) * h, (q[0] + (q[2] || 0)) * w, (q[1] + (q[3] || 0)) * h, q[0] * w, q[1] * h);
      p.closePath();
    }
  }
  return p;
}
const rgba = (c, a = 1) => `rgba(${Math.round(c?.[0] ?? 0)},${Math.round(c?.[1] ?? 0)},${Math.round(c?.[2] ?? 0)},${a})`;
function fillStyle(g, f, w, h) {
  if (!f || f.type === 'none') return null;
  if (f.type === 'linear') {
    const a = ((f.angle ?? 90) - 90) * Math.PI / 180, cx = w / 2, cy = h / 2;
    const r = Math.abs(w / 2 * Math.cos(a)) + Math.abs(h / 2 * Math.sin(a));
    const gr = g.createLinearGradient(cx - Math.cos(a) * r, cy - Math.sin(a) * r, cx + Math.cos(a) * r, cy + Math.sin(a) * r);
    gr.addColorStop(0, rgba(f.color, f.alpha ?? 1)); gr.addColorStop(1, rgba(f.color2, f.alpha2 ?? 1));
    return gr;
  }
  if (f.type === 'radial') {
    const gr = g.createRadialGradient(w / 2, h / 2, 0, w / 2, h / 2, Math.max(w, h) / 2);
    gr.addColorStop(0, rgba(f.color, f.alpha ?? 1)); gr.addColorStop(1, rgba(f.color2, f.alpha2 ?? 1));
    return gr;
  }
  return rgba(f.color, f.alpha ?? 1);
}

// ------------------------------------------------------------------ capa -> lienzo
/** margen que necesitan los estilos de la capa (en unidades de la capa) */
export function fxPad(l) {
  const fx = l.fx || {};
  let p = 0;
  if (fx.shadow?.on) p = Math.max(p, (fx.shadow.blur || 0) * 1.5 + Math.max(Math.abs(fx.shadow.dx || 0), Math.abs(fx.shadow.dy || 0)));
  if (fx.glow?.on) p = Math.max(p, (fx.glow.size || 0) * 1.6);
  if (fx.stroke?.on) p = Math.max(p, fx.stroke.width || 0);
  if (fx.blur) p = Math.max(p, fx.blur * 2.5);
  if (l.type === 'shape') p = Math.max(p, (l.shape?.stroke?.width || 0) / 2 + 1);
  return Math.ceil(p);
}

/** dibuja el contenido de la capa en (0,0)-(w,h) del contexto. false si aun falta una imagen */
function drawContent(g, l, w, h, urlOf, sil = null) {
  if (l.type === 'image') {
    const img = getImage(urlOf(l));
    if (!img) return false;
    g.drawImage(img, 0, 0, w, h);
  } else if (l.type === 'text') {
    const t = l.text || {};
    const L = layoutText(t, w);
    if (isWorFont(t.font)) {
      const F = worFont(t.font);
      if (!F) return false;
      // a un lienzo aparte a la resolucion real, se tine y se pega
      const k = Math.max(0.05, Math.hypot(g.getTransform().a, g.getTransform().b));
      const off = document.createElement('canvas');
      off.width = Math.max(1, Math.ceil(w * k)); off.height = Math.max(1, Math.ceil(h * k));
      const og = off.getContext('2d');
      og.scale(k, k);
      L.lines.forEach((line, i) => {
        const x = t.align === 'left' ? 0 : t.align === 'right' ? w - L.widths[i] : (w - L.widths[i]) / 2;
        worDrawLine(og, F, t, L.px, line, x, i * L.lh);
      });
      og.setTransform(1, 0, 0, 1, 0, 0);
      const col = t.color || [255, 255, 255];
      if (sil) { og.globalCompositeOperation = 'source-in'; og.fillStyle = sil; og.fillRect(0, 0, off.width, off.height); }
      else if (col[0] < 255 || col[1] < 255 || col[2] < 255) {
        const glyphs = document.createElement('canvas'); glyphs.width = off.width; glyphs.height = off.height;
        glyphs.getContext('2d').drawImage(off, 0, 0);
        og.globalCompositeOperation = 'multiply'; og.fillStyle = rgba(col); og.fillRect(0, 0, off.width, off.height);
        og.globalCompositeOperation = 'destination-in'; og.drawImage(glyphs, 0, 0);
      }
      g.drawImage(off, 0, 0, w, h);
      return true;
    }
    g.font = fontCss(t, L.px);
    if ('letterSpacing' in g) g.letterSpacing = `${t.spacing || 0}px`;
    g.textBaseline = 'top';
    g.fillStyle = sil || rgba(t.color || [255, 255, 255]);
    L.lines.forEach((line, i) => {
      const x = t.align === 'left' ? 0 : t.align === 'right' ? w - L.widths[i] : (w - L.widths[i]) / 2;
      g.fillText(line, x, i * L.lh + L.px * 0.08);
    });
  } else if (l.type === 'shape') {
    const sh = l.shape || {};
    const path = shapePath(sh, w, h);
    const f = sil || fillStyle(g, sh.fill, w, h);
    if (f && sh.kind !== 'line' && !(sh.kind === 'path' && !sh.closed)) { g.fillStyle = f; g.fill(path); }
    const st = sh.stroke || {};
    if ((st.width || 0) > 0) {
      g.lineWidth = st.width; g.strokeStyle = sil || rgba(st.color, st.alpha ?? 1);
      g.lineJoin = 'round'; g.lineCap = sh.cap || 'round';
      g.setLineDash(st.dash ? [st.dash, st.dash] : []);
      g.stroke(path);
    }
  }
  return true;
}

/**
 * Lienzo de una capa. pxScale = pixeles por unidad. Devuelve {canvas, pad, w, h, ready}
 * (ready = false si faltaba alguna imagen: se volvera a pedir cuando llegue).
 */
export function rasterize(l, w, h, pxScale, urlOf) {
  const pad = fxPad(l);
  const maxSide = 4096;
  let s = Math.max(0.05, pxScale);
  const tw = (w + pad * 2) * s, th = (h + pad * 2) * s;
  if (tw > maxSide || th > maxSide) s *= maxSide / Math.max(tw, th);
  const c = document.createElement('canvas');
  c.width = Math.max(1, Math.ceil((w + pad * 2) * s)); c.height = Math.max(1, Math.ceil((h + pad * 2) * s));
  const g = c.getContext('2d');
  if (l.type === 'group' || l.type === 'camera' || w <= 0 || h <= 0) return { canvas: c, pad, w, h, ready: true };
  // contenido limpio en su propio lienzo (para sombras, resplandor y trazo)
  const content = document.createElement('canvas');
  content.width = c.width; content.height = c.height;
  const cg = content.getContext('2d');
  cg.scale(s, s); cg.translate(pad, pad);
  const ready = drawContent(cg, l, w, h, urlOf);
  const fx = l.fx || {};
  // trazo exterior: la silueta repetida alrededor
  if (fx.stroke?.on && (fx.stroke.width || 0) > 0) {
    const silC = document.createElement('canvas'); silC.width = c.width; silC.height = c.height;
    const sg = silC.getContext('2d');
    sg.drawImage(content, 0, 0);
    sg.globalCompositeOperation = 'source-in';
    sg.fillStyle = rgba(fx.stroke.color, fx.stroke.alpha ?? 1); sg.fillRect(0, 0, silC.width, silC.height);
    const r = fx.stroke.width * s, n = Math.max(12, Math.min(48, Math.round(r * 3)));
    for (let i = 0; i < n; i++) { const a = (i / n) * Math.PI * 2; g.drawImage(silC, Math.cos(a) * r, Math.sin(a) * r); }
    for (let i = 0; i < n; i++) { const a = (i / n) * Math.PI * 2; g.drawImage(silC, Math.cos(a) * r * 0.5, Math.sin(a) * r * 0.5); }
  }
  if (fx.glow?.on) {
    g.save();
    g.shadowColor = rgba(fx.glow.color, fx.glow.alpha ?? 0.8); g.shadowBlur = (fx.glow.size || 0) * s;
    g.drawImage(content, 0, 0); g.drawImage(content, 0, 0);
    g.restore();
  }
  if (fx.shadow?.on) {
    g.save();
    g.shadowColor = rgba(fx.shadow.color, fx.shadow.alpha ?? 0.6); g.shadowBlur = (fx.shadow.blur || 0) * s;
    g.shadowOffsetX = (fx.shadow.dx || 0) * s; g.shadowOffsetY = (fx.shadow.dy || 0) * s;
    g.drawImage(content, 0, 0);
    g.restore();
  }
  if (fx.blur) g.filter = `blur(${fx.blur * s}px)`;
  g.drawImage(content, 0, 0);
  g.filter = 'none';
  return { canvas: c, pad, w, h, ready, scale: s };
}

/** clave de cache de lo que cambia la imagen de una capa (no su posicion, giro, escala, opacidad ni tinte) */
export function rasterKey(l, w, h) {
  return JSON.stringify([l.type, l.image, l.text, l.shape, l.fx, Math.round(w * 100), Math.round(h * 100)]);
}
