// Ventana "Logica": el comportamiento de la interfaz en Python (wui, tools/wui), como los scripts de Unity:
//   Proyecto   lo general; desde aqui se elige la pantalla principal
//   Pantallas  un script por pantalla
//   Capas      un script por capa: sin editar es el que sale de su panel; al editarlo pasa a ser codigo propio
// Pestanas: Codigo (editor con errores al momento), Reglas (bloques "Cuando ... hacer ..." en el script del
// proyecto) y Referencia (funciones, metodos, propiedades y eventos con ejemplos).
import React, { useEffect, useMemo, useRef, useState } from 'react';
import { Icon } from '../ui/icons.jsx';
import { Num } from '../studio/fields.jsx';
import { compileLogic, logicApi } from './logic.js';
import { ACTIONS, BUTTON_LABEL, EVENTS, FIELD_OPTIONS, neutralizePanel, newAction, newRule, withRules } from './blocks.js';
import { assemble, draftOf, layerName, locate, parts, screenSafe as safe, template } from './scripts.js';

/** escribe el borrador en el proyecto (capas que pasan a codigo: el panel deja de hacerlo, con copia para volver) */
export function applyDraft(p, d, rules) {
  p.logic = { ...(p.logic || {}), source: d.p || '', rules };
  for (const s of p.screens) {
    const sk = 's:' + s.id;
    if (d[sk] != null) s.logic = { source: d[sk] }; else delete s.logic;
    for (const l of s.layers) {
      const k = 'l:' + l.id;
      if (d[k] != null) {
        if (!l.script) {
          l.script = { source: d[k], panel: l.ui ? { action: structuredClone(l.ui.action || {}), hover: structuredClone(l.ui.hover || {}) } : null };
          neutralizePanel(l);
        } else l.script.source = d[k];
      } else if (l.script) {
        if (l.script.panel && l.ui) { l.ui.action = l.script.panel.action; l.ui.hover = l.script.panel.hover; }
        delete l.script;
      }
    }
  }
}

