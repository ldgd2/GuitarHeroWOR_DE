// Archivos de proyecto del estudio (.desp) desde el navegador: la biblioteca vive en el servidor
// (<estudio>/proyectos); "Abrir archivo" trae un .desp de cualquier carpeta y "Guardar copia" lo escribe donde
// elijas (dialogo nativo en Edge/Chrome; en otros navegadores se descarga).
export const EXT = '.desp';

export async function api(path, body) {
  const opt = body === undefined ? undefined
    : body instanceof ArrayBuffer || body instanceof Blob ? { method: 'POST', body }
      : { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) };
  const r = await fetch(path, opt);
  const ct = r.headers.get('Content-Type') || '';
  const j = ct.includes('json') ? await r.json() : null;
  if (!r.ok || (j && j.error)) throw new Error(j?.error || `${r.status} ${r.statusText}`);
  return j;
}

export const listProjects = kind => api('/api/files' + (kind ? '?kind=' + kind : ''));

/** elige un .desp del disco y lo copia a la biblioteca. Devuelve {file, kind, name, song, mode} o null */
export function openFromDisk() {
  return new Promise((resolve, reject) => {
    const inp = document.createElement('input');
    inp.type = 'file'; inp.accept = EXT;
    inp.onchange = async () => {
      const f = inp.files[0];
      if (!f) return resolve(null);
      try { resolve(await api('/api/files/import?name=' + encodeURIComponent(f.name.replace(/\.desp$/i, '')), await f.arrayBuffer())); }
      catch (e) { reject(e); }
    };
    inp.click();
  });
}

/** copia del .desp de la biblioteca en una carpeta a elegir */
export async function saveCopyToDisk(file) {
  const r = await fetch('/api/files/download?file=' + encodeURIComponent(file));
  if (!r.ok) throw new Error('No se pudo leer el proyecto ' + file);
  const blob = await r.blob();
  if (window.showSaveFilePicker) {
    try {
      const h = await window.showSaveFilePicker({ suggestedName: file + EXT,
        types: [{ description: 'Proyecto de DE Studio', accept: { 'application/octet-stream': [EXT] } }] });
      const w = await h.createWritable(); await w.write(blob); await w.close();
      return h.name;
    } catch (e) { if (e.name === 'AbortError') return null; }
  }
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob); a.download = file + EXT; a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 4000);
  return file + EXT;
}

/** a que pagina va cada tipo de proyecto */
export function editorUrl(item) {
  if (item.kind === 'song') return `cancion.html?song=${encodeURIComponent(item.song || '')}`;
  return `ui.html#${encodeURIComponent(item.file)}`;
}
