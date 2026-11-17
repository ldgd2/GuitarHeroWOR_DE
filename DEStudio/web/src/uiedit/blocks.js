// Reglas por bloques ("Cuando ... hacer ...") de la logica de una interfaz: se guardan en proj.logic.rules y se
// convierten en Python de wui dentro de una zona marcada del codigo (proj.logic.source). Lo que se exporta y lo
// que corre en el juego es siempre ese unico programa en Python.

export const BEGIN = '# --- reglas del editor de bloques (generado: cambialas en la pestana Reglas) ---';
export const END = '# --- fin de las reglas ---';

const BUTTONS = ['green', 'red', 'yellow', 'blue', 'orange', 'strum_up', 'strum_down', 'up', 'down', 'left', 'right',
  'start', 'back', 'star', 'choose', 'cancel'];
export const BUTTON_LABEL = {
  green: 'Verde', red: 'Rojo', yellow: 'Amarillo', blue: 'Azul', orange: 'Naranja', strum_up: 'Rasgueo arriba',
  strum_down: 'Rasgueo abajo', up: 'Arriba', down: 'Abajo', left: 'Izquierda', right: 'Derecha', start: 'Start',
  back: 'Back', star: 'Star Power', choose: 'Elegir', cancel: 'Cancelar',
};
export const SOUNDS = ['select', 'back', 'ScrollUp', 'ScrollDown', 'ToggleOn', 'ToggleOff', 'negativeselect', 'warningmessage'];
const PROPS = ['x', 'y', 'scale', 'rot', 'alpha', 'brightness', 'glow', 'blur', 'fill'];

// campos: t = tipo de editor (layer, screen, button, num, text, color, sound, prop, var)
export const EVENTS = {
  start: { label: 'Al abrirse la interfaz', fields: [] },
  enter: { label: 'Al entrar en la pantalla', fields: [{ k: 'screen', t: 'screen' }] },
  press: { label: 'Al pulsar el boton', fields: [{ k: 'button', t: 'button' }] },
  release: { label: 'Al soltar el boton', fields: [{ k: 'button', t: 'button' }] },
  hold: { label: 'Al mantener el boton', fields: [{ k: 'button', t: 'button' }, { k: 'seconds', t: 'num', label: 'segundos', d: 1 }] },
  click: { label: 'Al elegir la capa', fields: [{ k: 'layer', t: 'layer' }] },
  focus: { label: 'Al enfocar la capa', fields: [{ k: 'layer', t: 'layer' }] },
  blur: { label: 'Al perder el foco la capa', fields: [{ k: 'layer', t: 'layer' }] },
  every: { label: 'Cada', fields: [{ k: 'seconds', t: 'num', label: 'segundos', d: 1 }] },
  game: { label: 'Al cambiar la variable del juego', fields: [{ k: 'var', t: 'var' }] },
};

