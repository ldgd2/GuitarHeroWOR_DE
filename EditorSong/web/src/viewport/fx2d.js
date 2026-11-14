// Efectos de imagen dibujados (particulas, rayos, laseres, iris, VHS...). Deterministas: dependen solo del
// tiempo, asi el editor y el hook de re-wor (re-wor/src/de/edfx.cpp, DrawFx2D) pintan EXACTAMENTE lo mismo.
// Si cambias algo aqui, cambialo tambien alli.
//   fx: { tipo: {k, el, left} } (ver activeFx), T: segundos de cancion, W/H: tamano en pixeles
const fr = x => x - Math.floor(x);
export const hash = (i, s) => fr(Math.sin(i * 12.9898 + s * 78.233) * 43758.5453);
const PAL = [[255, 60, 80], [255, 210, 40], [60, 220, 120], [60, 160, 255], [200, 90, 255], [255, 140, 30]];
const rgba = (c, a) => `rgba(${c[0]},${c[1]},${c[2]},${Math.max(0, Math.min(1, a))})`;
const smooth = k => { k = Math.max(0, Math.min(1, k)); return k * k * (3 - 2 * k); };

export const FX2D = ['scanlines', 'grain', 'vhs', 'iris', 'spotlight', 'heartbeat', 'confetti', 'sparks', 'snow', 'rain',
  'lightning', 'rainbow', 'smoke', 'lasers'];

function hole(c, W, H, x, y, r, a) {          // todo negro salvo un circulo
  c.fillStyle = `rgba(0,0,0,${a})`; c.beginPath(); c.rect(0, 0, W, H); c.arc(x, y, Math.max(0, r), 0, Math.PI * 2, true); c.fill();
}
function scan(c, W, H, a) { const st = Math.max(2, H / 240); c.fillStyle = `rgba(0,0,0,${0.22 * a})`; for (let y = 0; y < H; y += st) c.fillRect(0, y, W, Math.max(1, st / 3)); }
function grain(c, W, H, T, a, n = 500) {
  const f = Math.floor(T * 24), s = Math.max(1, H / 480);
  for (let i = 0; i < n; i++) { c.fillStyle = hash(i, f + 0.7) > 0.5 ? `rgba(255,255,255,${0.35 * a})` : `rgba(0,0,0,${0.35 * a})`; c.fillRect(hash(i, f) * W, hash(i, f + 0.5) * H, s, s); }
}
function hsv(h) { const k = n => { const x = (n + h * 6) % 6; return 255 * (1 - Math.max(0, Math.min(1, Math.min(x, 4 - x)))); }; return [k(5), k(3), k(1)]; }

