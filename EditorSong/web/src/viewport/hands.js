// Agarre de guitarra / bajo. Los .ska de WoR no animan manos ni dedos de forma util para la vista previa (el juego
// los resuelve en tiempo real), asi que aqui se hace como el motor: con el esqueleto del instrumento.
//   112 cuerpo, 113 GUITAR_FRET_POS (posicion en el mastil), 115 IK mano derecha (rasgueo), 116/121 cuerdas 1 y 6
// Mano izquierda: sobre el mastil (se desplaza por los trastes con las notas), palma cruzada al mastil y dedos
// cerrados alrededor de el. Mano derecha: en el punto de rasgueo, dedos en puno sujetando la pua y golpe de
// muneca en cada nota. Sin depender de los ejes locales de cada hueso: todo se calcula con direcciones en mundo.
import * as THREE from 'three';
import { S } from '../core/state.js';
import { rotateTo, solveArmTo } from './ik.js';

const FING = {
  L: [[33, 34, 35], [39, 40, 41], [45, 46, 47], [51, 52, 53]], LT: [57, 58, 59],
  R: [[36, 37, 38], [42, 43, 44], [48, 49, 50], [54, 55, 56]], RT: [60, 61, 62],
};
const P = () => new THREE.Vector3();
const a = P(), b = P(), d = P(), n = P(), s = P(), tgt = P(), tmp = P(), pal = P();

function gemIndex(t) {
  const g = S.proj?.gems; if (!g?.length || t < g[0]) return -1;
  let lo = 0, hi = g.length - 1;
  while (hi - lo > 1) { const m = (lo + hi) >> 1; if (g[m] <= t) lo = m; else hi = m; }
  return g[hi] <= t ? hi : lo;
}
const wpos = (bone, out) => bone.getWorldPosition(out);

// cierra un dedo (cadena de 3 huesos) hacia el punto 'center': cada falange gira 'ang' radianes hacia el
function curl(c, chain, center, angs) {
  for (let j = 0; j < chain.length - 1; j++) {
    const B = c.bones[chain[j]], C = c.bones[chain[j + 1]]; if (!B || !C) return;
    wpos(B, a); wpos(C, b); d.subVectors(b, a);
    const len = d.length(); if (len < 1e-5) return;
    d.divideScalar(len);
    tmp.subVectors(center, a); tmp.addScaledVector(d, -tmp.dot(d));      // hacia el centro, perpendicular al dedo
    if (tmp.lengthSq() < 1e-10) continue;
    tmp.normalize();
    const ang = angs[j];
    rotateTo(B, d.clone(), d.clone().multiplyScalar(Math.cos(ang)).addScaledVector(tmp, Math.sin(ang)));
  }
}
// dobla las falanges y luego gira la base para que la punta caiga en 'point' (dedo pisando una cuerda)
// Se prueban varias flexiones y se queda la que deja la punta mas cerca del punto.
const KS = [0.3, 0.6, 0.9, 1.2, 1.5, 1.8, 2.1];
function press(c, chain, point, bendTo, angs) {
  const bones = chain.map(i => c.bones[i]); if (bones.some(x => !x)) return;
  const saved = bones.map(x => x.quaternion.clone());
  const tip = bones[bones.length - 1];
  const tryK = k => {
    bones.forEach((x, i) => x.quaternion.copy(saved[i])); bones[0].updateMatrixWorld(true);
    curl(c, chain.slice(1), bendTo, angs.map(v => v * k));
    wpos(bones[0], a); wpos(tip, b);
    rotateTo(bones[0], b.clone().sub(a), point.clone().sub(a));
    return wpos(tip, b).distanceTo(point);
  };
  let best = KS[0], be = Infinity;
  for (const k of KS) { const e = tryK(k); if (e < be) { be = e; best = k; } }
  tryK(best);
}
// dedos medios de la mano (direccion palma -> base del dedo corazon)
function fingerDir(c, palm, mid, out) { wpos(c.bones[palm], a); wpos(c.bones[mid], out); return out.sub(a).normalize(); }