export const ACTIONS = {
  glow: { label: 'Resplandor', fields: [{ k: 'layer', t: 'layer' }, { k: 'size', t: 'num', label: 'tamano', d: 24 }, { k: 'color', t: 'color', d: '#ffd21f' }] },
  unglow: { label: 'Quitar resplandor', fields: [{ k: 'layer', t: 'layer' }] },
  blur: { label: 'Desenfocar', fields: [{ k: 'layer', t: 'layer' }, { k: 'px', t: 'num', label: 'px', d: 6 }] },
  shadow: { label: 'Sombra', fields: [{ k: 'layer', t: 'layer' }, { k: 'size', t: 'num', label: 'desenfoque', d: 12 }, { k: 'color', t: 'color', d: '#000000' }] },
  outline: { label: 'Contorno', fields: [{ k: 'layer', t: 'layer' }, { k: 'width', t: 'num', label: 'grosor', d: 3 }, { k: 'color', t: 'color', d: '#ffffff' }] },
  tint: { label: 'Tenir', fields: [{ k: 'layer', t: 'layer' }, { k: 'color', t: 'color', d: '#ff2a2a' }] },
  untint: { label: 'Quitar tinte', fields: [{ k: 'layer', t: 'layer' }] },
  animate: { label: 'Animar', fields: [{ k: 'layer', t: 'layer' }, { k: 'prop', t: 'prop' }, { k: 'value', t: 'num', label: 'hasta', d: 1 }, { k: 'time', t: 'num', label: 'segundos', d: 0.3 }] },
  show: { label: 'Mostrar', fields: [{ k: 'layer', t: 'layer' }] },
  hide: { label: 'Ocultar', fields: [{ k: 'layer', t: 'layer' }] },
  fade_in: { label: 'Aparecer', fields: [{ k: 'layer', t: 'layer' }, { k: 'time', t: 'num', label: 'segundos', d: 0.3 }] },
  fade_out: { label: 'Desvanecer', fields: [{ k: 'layer', t: 'layer' }, { k: 'time', t: 'num', label: 'segundos', d: 0.3 }] },
  pulse: { label: 'Latir', fields: [{ k: 'layer', t: 'layer' }] },
  shake: { label: 'Sacudir', fields: [{ k: 'layer', t: 'layer' }] },
  bounce: { label: 'Rebotar', fields: [{ k: 'layer', t: 'layer' }] },
  flash: { label: 'Destello', fields: [{ k: 'layer', t: 'layer' }, { k: 'color', t: 'color', d: '#ffffff' }] },
  text: { label: 'Cambiar texto', fields: [{ k: 'layer', t: 'layer' }, { k: 'text', t: 'text', d: '' }] },
  goto: { label: 'Ir a la pantalla', fields: [{ k: 'screen', t: 'screen' }] },
  back: { label: 'Volver atras', fields: [] },
  close: { label: 'Cerrar la interfaz', fields: [] },
  sound: { label: 'Sonido', fields: [{ k: 'sound', t: 'sound', d: 'select' }] },
  set: { label: 'Cambiar variable del juego', fields: [{ k: 'var', t: 'var' }, { k: 'value', t: 'num', label: 'valor', d: 1 }] },
  call: { label: 'Ejecutar script del juego', fields: [{ k: 'script', t: 'var' }] },
  wait: { label: 'Esperar', fields: [{ k: 'seconds', t: 'num', label: 'segundos', d: 0.5 }] },
  log: { label: 'Escribir en el registro', fields: [{ k: 'text', t: 'text', d: 'hola' }] },
};
export const FIELD_OPTIONS = { button: BUTTONS, sound: SOUNDS, prop: PROPS };

const q = s => JSON.stringify(String(s ?? ''));
const n = v => { const x = Number(v); return Number.isFinite(x) ? String(x) : '0'; };
const ident = s => /^[A-Za-z_][A-Za-z0-9_]*$/.test(s || '') ? s : null;
const L = name => `layer(${q(name)})`;

export function newRule(ev = 'press') {
  const when = { ev };
  for (const f of EVENTS[ev].fields) when[f.k] = f.d ?? (f.t === 'button' ? 'green' : '');
  return { id: Math.random().toString(36).slice(2, 9), when, actions: [], on: true };
}
export function newAction(op) {
  const a = { op };
  for (const f of ACTIONS[op].fields) a[f.k] = f.d ?? (f.t === 'prop' ? 'scale' : '');
  return a;
}

function decorator(w) {
  switch (w.ev) {
    case 'start': return '@on_start';
    case 'enter': return w.screen ? `@on_enter(${q(w.screen)})` : '@on_enter()';
    case 'press': return `@on_press(Button.${(w.button || 'green').toUpperCase()})`;
    case 'release': return `@on_release(Button.${(w.button || 'green').toUpperCase()})`;
    case 'hold': return `@on_hold(Button.${(w.button || 'green').toUpperCase()}, seconds=${n(w.seconds)})`;
    case 'click': return w.layer ? `@on_click(${q(w.layer)})` : '@on_click()';
    case 'focus': return w.layer ? `@on_focus(${q(w.layer)})` : '@on_focus()';
    case 'blur': return w.layer ? `@on_blur(${q(w.layer)})` : '@on_blur()';
    case 'every': return `@every(${n(Math.max(0.05, +w.seconds || 1))})`;
    case 'game': return `@on_game(${q(w.var)})`;
    default: return null;
  }
}