export function LogicWindow({ proj, editProj, close, start }) {
  const [tab, setTab] = useState(start?.tab || 'code');
  const [key, setKey] = useState(start?.key || 'p');
  const [draft, setDraft] = useState(() => draftOf(proj));
  const [rules, setRules] = useState(() => proj.logic?.rules || []);
  const [diag, setDiag] = useState({ state: 'idle', list: [] });
  const [api, setApi] = useState(null);
  const [pendingJump, setPendingJump] = useState(null);
  const ta = useRef(null);

  const own = draft[key] != null || key === 'p';
  const src = key === 'p' ? (draft.p || '') : (draft[key] ?? template(proj, key));
  const setSrc = v => setDraft(d => ({ ...d, [key]: typeof v === 'function' ? v(d[key] ?? src) : v }));

  // guardar en el proyecto (sin llenar el deshacer en cada tecla; abrir la ventana no cambia nada)
  const first = useRef(true);
  useEffect(() => {
    if (first.current) { first.current = false; return undefined; }
    const t = setTimeout(() => editProj(p => applyDraft(p, draft, rules), false), 300);
    return () => clearTimeout(t);
  }, [draft, rules]);
  // compilar todo el programa al momento; cada error vuelve a su parte
  const prog = useMemo(() => assemble(parts(proj, draft)), [proj.screens, draft]);
  useEffect(() => {
    if (!prog.text.trim()) { setDiag({ state: 'idle', list: [] }); return undefined; }
    setDiag(d => ({ ...d, state: 'busy' }));
    let live = true;
    const t = setTimeout(() => compileLogic(proj, prog.text)
      .then(r => live && setDiag({ state: r.ok ? 'ok' : 'err',
        list: (r.diagnostics || []).map(x => ({ ...x, ...(x.line ? locate(prog.map, x.line) : { key: 'p', line: 0 }) })) }))
      .catch(e => live && setDiag({ state: 'err', list: [{ severity: 'error', line: 0, key: 'p', message: e.message }] })), 450);
    return () => { live = false; clearTimeout(t); };
  }, [prog.text]);
  useEffect(() => { logicApi().then(setApi).catch(() => setApi({ functions: [], methods: [], events: [], properties: [] })); }, []);
  useEffect(() => {
    const k = e => { if (e.key === 'Escape') { e.stopPropagation(); close(); } };
    window.addEventListener('keydown', k, true);
    return () => window.removeEventListener('keydown', k, true);
  }, []);

  const setRulesAndCode = next => { setRules(next); setDraft(d => ({ ...d, p: withRules(d.p || '', next) })); };
  const insert = text => {
    setTab('code');
    const el = ta.current;
    const at = el ? el.selectionStart : src.length;
    const before = src.slice(0, at), after = src.slice(at);
    setSrc(before + (before && !before.endsWith('\n') ? '\n' : '') + text + '\n' + after);
  };
  const jump = (k, line) => { setTab('code'); setKey(k); setPendingJump(line || null); };
  useEffect(() => {
    if (!pendingJump || tab !== 'code') return;
    const el = ta.current;
    if (!el) return;
    const lines = src.split('\n');
    const pos = lines.slice(0, pendingJump - 1).reduce((a, l) => a + l.length + 1, 0);
    el.focus();
    el.setSelectionRange(pos, pos + (lines[pendingJump - 1] || '').length);
    el.scrollTop = Math.max(0, (pendingJump - 4) * 18);
    setPendingJump(null);
  }, [pendingJump, key, tab]);

  const errs = diag.list.filter(d => d.severity === 'error'), warns = diag.list.filter(d => d.severity === 'warning');
  const here = diag.list.filter(d => d.key === key);
  const count = k => diag.list.filter(d => d.key === k && d.severity === 'error').length;
  const titleOf = k => {
    if (k === 'p') return 'Proyecto';
    const [t, id] = k.split(':');
    for (const s of proj.screens) {
      if (t === 's' && s.id === id) return `Pantalla ${s.name}`;
      const l = t === 'l' && s.layers.find(x => x.id === id);
      if (l) return `Capa ${layerName(l)} (${s.name})`;
    }
    return k;
  };
  const isLayer = key.startsWith('l:');

  return (
    <div className="st-modal" onMouseDown={close}>
      <div className="st-modalbox ue-logicwin" onMouseDown={e => e.stopPropagation()}>
        <h3><Icon n="code" />Logica<span className="st-muted ue-logicsub">Python que corre en el juego mientras la interfaz esta abierta</span>
          <span className="st-grow" />
          <button className="st-icon" onClick={close} title="Cerrar (Esc)"><Icon n="x" /></button></h3>
        <div className="ue-tabs ue-logictabs">
          <button className={tab === 'code' ? 'on' : ''} onClick={() => setTab('code')}><Icon n="code" s={14} />Codigo</button>
          <button className={tab === 'rules' ? 'on' : ''} onClick={() => setTab('rules')}><Icon n="flow" s={14} />Reglas<span className="ue-count">{rules.length}</span></button>
          <button className={tab === 'ref' ? 'on' : ''} onClick={() => setTab('ref')}><Icon n="info" s={14} />Referencia</button>
          <span className="st-grow" />
          <span className={'ue-logicstate ' + (diag.state === 'err' ? 'err' : diag.state === 'ok' ? 'ok' : '')}>
            {diag.state === 'busy' ? 'Comprobando' : diag.state === 'ok' ? (warns.length ? `Compila, ${warns.length} aviso${warns.length > 1 ? 's' : ''}` : 'Compila sin errores')
              : diag.state === 'err' ? `${errs.length} error${errs.length !== 1 ? 'es' : ''}` : 'Sin codigo'}</span>
        </div>
        <div className="ue-logicbody">
          {tab === 'code' && <nav className="ue-scripttree">
            <button className={'ue-st proj' + (key === 'p' ? ' on' : '')} onClick={() => setKey('p')}>
              <Icon n="package" s={14} /><span>Proyecto</span>{count('p') > 0 && <em>{count('p')}</em>}</button>
            {proj.screens.map(s => <React.Fragment key={s.id}>
              <button className={'ue-st scr' + (key === 's:' + s.id ? ' on' : '') + (draft['s:' + s.id] != null ? ' own' : '')} onClick={() => setKey('s:' + s.id)}>
                <Icon n="screen" s={14} /><span>{s.name}</span>{proj.start === s.id && <i>principal</i>}{count('s:' + s.id) > 0 && <em>{count('s:' + s.id)}</em>}</button>
              {s.layers.filter(l => ['image', 'text', 'shape'].includes(l.type)).map(l =>
                <button key={l.id} className={'ue-st lay' + (key === 'l:' + l.id ? ' on' : '') + (draft['l:' + l.id] != null ? ' own' : '')} onClick={() => setKey('l:' + l.id)}
                  title={draft['l:' + l.id] != null ? 'Codigo propio' : 'Codigo de su panel (sin editar)'}>
                  <Icon n={l.type === 'text' ? 'text' : l.type === 'image' ? 'tex' : 'vector'} s={13} /><span>{layerName(l)}</span>
                  {l.ui?.focus && <i>boton</i>}{count('l:' + l.id) > 0 && <em>{count('l:' + l.id)}</em>}</button>)}
            </React.Fragment>)}
          </nav>}
          {tab === 'code' && <div className="ue-scriptpane">
            <div className="ue-scripthead">
              <b>{titleOf(key)}</b>
              {key === 'p' && <label className="ue-inline">Pantalla principal
                <select value={proj.start || ''} onChange={e => editProj(p => { p.start = e.target.value; })}>
                  {proj.screens.map(s => <option key={s.id} value={s.id}>{s.name}</option>)}</select></label>}
              {!own && <span className="st-muted">{isLayer ? 'Este codigo sale de su panel. Al cambiarlo pasa a ser codigo propio y el panel deja de hacerlo.' : 'Aun sin codigo: lo que escribas se guarda.'}</span>}
              <span className="st-grow" />
              {own && key !== 'p' && <button onClick={() => { setDraft(d => { const n = { ...d }; delete n[key]; return n; }); }}
                title={isLayer ? 'Borra su codigo y el panel vuelve a hacer lo que hacia' : 'Borra el codigo de esta pantalla'}>
                <Icon n="undo" />{isLayer ? 'Volver al panel' : 'Quitar el codigo'}</button>}
            </div>
            <CodeEditor src={src} setSrc={setSrc} diag={here} taRef={ta} />
          </div>}
          {tab === 'rules' && <RulesEditor proj={proj} rules={rules} setRules={setRulesAndCode} />}
          {tab === 'ref' && <Reference api={api} insert={insert} />}
        </div>
        {diag.list.length > 0 && tab !== 'ref' && <div className="ue-logicdiag">
          {diag.list.slice(0, 40).map((d, i) => <button key={i} className={d.severity} onClick={() => jump(d.key, d.line)}>
            <Icon n={d.severity === 'error' ? 'alert' : 'warn'} s={13} />
            <span className="ue-dline">{titleOf(d.key)}{d.line ? `, linea ${d.line}` : ''}</span>
            <span>{d.message}{d.hint ? <i> {d.hint}</i> : null}</span></button>)}
        </div>}
      </div>
    </div>
  );
}