export function handsOnGuitar(c, role, dt) {
  const G = c.bones[112], F = c.bones[113], R115 = c.bones[115], s1 = c.bones[116], s6 = c.bones[121];
  if (!G || !F || !R115 || !c.bones[10] || !c.bones[14]) return 0;
  c.root.updateMatrixWorld(true);
  // ejes del instrumento: n a lo largo del mastil (cuerpo -> clavijero), s a lo ancho (cuerda 6 -> 1)
  const g0 = wpos(G, P()); wpos(F, b); n.subVectors(b, g0); const neckLen = n.length(); n.normalize();
  if (s1 && s6) { wpos(s1, tmp); wpos(s6, s); s.subVectors(tmp, s); s.addScaledVector(n, -s.dot(n)); }
  if (s.lengthSq() < 1e-8) s.set(0, 1, 0).addScaledVector(n, -n.y);
  s.normalize();
  const face = P().crossVectors(n, s).normalize();          // normal del diapason
  // ---- mano izquierda: posicion en el mastil segun la nota (patron estable, sin saltos bruscos)
  const gi = gemIndex(S.t), bass = role === 'bassist';
  const fret = gi < 0 ? 0.5 : 0.5 + 0.35 * Math.sin(gi * 0.9) * Math.cos(gi * 0.23);
  c.fret = c.fret == null ? fret : c.fret + (fret - c.fret) * Math.min(1, dt * 14);
  // primeros trastes ~0.3 m del cuerpo, del 3 al 9 ~0.4-0.55 m (medido en los loops: 0.47); el bajo es mas largo
  const along = Math.max(neckLen + 0.04, (0.42 + 0.26 * (c.fret - 0.5)) * (bass ? 1.2 : 1));
  tgt.copy(g0).addScaledVector(n, along).addScaledVector(face, -0.025).addScaledVector(s, -0.03);            // detras del mastil
  let ok = 0;
  if (solveArmTo(c, 0, tgt, null)) ok++;
  // palma: dedos cruzando el mastil (sin componente a lo largo de el), envolviendolo
  const fdL = fingerDir(c, 10, 39, P());
  // los dedos salen de la palma (detras y debajo del mastil) hacia el borde del diapason
  const edge = P().copy(g0).addScaledVector(n, along).addScaledVector(face, 0.03);
  const want = edge.sub(wpos(c.bones[10], P())); want.addScaledVector(n, -want.dot(n));
  if (want.lengthSq() > 1e-6) rotateTo(c.bones[10], fdL.clone(), want.normalize());
  c.root.updateMatrixWorld(true);
  const neckPt = P().copy(g0).addScaledVector(n, along);
  // cada dedo pisa una cuerda: indice hacia el clavijero, los demas detras; la cuerda cambia con la nota
  FING.L.forEach((ch, i) => {
    const str = ((gi < 0 ? i : gi * 3 + i * 2) % 5) / 4 - 0.5;          // -0.5..0.5 a lo ancho del diapason
    const pt = P().copy(neckPt).addScaledVector(face, 0.014).addScaledVector(s, str * 0.04).addScaledVector(n, 0.03 - i * 0.022);
    press(c, ch, pt, neckPt, [1.2, 0.8]);
  });
  // pulgar detras del mastil
  press(c, FING.LT, P().copy(neckPt).addScaledVector(face, -0.03).addScaledVector(n, 0.01), neckPt, [0.3, 0.2]);
  // ---- mano derecha: en el punto de rasgueo, puno con la pua
  wpos(R115, tgt); tgt.addScaledVector(face, 0.05);
  // golpe de muneca en cada nota (sube y baja por las cuerdas)
  if (gi >= 0) {
    const since = (S.t - S.proj.gems[gi]) / 1000, dir = gi % 2 ? 1 : -1;
    tgt.addScaledVector(s, dir * 0.04 * Math.exp(-since * 14) * Math.sin(Math.min(1, since * 20) * Math.PI / 2));
  }
  if (solveArmTo(c, 1, tgt, null)) ok++;
  c.root.updateMatrixWorld(true);
  // palma hacia las cuerdas: los dedos apuntan al cuerpo del instrumento
  const fdR = fingerDir(c, 14, 42, P());
  const toBody = P().subVectors(wpos(R115, P()), wpos(c.bones[14], P())).addScaledVector(face, -0.15).normalize();
  rotateTo(c.bones[14], fdR.clone(), fdR.clone().lerp(toBody, 0.5).normalize());
  c.root.updateMatrixWorld(true);
  const fist = wpos(c.bones[14], P()).addScaledVector(fingerDir(c, 14, 42, P()), 0.04).addScaledVector(face, -0.06);
  for (const ch of FING.R) curl(c, ch, fist, [1.4, 1.6, 1.2]);
  curl(c, FING.RT, wpos(c.bones[37] || c.bones[14], P()), [0.4, 0.5, 0.3]);   // pulgar contra el indice (pua)
  return ok;
}
