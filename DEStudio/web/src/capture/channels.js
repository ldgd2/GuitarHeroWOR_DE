// Canales de la captura del movil.
// Cara: los 52 blendshapes de MediaPipe Face Landmarker (mismo juego y nombres que ARKit; sin "_neutral"),
// 0 = reposo, 1 = gesto completo, mas el giro de la cabeza en grados. Izquierda/derecha = las del cantante.
// Cuerpo: los 33 puntos 3D del MediaPipe Pose Landmarker en metros, con origen entre las caderas.

export const BLEND = [
  'browDownLeft', 'browDownRight', 'browInnerUp', 'browOuterUpLeft', 'browOuterUpRight',
  'cheekPuff', 'cheekSquintLeft', 'cheekSquintRight',
  'eyeBlinkLeft', 'eyeBlinkRight', 'eyeLookDownLeft', 'eyeLookDownRight', 'eyeLookInLeft', 'eyeLookInRight',
  'eyeLookOutLeft', 'eyeLookOutRight', 'eyeLookUpLeft', 'eyeLookUpRight', 'eyeSquintLeft', 'eyeSquintRight',
  'eyeWideLeft', 'eyeWideRight',
  'jawForward', 'jawLeft', 'jawOpen', 'jawRight',
  'mouthClose', 'mouthDimpleLeft', 'mouthDimpleRight', 'mouthFrownLeft', 'mouthFrownRight', 'mouthFunnel',
  'mouthLeft', 'mouthLowerDownLeft', 'mouthLowerDownRight', 'mouthPressLeft', 'mouthPressRight', 'mouthPucker',
  'mouthRight', 'mouthRollLower', 'mouthRollUpper', 'mouthShrugLower', 'mouthShrugUpper',
  'mouthSmileLeft', 'mouthSmileRight', 'mouthStretchLeft', 'mouthStretchRight',
  'mouthUpperUpLeft', 'mouthUpperUpRight', 'noseSneerLeft', 'noseSneerRight',
];
export const HEAD = ['yaw', 'pitch', 'roll'];
export const FACE_KEYS = [...BLEND, ...HEAD];
export const FACE_INDEX = Object.fromEntries(FACE_KEYS.map((k, i) => [k, i]));

// Medidores del panel: grupos legibles (media de sus canales)
export const FACE_METERS = [
  ['Boca abierta', ['jawOpen']], ['Sonrisa', ['mouthSmileLeft', 'mouthSmileRight']],
  ['Labios en O', ['mouthPucker', 'mouthFunnel']], ['Labios estirados', ['mouthStretchLeft', 'mouthStretchRight']],
  ['Parpado izq.', ['eyeBlinkLeft']], ['Parpado der.', ['eyeBlinkRight']],
  ['Cejas arriba', ['browInnerUp', 'browOuterUpLeft', 'browOuterUpRight']], ['Ceno', ['browDownLeft', 'browDownRight']],
  ['Mirada', ['eyeLookOutLeft', 'eyeLookInLeft', 'eyeLookUpLeft', 'eyeLookDownLeft']],
];

// MediaPipe Pose: indices de los 33 puntos que se usan
export const LM = {
  nose: 0, earL: 7, earR: 8, shL: 11, shR: 12, elL: 13, elR: 14, wrL: 15, wrR: 16, piL: 17, piR: 18, inL: 19, inR: 20,
  hipL: 23, hipR: 24, knL: 25, knR: 26, anL: 27, anR: 28, heL: 29, heR: 30, ftL: 31, ftR: 32,
};
export const LM_COUNT = 33;
// pares izquierda/derecha (para intercambiar lados)
export const LM_SWAP = [[1, 4], [2, 5], [3, 6], [7, 8], [9, 10], [11, 12], [13, 14], [15, 16], [17, 18], [19, 20], [21, 22],
  [23, 24], [25, 26], [27, 28], [29, 30], [31, 32]];
// lineas del esqueleto para dibujarlo
export const LM_BONES = [[11, 12], [11, 13], [13, 15], [12, 14], [14, 16], [15, 19], [16, 20], [11, 23], [12, 24], [23, 24],
  [23, 25], [25, 27], [27, 31], [24, 26], [26, 28], [28, 32], [7, 0], [8, 0]];

// Tomas grabadas con la version anterior (9 medidas propias) -> blendshapes
const LEGACY = ['jaw', 'wide', 'smile', 'blinkL', 'blinkR', 'brow', 'yaw', 'pitch', 'roll'];
export function legacyFrame(v) {
  const o = new Array(FACE_KEYS.length).fill(0), get = k => v[LEGACY.indexOf(k)] || 0, set = (k, x) => { o[FACE_INDEX[k]] = +x.toFixed(3); };
  set('jawOpen', get('jaw'));
  const w = get('wide'), s = get('smile'), b = get('brow');
  set('mouthStretchLeft', Math.max(0, w)); set('mouthStretchRight', Math.max(0, w)); set('mouthPucker', Math.max(0, -w));
  set('mouthSmileLeft', Math.max(0, s)); set('mouthSmileRight', Math.max(0, s));
  set('mouthFrownLeft', Math.max(0, -s)); set('mouthFrownRight', Math.max(0, -s));
  set('eyeBlinkLeft', get('blinkL')); set('eyeBlinkRight', get('blinkR'));
  set('browInnerUp', Math.max(0, b)); set('browOuterUpLeft', Math.max(0, b)); set('browOuterUpRight', Math.max(0, b));
  set('browDownLeft', Math.max(0, -b)); set('browDownRight', Math.max(0, -b));
  set('yaw', get('yaw')); set('pitch', get('pitch')); set('roll', get('roll'));
  return o;
}

// Manos y micro (cuerpo con dedos): detras de los 33 puntos x 4 del cuerpo van las dos manos (MediaPipe Hand
// Landmarker, 21 puntos 3D en metros, mismos ejes que el cuerpo) y el agarre del micro:
//   [132]       presencia mano izq. (0..1)   [133..195] 21 x (x, y, z)
//   [196]       presencia mano der.          [197..259] 21 x (x, y, z)
//   [260, 261]  micro en la mano izq. / der. (0 = en el pie, 1 = en la mano; suave entre medias)
// Izquierda/derecha = las del cantante (como los puntos del cuerpo).
export const HAND_OFF = { L: LM_COUNT * 4, R: LM_COUNT * 4 + 64 };
export const MIC_OFF = LM_COUNT * 4 + 128;
export const BODY_FULL = MIC_OFF + 2;
// puntos de la mano: muneca 0; pulgar 1-4, indice 5-8, corazon 9-12, anular 13-16, menique 17-20 (base -> punta)
export const HAND_FINGERS = [[5, 6, 7, 8], [9, 10, 11, 12], [13, 14, 15, 16], [17, 18, 19, 20], [1, 2, 3, 4]];
