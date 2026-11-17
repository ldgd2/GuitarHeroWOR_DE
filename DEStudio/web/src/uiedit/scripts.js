// Los scripts de una interfaz, como en Unity: uno del proyecto, uno por pantalla y uno por capa.
//   proyecto  proj.logic.source        lo general (variables, funciones comunes); apunta a la pantalla principal
//   pantalla  screen.logic.source      lo de esa pantalla
//   capa      layer.script.source      lo de esa capa (si no tiene, su codigo es el que sale de su panel)
// Al compilar se juntan en un solo programa de wui (assemble); los errores vuelven a su parte (locate).
// Misma forma de juntar en DEStudio/editor/ui/logic.py (exportar): si cambias algo aqui, cambialo alli.
import { layerScript } from './blocks.js';

export const screenSafe = n => (n || '').toLowerCase().replace(/[^a-z0-9_]+/g, '_').replace(/^_+|_+$/g, '').slice(0, 32) || 'pantalla';
export const layerName = l => String(l.name || l.id).split(/\s+/).filter(Boolean).join(' ');
const fn = n => (n || 'x').toLowerCase().normalize('NFD').replace(/[^a-z0-9_]+/g, '_').replace(/^_+|_+$/g, '').replace(/^(\d)/, '_$1') || 'x';

/** borrador editable: {p: proyecto, 's:<id>': pantalla, 'l:<id>': capa} (solo lo que tiene codigo propio) */
export function draftOf(proj) {
  const d = { p: proj.logic?.source || '' };
  for (const s of proj.screens || []) {
    if (s.logic?.source != null) d['s:' + s.id] = s.logic.source;
    for (const l of s.layers || []) if (l.script?.source != null) d['l:' + l.id] = l.script.source;
  }
  return d;
}

/** las partes en orden: proyecto, y por pantalla la pantalla y sus capas con codigo propio */
export function parts(proj, d = draftOf(proj)) {
  const out = [{ key: 'p', kind: 'project', title: 'Proyecto', src: d.p || '' }];
  for (const s of proj.screens || []) {
    out.push({ key: 's:' + s.id, kind: 'screen', id: s.id, title: s.name, src: d['s:' + s.id] ?? '', own: d['s:' + s.id] != null });
    for (const l of s.layers || []) if (d['l:' + l.id] != null)
      out.push({ key: 'l:' + l.id, kind: 'layer', id: l.id, sid: s.id, title: layerName(l), src: d['l:' + l.id], own: true });
  }
  return out;
}

const KIND = { project: 'proyecto', screen: 'pantalla', layer: 'capa' };
/** un solo programa: {text, map: [{key, from, to}]} (lineas de 1 en adelante) */
export function assemble(ps) {
  const lines = [], map = [];
  for (const p of ps) {
    if (!p.src.trim()) continue;
    lines.push(`# ===== ${KIND[p.kind]} ${p.title} =====`);
    const body = p.src.replace(/\n+$/, '').split('\n');
    map.push({ key: p.key, from: lines.length + 1, to: lines.length + body.length });
    lines.push(...body, '');
  }
  return { text: lines.join('\n'), map };
}
export function locate(map, line) {
  const m = map.find(x => line >= x.from && line <= x.to);
  return m ? { key: m.key, line: line - m.from + 1 } : { key: 'p', line: 0 };
}
export const programText = proj => assemble(parts(proj)).text;

/** lo que se ve al abrir una parte que aun no tiene codigo propio */
export function template(proj, key) {
  if (key === 'p') {
    const st = proj.screens.find(s => s.id === proj.start) || proj.screens[0];
    return `# Proyecto "${proj.name}". Pantalla principal: "${screenSafe(st?.name)}" (se elige arriba).\n` +
      '# Aqui va lo de toda la interfaz: variables, funciones comunes y eventos generales.\n\n' +
      '@on_start\ndef al_abrir():\n    pass\n';
  }
  const [k, id] = key.split(':');
  if (k === 's') {
    const s = proj.screens.find(x => x.id === id), n = screenSafe(s?.name);
    return `# Pantalla "${s?.name}": lo que pasa mientras esta abierta.\n\n` +
      `@on_enter(${JSON.stringify(n)})\ndef al_entrar_${fn(n)}():\n    pass\n\n` +
      `@on_exit(${JSON.stringify(n)})\ndef al_salir_${fn(n)}():\n    pass\n`;
  }
  for (const s of proj.screens) {
    const l = s.layers.find(x => x.id === id);
    if (l) return layerScript(l, layerName(l), proj).split('\n').filter(x => !x.startsWith('# --- ')).join('\n') + '\n';
  }
  return '';
}