function action(a) {
  const lay = a.layer ? L(a.layer) : null;
  const needLayer = ACTIONS[a.op]?.fields.some(f => f.t === 'layer');
  if (needLayer && !lay) return `pass  # ${ACTIONS[a.op].label}: falta la capa`;
  switch (a.op) {
    case 'glow': return `${lay}.glow(${n(a.size)}, ${q(a.color)})`;
    case 'unglow': return `${lay}.unglow()`;
    case 'blur': return `${lay}.blur(${n(a.px)})`;
    case 'shadow': return `${lay}.shadow(${n(a.size)}, ${q(a.color)})`;
    case 'outline': return `${lay}.outline(${n(a.width)}, ${q(a.color)})`;
    case 'tint': return `${lay}.tint(${q(a.color)})`;
    case 'untint': return `${lay}.untint()`;
    case 'animate': return `${lay}.animate(${PROPS.includes(a.prop) ? a.prop : 'scale'}=${n(a.value)}, time=${n(a.time)})`;
    case 'show': return `${lay}.show()`;
    case 'hide': return `${lay}.hide()`;
    case 'fade_in': return `${lay}.fade_in(${n(a.time)})`;
    case 'fade_out': return `${lay}.fade_out(${n(a.time)})`;
    case 'pulse': return `${lay}.pulse()`;
    case 'shake': return `${lay}.shake()`;
    case 'bounce': return `${lay}.bounce()`;
    case 'flash': return `${lay}.flash(${q(a.color)})`;
    case 'text': return `${lay}.set_text(${q(a.text)})`;
    case 'goto': return a.screen ? `goto(${q(a.screen)})` : 'pass  # Ir a la pantalla: falta la pantalla';
    case 'back': return 'back()';
    case 'close': return 'close()';
    case 'sound': return `sound(${q(a.sound || 'select')})`;
    case 'set': return ident(a.var) ? `game.set(${q(a.var)}, ${n(a.value)})` : 'pass  # Cambiar variable: nombre no valido';
    case 'call': return ident(a.script) ? `game.call(${q(a.script)})` : 'pass  # Ejecutar script: nombre no valido';
    case 'wait': return `wait(${n(a.seconds)})`;
    case 'log': return `log(${q(a.text)})`;
    default: return 'pass';
  }
}

/** Python de todas las reglas (con sus marcas), o '' si no hay ninguna activa */
export function rulesToPython(rules) {
  const on = (rules || []).filter(r => r.on !== false && decorator(r.when));
  if (!on.length) return '';
  const out = [BEGIN];
  on.forEach((r, i) => {
    out.push(decorator(r.when));
    out.push(`def regla_${i + 1}():`);
    const body = (r.actions || []).map(action);
    for (const line of body.length ? body : ['pass']) out.push('    ' + line);
    out.push('');
  });
  out.push(END);
  return out.join('\n');
}

/** el codigo con la zona de reglas sustituida (o anadida al final) */
export function withRules(source, rules) {
  const gen = rulesToPython(rules);
  const src = source || '';
  const a = src.indexOf(BEGIN), b = src.indexOf(END);
  if (a >= 0 && b > a) {
    const before = src.slice(0, a).replace(/\n+$/, ''), after = src.slice(b + END.length).replace(/^\n+/, '');
    return [before, gen, after].filter(Boolean).join('\n\n') + '\n';
  }
  if (!gen) return src;
  return (src.replace(/\n+$/, '') ? src.replace(/\n+$/, '') + '\n\n' : '') + gen + '\n';
}

