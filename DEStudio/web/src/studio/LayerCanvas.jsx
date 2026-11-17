// Una capa dibujada en un <canvas> a la resolucion de la pantalla (los SVG, textos y formas siguen nitidos a
// cualquier zoom). Las imagenes ya dibujadas se guardan en una cache por contenido y escala (escalones de raiz de 2),
// asi mover, girar o cambiar la opacidad no vuelve a dibujar nada. El tinte (multiplicar) se aplica al pintar.
import React, { useEffect, useRef, useState } from 'react';
import { onRasterChange, rasterKey, rasterize } from './raster.js';

const cache = new Map();   // clave -> {canvas, pad, scale, ready}
const MAX = 260;
function cached(l, w, h, pxScale, urlOf) {
  const q = Math.pow(2, Math.ceil(Math.log2(Math.max(0.03, pxScale)) * 2) / 2);   // escalones de raiz de 2
  const key = rasterKey(l, w, h) + '|' + q + '|' + (l.type === 'image' ? urlOf(l) : '');
  let e = cache.get(key);
  if (!e || !e.ready) {
    e = rasterize(l, w, h, q, urlOf);
    cache.set(key, e);
    if (cache.size > MAX) cache.delete(cache.keys().next().value);
  } else { cache.delete(key); cache.set(key, e); }
  return e;
}
export { cached as cachedRaster };

/** hook: se vuelve a pintar cuando llega una imagen o una fuente */
export function useRasterVersion() {
  const [v, setV] = useState(0);
  useEffect(() => onRasterChange(setV), []);
  return v;
}

/**
 * l, w, h: capa y su tamano en unidades; scale: pixeles de pantalla por unidad; tint [r,g,b]; urlOf(l) -> url.
 * El canvas mide (w + 2 pad) * scale y se coloca con el margen hacia fuera (style del contenedor del llamador).
 */
export function LayerCanvas({ l, w, h, scale, tint, urlOf, style, className }) {
  const ref = useRef(null);
  const ver = useRasterVersion();
  const dpr = window.devicePixelRatio || 1;
  const r = cached(l, w, h, scale * dpr, urlOf);
  const tintKey = tint ? tint.map(Math.round).join(',') : '255,255,255';
  useEffect(() => {
    const c = ref.current;
    if (!c) return;
    c.width = r.canvas.width; c.height = r.canvas.height;
    const g = c.getContext('2d');
    g.clearRect(0, 0, c.width, c.height);
    g.drawImage(r.canvas, 0, 0);
    if (tintKey !== '255,255,255') {
      g.globalCompositeOperation = 'multiply';
      g.fillStyle = `rgb(${tintKey})`; g.fillRect(0, 0, c.width, c.height);
      g.globalCompositeOperation = 'destination-in';
      g.drawImage(r.canvas, 0, 0);
      g.globalCompositeOperation = 'source-over';
    }
  }, [r, tintKey, ver]);
  const pad = r.pad;
  return <canvas ref={ref} className={className} style={{ position: 'absolute', left: -pad * scale, top: -pad * scale,
    width: (w + pad * 2) * scale, height: (h + pad * 2) * scale, ...style }} />;
}
