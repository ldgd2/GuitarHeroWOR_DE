// Animaciones con nombre (ademas de Entrada, Bucle y Salida): se hacen en la linea de tiempo como cualquier clip
// (claves en una, varias capas o grupos) y se lanzan desde el codigo:
//
//   play_anim("pulso")                      la reproduce en las capas donde se hizo
//   play_anim("pulso", "Boton 2")           la misma animacion en otra capa (relativa a como este esa capa)
//   play_anim("pulso", loop=True)           en bucle hasta stop_anim("pulso")
//   play_anim("pulso", wait_end=True)       espera a que acabe
//   stop_anim("pulso")  /  stop_anim("pulso", "Boton 2")  /  anim_names()
//
// Se convierten en codigo wui (animSource), que corre igual en la vista previa y en el juego.
// Una pista de un grupo se reparte entre sus capas (moverlo mueve a todas; escala, giro y opacidad a cada una).
const layerName = l => String(l.name || l.id).split(/\s+/).filter(Boolean).join(' ');   // como scripts.js

export const isAnimClip = (k, c) => !!c?.custom;
export const animClips = doc => Object.entries(doc.clips || {}).filter(([k, c]) => isAnimClip(k, c));

// curvas del editor -> curvas de wui (las de wui son menos: la mas parecida)
const WUI_EASE = { linear: 'linear', hold: 'step', steps: 'step', easeIn: 'in_cubic', easeOut: 'out_cubic', easeInOut: 'inout_cubic',
  quadIn: 'in', quadOut: 'out', quadInOut: 'inout', sineIn: 'in', sineOut: 'out', sineInOut: 'inout',
  cubicIn: 'in_cubic', cubicOut: 'out_cubic', cubicInOut: 'inout_cubic', backIn: 'in_back', backOut: 'out_back',
  elasticOut: 'out_elastic', bounceOut: 'out_bounce', bounceIn: 'in_bounce' };
const wuiEase = e => WUI_EASE[e || 'linear'] || (/InOut$/.test(e) ? 'inout_cubic' : /Out$/.test(e) ? 'out_cubic' : /In$/.test(e) ? 'in_cubic' : 'linear');

// propiedad del editor -> [propiedad de wui, como se relaciona con la base: abs | add | mul | col]
const PROP = { x: ['x', 'abs'], y: ['y', 'abs'], scale: ['scale', 'mul'], rot: ['rot', 'add'], alpha: ['alpha', 'mul'],
  color: ['tint', 'col'], e_glow: ['glow', 'abs'], e_glowc: ['glow_color', 'col'], e_glowa: ['glow_alpha', 'abs'],
  e_blur: ['blur', 'abs'], e_bright: ['brightness', 'abs'] };
const NOT_IN_CODE = { e_chroma: 'aberracion cromatica', e_sat: 'saturacion', e_inv: 'invertir', e_hue: 'tono', w: 'ancho', h: 'alto', depth: 'profundidad' };

export const animIdent = n => (String(n || 'anim').toLowerCase().normalize('NFD').replace(/[^a-z0-9_]+/g, '_').replace(/^_+|_+$/g, '') || 'anim');
const num = v => { const x = Number(v); return Number.isFinite(x) ? (Math.round(x * 10000) / 10000).toString() : '0'; };
const lit = v => (Number.isInteger(+v) ? `${num(v)}.0` : num(v));
const str = s => JSON.stringify(String(s));
const hex = c => '#' + (Array.isArray(c) ? c : [255, 255, 255]).slice(0, 3).map(v => Math.round(Math.max(0, Math.min(255, v))).toString(16).padStart(2, '0')).join('');

/** valor de wui para una capa a partir del valor del editor (v) y su base (b); g = grupo del que viene (o null) */
function toWui(kind, prop, v, b, g) {
  if (kind === 'col') {
    const base = b.color || [255, 255, 255];
    return prop === 'color' ? hex(v.map((x, i) => (base[i] ? x / base[i] * 255 : x))) : hex(v);
  }
  if (g) {   // pista de un grupo repartida en esta capa
    const gb = g.base;
    if (prop === 'x' || prop === 'y') return b[prop] + (v - (gb[prop] || 0));
    if (prop === 'scale') return (gb.scale ? v / gb.scale : v);
    if (prop === 'rot') return v - (gb.rot || 0);
    if (prop === 'alpha') return gb.alpha ? v / gb.alpha : v;
    return v;
  }
  if (prop === 'scale') return b.scale ? v / b.scale : v;
  if (prop === 'alpha') return b.alpha ? v / b.alpha : v;
  if (prop === 'rot') return v - (b.rot || 0);
  return v;
}