// ------------------------------------------------------------------ script de una capa (como un componente)
const toHex = c => '#' + (c || [255, 255, 255]).map(v => Math.max(0, Math.min(255, Math.round(v))).toString(16).padStart(2, '0')).join('');
const fnName = n => (n || 'capa').toLowerCase().normalize('NFD').replace(/[^a-z0-9_]+/g, '_').replace(/^_+|_+$/g, '').replace(/^(\d)/, '_$1') || 'capa';
export const scriptBegin = name => `# --- script de la capa ${JSON.stringify(name)} ---`;
export const scriptEnd = name => `# --- fin del script de ${JSON.stringify(name)} ---`;

/** Python de una capa con lo que ya tenia en el panel (boton, accion, efecto de foco); el panel queda neutro */
export function layerScript(l, name, proj) {
  const f = fnName(name), q2 = s => JSON.stringify(String(s));
  const out = [scriptBegin(name)];
  const ui = l.ui || {};
  if (ui.focus) {
    const h = ui.hover || {}, b = l.base || {};
    const bx = +(b.x ?? 640), by = +(b.y ?? 360);
    out.push(`# Es un boton: el mando y el raton le dan el foco. Esto era su configuracion del panel.`);
    out.push(`@on_focus(${q2(name)})`, `def ${f}_al_enfocar(capa):`);
    out.push(`    capa.animate(scale=${n(h.scale ?? 1)}, x=${n(bx + (+h.dx || 0))}, y=${n(by + (+h.dy || 0))}, alpha=${n(h.alpha ?? 1)}, time=0.12)`);
    if (h.color && h.color.some(v => v !== 255)) out.push(`    capa.tint(${q2(toHex(h.color))}, time=0.12)`);
    out.push('', `@on_blur(${q2(name)})`, `def ${f}_al_perder_foco(capa):`);
    out.push(`    capa.animate(scale=1, x=${n(bx)}, y=${n(by)}, alpha=1, time=0.12)`);
    if (h.color && h.color.some(v => v !== 255)) out.push(`    capa.untint(time=0.12)`);
    out.push('', `@on_click(${q2(name)})`, `def ${f}_al_elegir(capa):`);
    const a = ui.action || {};
    const scr = proj.screens.find(s => s.id === a.screen);
    if (a.type === 'goto' && scr) out.push(`    goto(${q2((scr.name || '').toLowerCase().replace(/[^a-z0-9_]+/g, '_').replace(/^_+|_+$/g, '').slice(0, 32) || 'pantalla')})`);
    else if (a.type === 'back') out.push('    back()');
    else if (a.type === 'close') out.push('    close()');
    else if (a.type === 'set' && a.global) out.push(`    game.set(${q2(a.global)}, ${n(a.value ?? 1)})`);
    else if (a.type === 'script' && a.script) {
      const [s0, ...rest] = a.script.trim().split(/\s+/);
      const kv = rest.join(' ').match(/([A-Za-z_]\w*)\s*=\s*("[^"]*"|\S+)/g) || [];
      const args = kv.map(p => { const [k, v] = p.split(/\s*=\s*/); return `${k}=${/^-?\d+(\.\d+)?$/.test(v) || v.startsWith('"') ? v : q2(v)}`; });
      out.push(`    game.call(${[q2(s0), ...args].join(', ')})`);
    } else out.push('    pass  # lo que hace al elegirlo');
  } else {
    out.push(`@on_start`, `def ${f}_al_empezar():`, `    capa = layer(${q2(name)})`, '    pass  # p. ej. capa.pulse() o capa.glow(16, "gold")');
  }
  out.push(scriptEnd(name));
  return out.join('\n');
}

/** pasa la capa a codigo: quita del panel la accion y el efecto de foco (ahora los hace el script) */
export function neutralizePanel(l) {
  if (!l.ui?.focus) return;
  l.ui.action = { type: 'none' };
  l.ui.hover = { ...(l.ui.hover || {}), scale: 1, dx: 0, dy: 0, alpha: 1, color: [255, 255, 255] };
}