export function drawFx2D(c, fx, T, W, H) {
  const g = k => fx[k]?.k || 0, D = Math.hypot(W, H);
  if (g('rainbow')) { c.fillStyle = rgba(hsv(fr(T * 0.2)), 0.18 * g('rainbow')); c.fillRect(0, 0, W, H); }
  if (fx.smoke) { const a = g('smoke'), el = fx.smoke.el;
    for (let i = 0; i < 14; i++) { const life = fr(hash(i, 3) + el * 0.05), x = fr(hash(i, 1) + el * 0.02 * (hash(i, 2) - 0.5)) * W,
      y = H - life * H * 0.6, r = H * (0.15 + 0.15 * hash(i, 4));
      const gr = c.createRadialGradient(x, y, 0, x, y, r); gr.addColorStop(0, `rgba(200,200,205,${0.16 * a * (1 - life)})`); gr.addColorStop(1, 'rgba(200,200,205,0)');
      c.fillStyle = gr; c.fillRect(x - r, y - r, 2 * r, 2 * r); } }
  if (fx.rain) { const a = g('rain'), el = fx.rain.el; c.strokeStyle = `rgba(180,200,255,${0.35 * a})`; c.lineWidth = Math.max(1, H / 720); c.beginPath();
    for (let i = 0; i < 200; i++) { const L = H * 0.05, y = fr(hash(i, 3) + el * (1.6 + 0.8 * hash(i, 2))) * (H + L) - L, x = hash(i, 1) * (W + H * 0.3) - y * 0.25;
      c.moveTo(x, y); c.lineTo(x - L * 0.25, y + L); } c.stroke(); }
  if (fx.snow) { const a = g('snow'), el = fx.snow.el; c.fillStyle = `rgba(255,255,255,${0.85 * a})`;
    for (let i = 0; i < 160; i++) { const y = fr(hash(i, 3) + el * (0.08 + 0.1 * hash(i, 2))) * H * 1.05, x = hash(i, 1) * W + Math.sin(el * 1.3 + i) * W * 0.015,
      r = H * (0.003 + 0.004 * hash(i, 4)); c.beginPath(); c.arc(x, y, r, 0, Math.PI * 2); c.fill(); } }
  if (fx.confetti) { const a = g('confetti'), el = fx.confetti.el, s = H * 0.012;
    for (let i = 0; i < 140; i++) { const y = fr(hash(i, 3) + el * (0.15 + 0.25 * hash(i, 2))) * H * 1.1 - H * 0.05,
      x = hash(i, 1) * W + Math.sin(el * 2 + i) * W * 0.02, an = el * 3 + i, w = s * Math.abs(Math.cos(an * 0.7));
      c.save(); c.translate(x, y); c.rotate(an); c.fillStyle = rgba(PAL[i % 6], a); c.fillRect(-s / 2, -w / 4, s, Math.max(1, w / 2)); c.restore(); } }
  if (fx.sparks) { const a = g('sparks'), el = fx.sparks.el;
    for (let i = 0; i < 90; i++) { const life = fr(hash(i, 3) + el * (0.3 + 0.4 * hash(i, 2))), y = H - life * H * 0.9,
      x = hash(i, 1) * W + Math.sin(el * 3 + i) * W * 0.01, r = H * 0.004 * (1.2 - life);
      c.fillStyle = `rgba(255,${150 + 100 * hash(i, 5) | 0},40,${a * (1 - life)})`; c.beginPath(); c.arc(x, y, Math.max(0.5, r), 0, Math.PI * 2); c.fill(); } }
  if (fx.lasers) { const a = g('lasers'); c.lineCap = 'round';
    for (let i = 0; i < 6; i++) { const ox = i % 2 ? W : 0, an = (i % 2 ? Math.PI - 1.0 : 1.0) + Math.sin(T * 1.5 + i) * 0.5 * (i % 2 ? -1 : 1),
      ex = ox + Math.cos(an) * D, ey = Math.sin(an) * D, col = PAL[[0, 2, 3, 4, 0, 2][i]];
      for (const [w, al] of [[H * 0.014, 0.25], [H * 0.004, 1]]) { c.strokeStyle = rgba(col, al * a); c.lineWidth = w; c.beginPath(); c.moveTo(ox, 0); c.lineTo(ex, ey); c.stroke(); } } }
  if (fx.lightning) { const a = g('lightning'), el = fx.lightning.el, seed = Math.floor(el / 1.6), ph = fr(el / 1.6);
    if (ph < 0.12) { const k = (0.12 - ph) / 0.12; c.fillStyle = `rgba(210,225,255,${0.7 * a * k})`; c.fillRect(0, 0, W, H);
      let x = (0.2 + 0.6 * hash(seed, 1)) * W, y = 0; const end = H * (0.6 + 0.3 * hash(seed, 2)), pts = [[x, y]];
      for (let j = 1; j <= 12; j++) { x += (hash(seed * 13 + j, 3) - 0.5) * W * 0.08; y = end * j / 12; pts.push([x, y]); }
      for (const [w, al] of [[H * 0.012, 0.3], [H * 0.003, 1]]) { c.strokeStyle = `rgba(255,255,255,${al * a * k})`; c.lineWidth = w; c.beginPath(); pts.forEach(([px, py], j) => j ? c.lineTo(px, py) : c.moveTo(px, py)); c.stroke(); } } }
  if (fx.vhs) { const a = g('vhs'); scan(c, W, H, 0.6 * a);
    for (let j = 0; j < 3; j++) { c.fillStyle = `rgba(255,255,255,${0.07 * a})`; c.fillRect(0, fr(T * 0.13 + j * 0.37) * H, W, H * 0.02); }
    const gr = c.createLinearGradient(0, 0, W, 0); gr.addColorStop(0, `rgba(255,0,60,${0.18 * a})`); gr.addColorStop(0.12, 'rgba(0,0,0,0)');
    gr.addColorStop(0.88, 'rgba(0,0,0,0)'); gr.addColorStop(1, `rgba(0,160,255,${0.2 * a})`); c.fillStyle = gr; c.fillRect(0, 0, W, H);
    c.save(); c.beginPath(); c.rect(0, H * 0.9, W, H * 0.04); c.clip(); grain(c, W, H, T, a * 2, 900); c.restore(); }
  if (g('scanlines')) scan(c, W, H, g('scanlines'));
  if (g('grain')) grain(c, W, H, T, g('grain'));
  if (fx.heartbeat) { const a = g('heartbeat'), p = Math.max(0, 1 - fr(T * 1.2) * 4);
    const gr = c.createRadialGradient(W / 2, H / 2, H * 0.3, W / 2, H / 2, D / 2); gr.addColorStop(0, 'rgba(200,0,20,0)'); gr.addColorStop(1, `rgba(200,0,20,${(0.25 + 0.5 * p) * a})`);
    c.fillStyle = gr; c.fillRect(0, 0, W, H); }
  if (fx.spotlight) { const a = g('spotlight'), x = W / 2 + Math.sin(T * 0.9) * W * 0.3, y = H / 2 + Math.cos(T * 0.7) * H * 0.15, R = H * 0.35;
    hole(c, W, H, x, y, R * 1.25, 0.8 * a); c.fillStyle = `rgba(0,0,0,${0.4 * a})`; c.beginPath(); c.arc(x, y, R * 1.25, 0, Math.PI * 2); c.arc(x, y, R, 0, Math.PI * 2, true); c.fill(); }
  if (fx.iris) { const f = fx.iris, R = D / 2 * smooth(Math.min(1, f.el / 0.6, f.left / 0.6)); hole(c, W, H, W / 2, H / 2, R, f.k); }
}
