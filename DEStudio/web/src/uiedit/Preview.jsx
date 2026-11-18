// Vista previa interactiva: la interfaz funcionando como en el juego (runtime.js = misma logica que re-wor) y, si
// tiene, su logica en Python (wui) corriendo en la maquina JS (logic.js), igual que dentro del juego.
// Teclado como mando: flechas (foco y rasgueo), Intro o Espacio (elegir / verde), Esc (atras / rojo),
// 1-5 (trastes verde, rojo, amarillo, azul, naranja), Tab (start), Retroceso (back / star). Tambien el raton.
// Lo que tocaria el juego (scripts, variables, sonidos) se apunta en el registro; las variables se pueden cambiar.
import React, { useEffect, useMemo, useRef, useState } from 'react';
import { Icon } from '../ui/icons.jsx';
import { Num } from '../studio/fields.jsx';
import { Runtime, KEYS } from './runtime.js';
import { PreviewLogic, PAD_KEYS, compileLogic } from './logic.js';
import { programText } from './scripts.js';
import { Stage2D } from './Stage2D.jsx';
import { Stage3D } from './Stage3D.jsx';
import { W, actionOf, condVar } from './model.js';

const OPTS = { bg: 'black', snap: false, guides: false };
const b64bytes = b => Uint8Array.from(atob(b), c => c.charCodeAt(0));