// ------------------------------------------------------------------ codigo
function CodeEditor({ src, setSrc, diag, taRef }) {
  const gut = useRef(null);
  const lines = src.split('\n').length;
  const bad = useMemo(() => {
    const m = new Map();
    for (const d of diag) if (d.line && (d.severity === 'error' || !m.has(d.line))) m.set(d.line, d.severity);
    return m;
  }, [diag]);
  const edit = (value, a, b) => {
    setSrc(value);
    requestAnimationFrame(() => { const el = taRef.current; if (el) el.setSelectionRange(a, b ?? a); });
  };
  const onKey = e => {
    const el = e.target, s = el.selectionStart, t = el.selectionEnd, v = el.value;
    if (e.key === 'Tab') {
      e.preventDefault();
      const ls = v.lastIndexOf('\n', s - 1) + 1;
      if (e.shiftKey) {
        const cut = v.slice(ls).match(/^ {1,4}/)?.[0].length || 0;
        if (cut) edit(v.slice(0, ls) + v.slice(ls + cut), Math.max(ls, s - cut), Math.max(ls, t - cut));
      } else if (s !== t && v.slice(s, t).includes('\n')) {
        const block = v.slice(ls, t).split('\n').map(l => '    ' + l).join('\n');
        edit(v.slice(0, ls) + block + v.slice(t), s + 4, ls + block.length);
      } else edit(v.slice(0, s) + '    ' + v.slice(t), s + 4);
    } else if (e.key === 'Enter') {
      e.preventDefault();
      const ls = v.lastIndexOf('\n', s - 1) + 1, cur = v.slice(ls, s);
      let ind = cur.match(/^ */)[0];
      if (/:\s*(#.*)?$/.test(cur)) ind += '    ';
      edit(v.slice(0, s) + '\n' + ind + v.slice(t), s + 1 + ind.length);
    }
  };
  return (
    <div className="ue-code">
      <div className="ue-gutter" ref={gut}>
        {Array.from({ length: lines }, (_, i) => <div key={i} className={bad.get(i + 1) || ''}>{i + 1}</div>)}
      </div>
      <textarea ref={taRef} value={src} spellCheck={false} wrap="off" onChange={e => setSrc(e.target.value)} onKeyDown={onKey}
        onScroll={e => { if (gut.current) gut.current.scrollTop = e.target.scrollTop; }} />
    </div>
  );
}

// ------------------------------------------------------------------ reglas (bloques)
function RulesEditor({ proj, rules, setRules }) {
  const layers = useMemo(() => [...new Set(proj.screens.flatMap(s => s.layers.map(layerName)))], [proj]);
  const screens = useMemo(() => proj.screens.map(s => safe(s.name)), [proj]);
  const upd = (i, fn) => setRules(rules.map((r, k) => (k === i ? fn(structuredClone(r)) || r : r)));
  const move = (i, d) => { const j = i + d; if (j < 0 || j >= rules.length) return; const n = [...rules]; [n[i], n[j]] = [n[j], n[i]]; setRules(n); };
  return (
    <div className="ue-rules">
      {!rules.length && <div className="ue-rulesempty"><Icon n="flow" s={28} />
        <p>Una regla une un momento (pulsar un boton, entrar en una pantalla, cada cierto tiempo...) con lo que pasa
          (efectos en capas, ir a otra pantalla, sonidos, variables del juego). Se convierten en Python en la pestana Codigo.</p></div>}
      {rules.map((r, i) => (
        <div key={r.id} className={'ue-rule' + (r.on === false ? ' off' : '')}>
          <div className="ue-rulehead">
            <label className="ue-chk" title="Activa"><input type="checkbox" checked={r.on !== false} onChange={e => upd(i, x => { x.on = e.target.checked; })} /></label>
            <b>Cuando</b>
            <select value={r.when.ev} onChange={e => upd(i, () => ({ ...newRule(e.target.value), id: r.id, actions: r.actions, on: r.on }))}>
              {Object.entries(EVENTS).map(([k, v]) => <option key={k} value={k}>{v.label}</option>)}
            </select>
            {EVENTS[r.when.ev].fields.map(f => <Field key={f.k} f={f} v={r.when[f.k]} layers={layers} screens={screens}
              set={v => upd(i, x => { x.when[f.k] = v; })} />)}
            <span className="st-grow" />
            <button className="st-icon" onClick={() => move(i, -1)} title="Subir"><Icon n="up" s={14} /></button>
            <button className="st-icon" onClick={() => move(i, 1)} title="Bajar"><Icon n="down" s={14} /></button>
            <button className="st-icon" onClick={() => setRules([...rules.slice(0, i + 1), { ...structuredClone(r), id: newRule().id }, ...rules.slice(i + 1)])} title="Duplicar"><Icon n="duplicate" s={14} /></button>
            <button className="st-icon" onClick={() => setRules(rules.filter((_, k) => k !== i))} title="Borrar la regla"><Icon n="trash" s={14} /></button>
          </div>
          <div className="ue-ruleacts">
            {r.actions.map((a, j) => (
              <div key={j} className="ue-ruleact">
                <span className="ue-rulestep">{j + 1}</span>
                <select value={a.op} onChange={e => upd(i, x => { x.actions[j] = { ...newAction(e.target.value), layer: a.layer }; })}>
                  {Object.entries(ACTIONS).map(([k, v]) => <option key={k} value={k}>{v.label}</option>)}
                </select>
                {ACTIONS[a.op].fields.map(f => <Field key={f.k} f={f} v={a[f.k]} layers={layers} screens={screens}
                  set={v => upd(i, x => { x.actions[j][f.k] = v; })} />)}
                <span className="st-grow" />
                <button className="st-icon" onClick={() => upd(i, x => { if (j > 0) [x.actions[j - 1], x.actions[j]] = [x.actions[j], x.actions[j - 1]]; })} title="Subir"><Icon n="up" s={13} /></button>
                <button className="st-icon" onClick={() => upd(i, x => { x.actions.splice(j, 1); })} title="Quitar"><Icon n="x" s={13} /></button>
              </div>))}
            <select className="ue-addact" value="" onChange={e => { const op = e.target.value; if (op) upd(i, x => { x.actions.push(newAction(op)); }); }}>
              <option value="">Anadir accion...</option>
              {Object.entries(ACTIONS).map(([k, v]) => <option key={k} value={k}>{v.label}</option>)}
            </select>
          </div>
        </div>))}
      <button className="ue-addrule" onClick={() => setRules([...rules, newRule('press')])}><Icon n="plus" />Regla nueva</button>
    </div>
  );
}

function Field({ f, v, set, layers, screens }) {
  const lab = f.label ? <span className="ue-flabel">{f.label}</span> : null;
  switch (f.t) {
    case 'layer': return <select value={v || ''} onChange={e => set(e.target.value)}>
      <option value="">(capa)</option>{layers.map(n => <option key={n} value={n}>{n}</option>)}</select>;
    case 'screen': return <select value={v || ''} onChange={e => set(e.target.value)}>
      <option value="">(cualquiera)</option>{screens.map(n => <option key={n} value={n}>{n}</option>)}</select>;
    case 'button': return <select value={v || 'green'} onChange={e => set(e.target.value)}>
      {FIELD_OPTIONS.button.map(b => <option key={b} value={b}>{BUTTON_LABEL[b]}</option>)}</select>;
    case 'sound': case 'prop': return <select value={v} onChange={e => set(e.target.value)}>
      {FIELD_OPTIONS[f.t].map(b => <option key={b} value={b}>{b}</option>)}</select>;
    case 'num': return <>{lab}<Num v={+v || 0} step={0.1} onChange={set} /></>;
    case 'color': return <input type="color" value={/^#[0-9a-f]{6}$/i.test(v) ? v : '#ffffff'} onChange={e => set(e.target.value)} />;
    case 'var': return <input className="ue-fvar" value={v || ''} placeholder="nombre" onChange={e => set(e.target.value.replace(/[^A-Za-z0-9_]/g, ''))} />;
    default: return <input className="ue-ftext" value={v || ''} onChange={e => set(e.target.value)} />;
  }
}

// ------------------------------------------------------------------ referencia
const RECV = { Layer: 'capa', list: 'lista', str: 'texto', Task: 'tarea' };
const CAT_ORDER = ['eventos', 'capas', 'mando', 'pantallas', 'tiempo', 'juego', 'sonido', 'matematicas', 'nucleo'];
const CAT_LABEL = { eventos: 'Eventos', capas: 'Capas y efectos', mando: 'Mando', pantallas: 'Pantallas y foco', tiempo: 'Tiempo y tareas',
  juego: 'El juego', sonido: 'Sonido', matematicas: 'Matematicas', nucleo: 'Basico', props: 'Propiedades de las capas' };

const pyv = v => (v === null || v === undefined ? 'None' : v === true ? 'True' : v === false ? 'False' : JSON.stringify(v));
/** firma legible: sin self y con el receptor en espanol (capa.glow(...), lista.append(...)) */
function prettySig(f) {
  let s = f.signature || f.name;
  if (f.receiver) s = s.replace(new RegExp('^' + f.receiver + '\\.'), (RECV[f.receiver] || f.receiver) + '.');
  return s.replace(/\(self,\s*/, '(').replace(/\(self\)/, '()');
}
/** lo que se inserta si no hay ejemplo: la llamada con sus argumentos obligatorios */
function callOf(f) {
  const ps = (f.params || []).filter(p => p.name !== 'self' && p.kind === 'normal' && !('default' in p));
  const head = f.receiver === 'Layer' ? 'layer("nombre").' : f.receiver ? (RECV[f.receiver] || 'x') + '.' : '';
  return `${head}${f.name}(${ps.map(p => p.name).join(', ')})`;
}

function Reference({ api, insert }) {
  const [q, setQ] = useState('');
  const [cat, setCat] = useState('eventos');
  const entries = useMemo(() => {
    if (!api) return [];
    const out = [];
    for (const e of api.events || []) out.push({ cat: 'eventos', key: 'e' + e.name,
      sig: `@${e.name}(${(e.params || []).map(p => !('default' in p) ? p.name : `${p.name}=${pyv(p.default)}`).join(', ')})`,
      doc: e.doc, ex: e.example, gives: e.gives?.length ? `recibe: ${e.gives.join(', ')}` : '' });
    for (const f of [...(api.functions || []), ...(api.methods || [])]) out.push({ cat: f.category || 'nucleo', key: (f.receiver || '') + '.' + f.name,
      sig: prettySig(f), doc: f.doc, ex: f.example || callOf(f), auto: !f.example, wait: f.blocking, ret: f.returns && f.returns !== 'nada' ? f.returns : '' });
    for (const p of api.properties || []) if (p.receiver === 'Layer') out.push({ cat: 'props', key: 'p' + p.name,
      sig: `capa.${p.name}`, doc: p.doc, ret: p.type, tags: [p.writable ? null : 'solo lectura', p.animatable ? 'animable' : null].filter(Boolean),
      ex: p.writable ? `layer("nombre").${p.name} = ${p.type === 'color' ? '"#ffffff"' : p.type === 'bool' ? 'True' : p.type === 'texto' ? '"hola"' : (p.default == null ? '0' : pyv(p.default))}` : `layer("nombre").${p.name}`, auto: true });
    return out;
  }, [api]);
  if (!api) return <div className="st-muted ue-refload">Cargando la referencia...</div>;
  const needle = q.trim().toLowerCase();
  const cats = [...CAT_ORDER.filter(c => entries.some(e => e.cat === c)), ...[...new Set(entries.map(e => e.cat))].filter(c => !CAT_ORDER.includes(c) && c !== 'props'), 'props'];
  const shown = entries.filter(e => (needle ? (e.sig + ' ' + (e.doc || '')).toLowerCase().includes(needle) : e.cat === cat));
  return (
    <div className="ue-ref">
      <nav className="ue-refnav">
        <div className="ue-refsearch"><Icon n="search" s={14} /><input value={q} onChange={e => setQ(e.target.value)} placeholder="Buscar" /></div>
        {cats.map(c => <button key={c} className={!needle && c === cat ? 'on' : ''} onClick={() => { setCat(c); setQ(''); }}>
          <span>{CAT_LABEL[c] || c}</span><em>{entries.filter(e => e.cat === c).length}</em></button>)}
      </nav>
      <div className="ue-reflist">
        <div className="ue-refhead">{needle ? `${shown.length} resultado${shown.length !== 1 ? 's' : ''} para "${q.trim()}"` : (CAT_LABEL[cat] || cat)}
          {!needle && api.categories?.[cat] && <span className="st-muted">{api.categories[cat]}</span>}</div>
        {!shown.length && <div className="st-muted">Nada encontrado.</div>}
        {shown.map(e => (
          <div key={e.key} className="ue-refitem">
            <div className="ue-refsig"><code>{e.sig}</code>
              {e.ret && <span className="ue-refret">{e.cat === 'props' ? e.ret : '-> ' + e.ret}</span>}
              {e.wait && <span className="ue-reftag">espera</span>}
              {(e.tags || []).map(t => <span key={t} className="ue-reftag">{t}</span>)}
              {needle && <span className="ue-reftag">{CAT_LABEL[e.cat] || e.cat}</span>}</div>
            {e.doc && <p>{e.doc}</p>}
            {e.gives && <p className="st-muted">{e.gives}</p>}
            {e.ex && <div className="ue-refex"><code className="ue-refcode">{e.ex}</code>
              <button onClick={() => insert(e.ex)} title="Insertar en el codigo"><Icon n="plus" s={12} />Insertar</button></div>}
          </div>))}
      </div>
    </div>
  );
}
