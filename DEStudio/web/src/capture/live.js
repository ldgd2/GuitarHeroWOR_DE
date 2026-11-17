// Flujo en vivo interpolado: las muestras llegan a su ritmo (telefono 15-30/s, camara del PC 30-60/s, con huecos y
// rafagas por la red) y la vista y la grabacion necesitan un valor en cada fotograma. Se reproduce un poco en el
// pasado (algo mas que el intervalo medio entre muestras) interpolando entre las dos que rodean ese instante: el
// movimiento sale continuo aunque las muestras sean pocas o irregulares. Ese retraso se descuenta al grabar.
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));

export function liveStream(lerp) {
  return { buf: [], dt: 33, lerp };
}

export function pushSample(s, v, at = performance.now()) {
  const last = s.buf.at(-1);
  if (last) {
    const d = at - last.at;
    if (d < 1) { last.v = v; return; }
    if (d > 500) s.buf.length = 0;                     // hueco largo (se perdio la deteccion): se empieza de cero
    else s.dt = s.dt * 0.85 + clamp(d, 5, 250) * 0.15;
  }
  s.buf.push({ at, v });
  if (s.buf.length > 12) s.buf.shift();
}

// retraso de reproduccion (ms): lo justo para tener casi siempre la muestra siguiente
export const delayOf = s => clamp(s.dt * 1.25, 0, 160);

export function sampleAt(s, now = performance.now()) {
  const b = s.buf; if (!b.length) return null;
  const t = now - delayOf(s);
  if (t <= b[0].at) return b[0].v;
  for (let i = b.length - 1; i > 0; i--) {
    if (b[i - 1].at <= t) {
      if (t >= b[i].at) return b[i].v;
      const k = (t - b[i - 1].at) / (b[i].at - b[i - 1].at);
      return s.lerp(b[i - 1].v, b[i].v, k);
    }
  }
  return b.at(-1).v;
}
export const clearStream = s => { s.buf.length = 0; };

export const lerpArray = (a, b, k) => (a.length === b.length ? a.map((x, i) => x + (b[i] - x) * k) : b);
export function lerpObject(a, b, k) {
  const o = {};
  for (const key in b) {
    const x = a[key], y = b[key];
    o[key] = typeof x === 'number' && typeof y === 'number' ? x + (y - x) * k : y;
  }
  return o;
}
