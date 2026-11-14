// Piel de los personajes. En los .tex de CAS las zonas de piel vienen en amarillo puro (255,255,0): el juego
// compone en tiempo real la textura de piel con el tono elegido. Aqui se hace igual: los pixeles amarillos de
// cada textura se sustituyen por el tono (conservando su sombreado) y el resto de la textura queda intacto.
import * as THREE from 'three';

export const SKIN_TONES = [['#f1c7a5', 'Clara'], ['#d9a27c', 'Media clara'], ['#b97a56', 'Media'], ['#8d5a3b', 'Morena'], ['#5c3a26', 'Oscura']];
export const DEFAULT_SKIN = '#d9a27c';
const done = new WeakMap();
const SKIN_MESH = /^(body|piece)_/, SKIN_TEMPLATE = '0x852105c8';   // piel compuesta en tiempo real (sin textura)

// Algunos .tex marcados como normales no lo son (o usan otra codificacion): con media XY muy lejos de 128 inclinan
// todas las normales y la cara sale negra. Esos se quitan; los mapas de normales normales se quedan.
const nmOk = new WeakMap();
function checkNormalMap(m) {
  const t = m.normalMap, img = t?.image; if (!img?.width) return;
  let ok = nmOk.get(img);
  if (ok === undefined) {
    const cv = document.createElement('canvas'); cv.width = cv.height = 32;
    const g = cv.getContext('2d', { willReadFrequently: true }); g.drawImage(img, 0, 0, 32, 32);
    const p = g.getImageData(0, 0, 32, 32).data; let r = 0, gg = 0;
    for (let i = 0; i < p.length; i += 4) { r += p[i]; gg += p[i + 1]; }
    r /= 1024; gg /= 1024;
    ok = Math.abs(r - 128) < 14 && Math.abs(gg - 128) < 14; nmOk.set(img, ok);
  }
  if (!ok) { m.userData.badNormal = t; m.normalMap = null; m.needsUpdate = true; }
}   // imagen original -> {tono: textura}

function isSkin(r, g, b) { return r > 150 && g > 130 && b < 110 && Math.abs(r - g) < 80 && (r + g) / 2 - b > 110; }

function recolor(tex, hex) {
  const img = tex.image; if (!img?.width) return null;
  const per = done.get(img) || new Map(); done.set(img, per);
  if (per.has(hex)) return per.get(hex);
  const w = Math.min(img.width, 1024), h = Math.round(img.height * w / img.width);
  const cv = document.createElement('canvas'); cv.width = w; cv.height = h;
  const g = cv.getContext('2d', { willReadFrequently: true }); g.drawImage(img, 0, 0, w, h);
  const d = g.getImageData(0, 0, w, h), p = d.data;
  const t = new THREE.Color(hex), tr = t.r * 255, tg = t.g * 255, tb = t.b * 255;
  let n = 0;
  for (let i = 0; i < p.length; i += 4) {
    if (!isSkin(p[i], p[i + 1], p[i + 2])) continue;
    const k = Math.min(1.15, (p[i] + p[i + 1]) / 510 + 0.05);       // sombreado horneado en el amarillo
    p[i] = Math.min(255, tr * k); p[i + 1] = Math.min(255, tg * k); p[i + 2] = Math.min(255, tb * k); n++;
  }
  let out = null;
  if (n > 0) {
    g.putImageData(d, 0, 0);
    out = new THREE.CanvasTexture(cv);
    Object.assign(out, { flipY: tex.flipY, wrapS: tex.wrapS, wrapT: tex.wrapT, colorSpace: tex.colorSpace, anisotropy: 4 });
  }
  per.set(hex, out);
  return out;
}

// sustituye el mapa de color (y lo recuerda como textura "real" para el interruptor de texturas)
export function applySkin(root, hex = DEFAULT_SKIN) {
  root.traverse(o => {
    if (!o.isMesh) return;
    for (const m of [].concat(o.material)) checkNormalMap(m);
    for (const m of [].concat(o.material)) {
      const src = m.userData.srcMap || m.userData.texMap || m.map;
      // sin textura de color: es piel que el juego compone en tiempo real (manos, brazos, cuello)
      if (!src) {
        if (m.color && !m.transparent && SKIN_MESH.test(o.name) && m.userData.template === SKIN_TEMPLATE) { m.userData.skinColor = true; m.color.set(hex); m.needsUpdate = true; }
        continue;
      }
      m.userData.srcMap = src;
      const t = recolor(src, hex);
      if (!t) continue;
      const showing = m.map === (m.userData.texMap ?? m.map);
      m.userData.texMap = t;
      if (showing) { m.map = t; m.needsUpdate = true; }
    }
  });
}