export function Preview({ proj, close }) {
  const rt = useRef(null);
  const lg = useRef(null);
  const held = useRef(new Set());
  const stageRef = useRef(null);
  const [, force] = useState(0);
  const [log, setLog] = useState([]);
  const [logicState, setLogicState] = useState('');   // '', 'compilando', 'ok', 'error: ...'
  const [view, setView] = useState({ z: 0.5, x: 0, y: 0, fit: true });
  const source = useMemo(() => programText(proj), [proj]);
  const vars = useMemo(() => {
    const out = new Set();
    for (const s of proj.screens) for (const l of s.layers) {
      if (condVar(l.showIf)) out.add(condVar(l.showIf));
      if (l.ui?.focus && actionOf(l).type === 'set' && actionOf(l).global) out.add(actionOf(l).global);
    }
    for (const m of source.matchAll(/game\.(?:get|set)\(\s*["']\$?([A-Za-z_][A-Za-z0-9_]*)["']/g)) out.add(m[1]);
    return [...out];
  }, [proj, source]);
  const say = m => setLog(l => [...l.slice(-60), m]);

  const start = async () => {
    const r = new Runtime(proj, say);
    if (rt.current) r.vars = rt.current.vars;
    r.open(); rt.current = r; lg.current = null; setLog([]);
    if (!source.trim()) { setLogicState(''); return; }
    setLogicState('compilando');
    try {
      const c = await compileLogic(proj, source);
      if (rt.current !== r) return;
      if (!c.ok) {
        const e = (c.diagnostics || []).find(d => d.severity === 'error');
        setLogicState('error: ' + (e ? `linea ${e.line}: ${e.message}` : 'no compila'));
        return;
      }
      lg.current = new PreviewLogic(r, b64bytes(c.wuib), say);
      setLogicState('ok');
    } catch (e) {
      setLogicState('error: ' + e.message);
    }
  };
  useEffect(() => {
    start();
    let raf, last = performance.now();
    const tick = now => {
      const dt = Math.min(0.1, (now - last) / 1000);
      last = now;
      rt.current.step(dt);
      lg.current?.step(dt, held.current);
      force(x => x + 1);
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, []);
  useEffect(() => {
    const nav = { ArrowUp: KEYS.up, ArrowDown: KEYS.down, ArrowLeft: KEYS.left, ArrowRight: KEYS.right, Enter: KEYS.choose, ' ': KEYS.choose, Escape: KEYS.back, Backspace: KEYS.back };
    const down = e => {
      if (/INPUT|SELECT|TEXTAREA/.test(e.target.tagName)) return;
      const pad = PAD_KEYS[e.key];
      if (!pad && !nav[e.key]) return;
      e.preventDefault(); e.stopPropagation();
      if (pad) for (const b of pad) held.current.add(b);
      if (e.repeat || !nav[e.key]) return;
      const locked = lg.current?.locked;
      if (locked && nav[e.key] !== KEYS.back) return;
      rt.current?.input(nav[e.key]);
    };
    const up = e => { const pad = PAD_KEYS[e.key]; if (pad) for (const b of pad) held.current.delete(b); };
    const blur = () => held.current.clear();
    window.addEventListener('keydown', down, true);
    window.addEventListener('keyup', up, true);
    window.addEventListener('blur', blur);
    return () => { window.removeEventListener('keydown', down, true); window.removeEventListener('keyup', up, true); window.removeEventListener('blur', blur); };
  }, []);

  // raton en px del editor (para mouse_x, hovered...)
  const onMouse = e => {
    const L = lg.current, el = stageRef.current?.querySelector('.ue-board');
    if (!L || !el) return;
    const b = el.getBoundingClientRect();
    L.mouse = [(e.clientX - b.left) * W / b.width, (e.clientY - b.top) * W / b.width, e.buttons === 1];
  };

  const r = rt.current, L = lg.current;
  const fr = r?.frame();
  const overrides = it => {
    const o = r.overrides(it);
    if (!L || o?.hide) return o;
    const vm = L.look(it.l);
    return vm ? { ...o, vm } : o;
  };
  const pick = (id, how) => {
    if (!id) return;
    if (how === 'click') {
      const l = r.doc.layers.find(x => x.id === id);
      if (r.F.some(x => x.id === id)) { if (!L?.locked) r.click(id); }
      else if (l && L) L.click(l);
    } else if (!L?.locked) r.hover(id);
  };
  const errors = L?.vm.errors || [];
  return (
    <div className="ue-preview">
      <header>
        <b><Icon n="play" />Vista previa</b>
        <span className="st-muted">{r ? (fr ? `${proj.screens[r.screen].name}  .  ${r.mode === 'intro' ? 'entrada' : r.mode === 'loop' ? 'bucle' : 'salida'}` : 'cerrada') : ''}</span>
        {logicState && <span className={'ue-logicstate' + (logicState.startsWith('error') ? ' err' : '')}>
          <Icon n="code" />{logicState === 'ok' ? 'Logica en marcha' : logicState === 'compilando' ? 'Compilando la logica' : 'Logica: ' + logicState.slice(7)}</span>}
        <span className="st-grow" />
        <span className="st-muted">Flechas, Intro, Esc, 1-5 trastes, Tab start, o el raton</span>
        <button onClick={start}><Icon n="reload" />Abrir de nuevo</button>
        <button onClick={close}><Icon n="x" />Cerrar</button>
      </header>
      <div className="ue-prevbody">
        <div className="ue-prevstage" ref={stageRef} onPointerMove={onMouse} onPointerDown={onMouse} onPointerUp={onMouse}>
          {!fr && <div className="ue-prevclosed"><p>La interfaz se ha cerrado.</p><button className="primary" onClick={start}><Icon n="play" />Abrir de nuevo</button></div>}
          {fr && proj.mode !== '3d' && <Stage2D proj={proj} doc={fr.doc} mode={proj.mode} clip={fr.clip} t={fr.t} sel={[]} setSel={() => {}} tool="move"
            edit={() => {}} live={null} opts={OPTS} view={view} setView={setView} readOnly overrides={overrides} onPick={pick}
            onListWheel={(x, y, d) => !L?.locked && !!r?.wheel(x, y, d)} />}
          {fr && proj.mode === '3d' && <Stage3D key={fr.doc.id} proj={proj} doc={fr.doc} clip={fr.clip} t={fr.t} sel={[]} setSel={() => {}} preview />}
        </div>
        <aside className="ue-prevside">
          <h4>Variables del juego (simuladas)</h4>
          {!vars.length && <div className="st-muted">Esta interfaz no usa variables.</div>}
          {vars.map(v => <label key={v} className="ue-row"><span>{v}</span><div><Num v={r?.vars[v] ?? 0} onChange={x => { r.vars[v] = Math.round(x); force(n => n + 1); }} /></div></label>)}
          {errors.length > 0 && <><h4>Errores de la logica</h4>
            <div className="ue-prevlog err">{errors.slice(-8).map((e, i) => <div key={i}>{e.function}(){e.line ? `, linea ${e.line}` : ''}: {e.message}</div>)}</div></>}
          <h4>Lo que haria en el juego</h4>
          <div className="ue-prevlog">{!log.length && <div className="st-muted">Pulsa un boton.</div>}{log.map((m, i) => <div key={i}>{m}</div>)}</div>
          {r && r.F.length > 0 && <><h4>Botones de esta pantalla</h4>
            <div className="ue-prevbtns">{r.F.map((l, i) => <button key={l.id} className={i === r.focus ? 'on' : ''} onClick={() => pick(l.id, 'click')}>{l.name}</button>)}</div></>}
        </aside>
      </div>
    </div>
  );
}
