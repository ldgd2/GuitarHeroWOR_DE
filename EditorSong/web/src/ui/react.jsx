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

// iconos SVG del editor (ui/icons.jsx)
export { Icon } from './icons.jsx';
