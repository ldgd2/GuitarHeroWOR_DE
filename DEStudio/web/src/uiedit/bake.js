// Imagenes que el juego necesita: cada capa de texto, forma, SVG, imagen con estilos o imagen que no es PNG se
// dibuja aqui (studio/raster.js, lo mismo que se ve en el editor) a la calidad del proyecto y se manda como PNG con
// su margen. Las imagenes del juego y los PNG propios sin estilos van tal cual (el servidor los copia).
import { preload, rasterize } from '../studio/raster.js';
import { VISUAL, hasFx, layerUrl } from './view2d.js';

const maxKey = (doc, id, prop) => {
  let m = 0;
  for (const c of Object.values(doc.clips || {})) for (const k of c.tracks?.[id]?.[prop] || []) m = Math.max(m, +k.v || 0);
  return m;
};

export async function bakeTextures(proj, onProgress) {
  const quality = Math.max(1, Math.min(4, proj.settings?.quality || 2));
  const urlOf = l => layerUrl(proj, l);
  const jobs = [];
  for (const doc of proj.screens) for (const l of doc.layers) {
    if (!VISUAL.includes(l.type) || l.visible === false) continue;
    const plain = l.type === 'image' && !hasFx(l) && (!l.image?.custom || /\.png$/i.test(l.image.file || ''));
    if (!plain) jobs.push([doc, l]);
  }
  await preload(jobs.map(j => j[1]), urlOf);
  const out = {};
  for (const [i, [doc, l]] of jobs.entries()) {
    // escala mas grande que llega a tener (base, claves y foco) para que no se vea borrosa
    const grow = Math.max(1, l.base.scale || 1, maxKey(doc, l.id, 'scale'), l.ui?.focus ? l.ui.hover?.scale || 1 : 1,
      proj.mode === '3d' ? 1.5 : 1);
    const r = rasterize(l, l.base.w, l.base.h, quality * grow, urlOf);
    out[l.id] = { png: r.canvas.toDataURL('image/png').split(',')[1], pad: r.pad, w: l.base.w, h: l.base.h };
    onProgress?.(i + 1, jobs.length);
    await new Promise(res => setTimeout(res));
  }
  return out;
}