/** {text, warnings}: el codigo wui de todas las animaciones con nombre del proyecto */
export function animSource(proj) {
  const anims = [];   // {name, fn, dur, tracks: [{layer, prop, wprop, kind, keys: [{t, v (wui), d (para relativo), ease}]}]}
  const warnings = [];
  const seen = new Set();
  for (const s of proj.screens || []) {
    const byId = Object.fromEntries((s.layers || []).map(l => [l.id, l]));
    const kids = id => (s.layers || []).filter(l => l.parent === id);
    const leaves = id => kids(id).flatMap(l => (l.type === 'group' ? leaves(l.id) : [l]));
    for (const [, c] of animClips(s)) {
      const name = String(c.name || 'animacion');
      if (seen.has(name)) { warnings.push(`hay dos animaciones "${name}": se usa la de la ultima pantalla`); }
      seen.add(name);
      const a = { name, fn: `_anim_${animIdent(name)}_${anims.length}`, dur: Math.max(0.01, +c.duration || 1), tracks: [] };
      for (const [lid, props] of Object.entries(c.tracks || {})) {
        const src = byId[lid];
        if (!src) continue;
        const targets = src.type === 'group' ? leaves(src.id).map(l => [l, src]) : [[src, null]];
        for (const [prop, ks] of Object.entries(props || {})) {
          if (!ks?.length) continue;
          if (!PROP[prop]) { if (NOT_IN_CODE[prop]) warnings.push(`"${name}": ${NOT_IN_CODE[prop]} no se puede animar desde el codigo (se ignora)`); continue; }
          const [wprop, kind] = PROP[prop];
          const sorted = [...ks].sort((p, q) => p.t - q.t);
          for (const [l, g] of targets) {
            if (l.type === 'camera') continue;
            const keys = sorted.map(k => ({ t: k.t, ease: k.ease, v: toWui(kind, prop, k.v, l.base, g), raw: k.v }));
            a.tracks.push({ layer: layerName(l), prop, wprop, kind, keys });
          }
        }
      }
      anims.push(a);
    }
  }
  if (!anims.length) return { text: '', warnings };

  const L = ['# Animaciones hechas en la linea de tiempo (codigo generado: se cambian alli, no aqui).',
    '# play_anim("nombre"), play_anim("nombre", "otra capa"), play_anim("nombre", loop=True), stop_anim("nombre")', ''];
  anims.forEach(a => {
    a.tracks.forEach((tr, i) => {
      const f = `${a.fn}_${i}`;
      const k0 = tr.keys[0];
      L.push(`def ${f}(who):`);
      L.push(`    if who == "":`);
      L.push(`        c = layer(${str(tr.layer)})`);
      L.push(`        k = [${tr.keys.map(k => (tr.kind === 'col' ? str(k.v) : lit(k.v))).join(', ')}]`);
      if (tr.kind === 'col') {
        L.push(`    else:`);
        L.push(`        c = layer(who)`);
        L.push(`        k = [${tr.keys.map(k => str(k.v)).join(', ')}]`);
      } else {
        L.push(`    else:`);
        L.push(`        c = layer(who)`);
        L.push(`        b = c.${tr.wprop}`);
        // relativo: misma variacion respecto a la primera clave, sobre como este la otra capa
        const rel = tr.keys.map(k => {
          if (tr.kind === 'mul') { const r = k0.raw ? k.raw / k0.raw : 1; return `b * ${lit(r)}`; }
          return `b + ${lit(k.raw - k0.raw)}`;
        });
        L.push(`        k = [${rel.join(', ')}]`);
      }
      if (k0.t > 0) L.push(`    wait(${lit(k0.t)})`);
      L.push(`    c.${tr.wprop} = k[0]`);
      for (let j = 1; j < tr.keys.length; j++) {
        const dt = Math.max(0, tr.keys[j].t - tr.keys[j - 1].t);
        L.push(`    c.animate(${lit(dt)}, ${str(wuiEase(tr.keys[j].ease))}, wait=True, ${tr.wprop}=k[${j}])`);
      }
      L.push('');
    });
    L.push(`def ${a.fn}(who, subs):`);
    if (!a.tracks.length) L.push('    pass');
    a.tracks.forEach((_, i) => L.push(`    subs.append(spawn(${a.fn}_${i}, who))`));
    L.push(`    wait(${lit(a.dur)})`);
    L.push('');
  });
  L.push(`_ANIM_NAMES = [${anims.map(a => str(a.name)).join(', ')}]`);
  L.push(`_ANIM_FNS = [${anims.map(a => a.fn).join(', ')}]`);
  L.push('_anim_keys = []');
  L.push('_anim_runs = []');
  L.push('');
  L.push(...`def anim_names():
    return _ANIM_NAMES

def _anim_find(name):
    i = 0
    for n in _ANIM_NAMES:
        if n == name:
            return i
        i += 1
    return -1

def _anim_body(i, who, loop, subs):
    f = _ANIM_FNS[i]
    f(who, subs)
    while loop:
        f(who, subs)

def stop_anim(name, target=""):
    key = f"{name}|{target}"
    i = 0
    while i < len(_anim_keys):
        if _anim_keys[i] == key:
            run = _anim_runs[i]
            run[0].stop()
            for s in run[1]:
                s.stop()
            _anim_keys.pop(i)
            _anim_runs.pop(i)
        else:
            i += 1

def play_anim(name, target="", loop=False, wait_end=False):
    i = _anim_find(name)
    if i < 0:
        print(f"no hay ninguna animacion llamada {name}")
        return None
    stop_anim(name, target)
    subs = []
    t = spawn(_anim_body, i, target, loop, subs)
    _anim_keys.append(f"{name}|{target}")
    _anim_runs.append([t, subs])
    if wait_end:
        t.join()
    return t
`.split('\n'));
  return { text: L.join('\n'), warnings };
}
