// Utilidades de React del editor: bus de eventos -> re-render, montaje e iconos SVG.
import React, { useEffect, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { on } from '../core/state.js';

export { React };
export { useState, useEffect, useRef, useMemo, useCallback, useLayoutEffect, Fragment } from 'react';

// Vuelve a pintar el componente cuando se emite alguno de los eventos del bus (ver core/state.js).
const subs = new Map();
export function useBus(...events) {
  const [, force] = useState(0);
  useEffect(() => {
    const fn = () => force(n => n + 1);
    events.forEach(e => { if (!subs.has(e)) { subs.set(e, new Set()); on(e, () => subs.get(e).forEach(f => f())); } subs.get(e).add(fn); });
    return () => events.forEach(e => subs.get(e).delete(fn));
  }, []);
}

export function mount(id, Component) {
  const el = document.getElementById(id);
  if (el) createRoot(el).render(<Component />);
}

// iconos SVG pequenos (trazos), para no depender de fuentes externas
const P = {
  play: 'M6 4l14 8-14 8z', pause: 'M6 4h4v16H6zM14 4h4v16h-4z', undo: 'M9 14L4 9l5-5M4 9h11a5 5 0 010 10h-3',
  redo: 'M15 14l5-5-5-5M20 9H9a5 5 0 000 10h3', save: 'M5 3h11l3 3v15H5zM8 3v6h8V3M8 21v-7h8v7',
  build: 'M12 3v12M7 10l5 5 5-5M4 21h16', reload: 'M20 11a8 8 0 10-2.3 5.7M20 4v7h-7', history: 'M12 7v5l3 3M3 12a9 9 0 109-9 9 9 0 00-7 3.3M3 4v4h4',
  front: 'M3 7l9-4 9 4v10l-9 4-9-4z', top: 'M12 3v18M3 12h18', eye: 'M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12zM12 9a3 3 0 100 6 3 3 0 000-6',
  eyeoff: 'M3 3l18 18M10.6 5.1A10 10 0 0112 5c6 0 10 7 10 7a17 17 0 01-3.2 3.9M6.6 6.6C3.8 8.4 2 12 2 12s4 7 10 7a9.6 9.6 0 005.4-1.6',
  cam: 'M3 7h4l2-3h6l2 3h4v12H3zM12 10a3 3 0 100 6 3 3 0 000-6', tex: 'M4 4h16v16H4zM4 14l5-5 4 4 3-3 4 4',
  bone: 'M7 7l10 10M5 5a2 2 0 103 3M16 16a2 2 0 103 3', max: 'M4 9V4h5M20 9V4h-5M4 15v5h5M20 15v5h-5',
  plus: 'M12 5v14M5 12h14', x: 'M6 6l12 12M18 6L6 18', search: 'M11 4a7 7 0 100 14 7 7 0 000-14zM20 20l-4-4',
  file: 'M6 3h9l4 4v14H6zM14 3v5h5', clip: 'M4 6h16v12H4zM9 6v12M15 6v12', light: 'M9 18h6M10 21h4M12 3a6 6 0 00-3 11v2h6v-2a6 6 0 00-3-11',
  sun: 'M12 8a4 4 0 100 8 4 4 0 000-8M12 2v2M12 20v2M2 12h2M20 12h2M5 5l1.5 1.5M17.5 17.5L19 19M5 19l1.5-1.5M17.5 6.5L19 5',
  chev: 'M9 6l6 6-6 6', lock: 'M6 11h12v10H6zM8 11V7a4 4 0 018 0v4', unlock: 'M6 11h12v10H6zM8 11V7a4 4 0 017.5-2',
  dots: 'M5 12h.01M12 12h.01M19 12h.01', vol: 'M4 9h4l5-4v14l-5-4H4zM16 9a4 4 0 010 6M19 6a8 8 0 010 12',
  mute: 'M4 9h4l5-4v14l-5-4H4zM17 9l5 6M22 9l-5 6', fire: 'M12 3c1 4 5 5 5 10a5 5 0 01-10 0c0-3 2-4 2-7 2 1 3 3 3 5 1-2 1-5 0-8z',
  star: 'M12 3l2.6 5.6 6.1.7-4.5 4.2 1.2 6L12 16.6 6.6 19.5l1.2-6L3.3 9.3l6.1-.7z', music: 'M9 18V5l11-2v13M9 18a3 3 0 11-6 0 3 3 0 016 0zM20 16a3 3 0 11-6 0 3 3 0 016 0',
  folder: 'M3 6h6l2 2h10v11H3z', trash: 'M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13', edit: 'M4 20h4L19 9l-4-4L4 16zM14 6l4 4',
  user: 'M12 12a4 4 0 100-8 4 4 0 000 8zM4 21a8 8 0 0116 0', fx: 'M12 2l2 5 5 2-5 2-2 5-2-5-5-2 5-2z', cube: 'M12 3l8 4.5v9L12 21l-8-4.5v-9zM12 12l8-4.5M12 12v9M12 12L4 7.5',
};
export const Icon = ({ n, s = 16 }) => (
  <svg className="ico" width={s} height={s} viewBox="0 0 24 24" fill={n === 'play' || n === 'pause' ? 'currentColor' : 'none'}
    stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d={P[n] || ''} /></svg>
);
