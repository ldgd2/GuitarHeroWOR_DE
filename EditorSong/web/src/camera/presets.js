// Presets de toma de camara. Cada preset modifica el encuadre base en funcion del avance u (0..1) del plano.
// Espacio local del musico: el musico en el origen mirando a +Z (hacia el publico), Y arriba.
//   dist (m), azim (grados, 0 = de frente, + = hacia su izquierda), height (m), fov (horizontal, grados),
//   aim (altura del punto al que mira, m), amount (intensidad 0..2), roll (grados)
export const ease = u => u * u * (3 - 2 * u);
const lerp = (a, b, k) => a + (b - a) * k;
const rad = d => d * Math.PI / 180, deg = r => r * 180 / Math.PI;

export const FRAMINGS = {
  primer: { label: 'Primer plano', dist: 1.3, fov: 34, height: 1.45 },
  medio: { label: 'Plano medio', dist: 2.6, fov: 42, height: 1.35 },
  americano: { label: 'Plano americano', dist: 3.6, fov: 48, height: 1.3 },
  general: { label: 'Plano general', dist: 7, fov: 58, height: 2.2 },
};

// r = resultado modificable {dist, azim, height, fov, lat, pan, tilt, roll, hand, shake, aim}
export const PRESETS = {
  keys: { label: 'Animacion personalizada', icon: 'keyframe', desc: 'Movimiento hecho a mano con keyframes (o grabado con el movil). Editalo con "Editar animacion".', run: () => {} },
  fija: { label: 'Fija', icon: 'frame', desc: 'Plano quieto encuadrando al musico.', run: () => {} },
  mano: { label: 'Camara en mano', icon: 'hand', desc: 'Operador con la camara al hombro: balanceo organico y respiracion.',
    run: (r, u, P) => { r.hand = P.amount; } },
  temblor: { label: 'Temblor', icon: 'shake', desc: 'Sacudida fuerte que se va apagando (golpe, explosion, entrada de bateria).',
    run: (r, u, P) => { r.shake = P.amount * (1 - u) ** 2; r.hand = 0.4; } },
  acercar: { label: 'Acercamiento', icon: 'dollyin', desc: 'Travelling hacia el musico (dolly in).',
    run: (r, u, P) => { r.dist = P.dist * (1 - 0.55 * P.amount * ease(u)); r.hand = 0.3; } },
  alejar: { label: 'Alejamiento', icon: 'dollyout', desc: 'La camara se aleja y revela el escenario (dolly out).',
    run: (r, u, P) => { r.dist = P.dist * (1 + 1.6 * P.amount * ease(u)); r.height = P.height + 1.2 * P.amount * ease(u); r.hand = 0.3; } },
  zoomin: { label: 'Zoom de lente', icon: 'zoompunch', desc: 'Cierra el objetivo sin mover la camara.',
    run: (r, u, P) => { r.fov = P.fov * (1 - 0.6 * Math.min(1, P.amount) * ease(u)); } },
  zoomout: { label: 'Zoom out de lente', icon: 'zoomout', desc: 'Abre el objetivo de golpe o poco a poco.',
    run: (r, u, P) => { r.fov = P.fov * (0.4 + 0.6 * ease(u)); } },
  vertigo: { label: 'Vertigo (dolly zoom)', icon: 'vertigo', desc: 'Avanza mientras abre el objetivo: el musico queda igual y el fondo se estira (efecto Hitchcock).',
    run: (r, u, P) => {
      const d = P.dist * (1 - 0.65 * Math.min(1.3, P.amount) * ease(u));
      r.dist = d; r.fov = deg(2 * Math.atan(Math.tan(rad(P.fov) / 2) * P.dist / d));
    } },
  orbita: { label: 'Orbita', icon: 'orbit', desc: 'Gira alrededor del musico.',
    run: (r, u, P) => { r.azim = P.azim + 100 * P.amount * ease(u); r.hand = 0.25; } },
  grua: { label: 'Grua', icon: 'crane', desc: 'Sube desde el suelo hasta un plano alto mirando al musico.',
    run: (r, u, P) => { r.height = lerp(0.35, P.height + 2.8 * P.amount, ease(u)); r.dist = P.dist * (1 + 0.4 * ease(u)); } },
  travelling: { label: 'Travelling lateral', icon: 'truck', desc: 'Se desplaza de un lado a otro frente al musico.',
    run: (r, u, P) => { r.lat = lerp(-1.6, 1.6, u) * P.amount; r.hand = 0.2; } },
  heroico: { label: 'Contrapicado heroico', icon: 'lowangle', desc: 'Desde muy abajo con gran angular y empuje lento: el musico parece un gigante.',
    run: (r, u, P) => { r.height = 0.3; r.fov = P.fov + 18; r.dist = P.dist * (0.75 - 0.2 * ease(u)); r.hand = 0.3; } },
  cenital: { label: 'Cenital', icon: 'overhead', desc: 'Desde arriba, girando despacio.',
    run: (r, u, P) => { r.height = 5.5 + P.amount; r.dist = 0.4; r.azim = P.azim + 45 * u; r.aim = 0.9; } },
  barrido: { label: 'Barrido (whip pan)', icon: 'whip', desc: 'Mira a otro lado y al final gira rapidisimo hacia el musico.',
    run: (r, u, P) => { const k = u < 0.7 ? 0 : ease((u - 0.7) / 0.3); r.pan = 75 * P.amount * (1 - k); r.hand = 0.4; } },
  latido: { label: 'Latido al ritmo', icon: 'pulse', desc: 'Golpes de zoom en cada pulso de la cancion.',
    run: (r, u, P, beat) => { r.fov = P.fov * (1 - 0.1 * P.amount * Math.exp(-(beat % 1) * 7)); r.hand = 0.3; } },
  inclinada: { label: 'Plano holandes', icon: 'dutch', desc: 'Camara inclinada que se endereza (tension, locura).',
    run: (r, u, P) => { r.roll = (P.roll || 18) * (1 - ease(u)) * P.amount + 4; r.hand = 0.3; } },
  espiral: { label: 'Espiral', icon: 'spiral', desc: 'Orbita mientras se acerca y gira la imagen: entrada de solo.',
    run: (r, u, P) => { const e = ease(u); r.azim = P.azim + 180 * P.amount * e; r.dist = P.dist * (1.6 - 0.9 * e); r.roll = 25 * P.amount * (1 - e); r.hand = 0.2; } },
  picado: { label: 'Picado lento', icon: 'highangle', desc: 'Desde arriba y delante, baja hasta la altura de los ojos.',
    run: (r, u, P) => { const e = ease(u); r.height = lerp(P.height + 3 * P.amount, P.height, e); r.dist = P.dist * 1.3; } },
  rasante: { label: 'Rasante al suelo', icon: 'ground', desc: 'Pegada al escenario, avanza hacia los pies del musico mirando arriba.',
    run: (r, u, P) => { r.height = 0.15; r.aim = P.aim + 0.2; r.fov = P.fov + 22; r.dist = P.dist * (1.2 - 0.5 * ease(u) * P.amount); r.hand = 0.25; } },
  estrobo: { label: 'Cortes al ritmo', icon: 'zap', desc: 'Salta de angulo en cada pulso (montaje rapido sin cortar el clip).',
    run: (r, u, P, beat) => { const n = Math.floor(beat) % 4; r.azim = P.azim + [-35, 25, -10, 45][n] * P.amount; r.dist = P.dist * [1, 0.7, 1.3, 0.85][n]; r.hand = 0.3; } },
  rebote: { label: 'Rebote al ritmo', icon: 'bounce', desc: 'La camara bota con cada pulso (headbang).',
    run: (r, u, P, beat) => { r.height = P.height - 0.12 * P.amount * Math.exp(-(beat % 1) * 6); r.tilt = 3 * P.amount * Math.exp(-(beat % 1) * 6); r.hand = 0.3; } },
  pasada: { label: 'Pasada (fly-by)', icon: 'flyby', desc: 'Pasa rapidisimo por delante del musico de un lado a otro.',
    run: (r, u, P) => { const e = ease(u); r.lat = lerp(-3, 3, e) * P.amount; r.dist = P.dist * 0.6; r.pan = lerp(-25, 25, e); } },
  revelado: { label: 'Revelado', icon: 'reveal', desc: 'Empieza muy cerca y desenfocada de angulo y abre al plano general.',
    run: (r, u, P) => { const e = ease(u); r.dist = lerp(0.6, P.dist * 2.4, e); r.height = lerp(P.aim, P.height + 1.5, e); r.fov = lerp(P.fov * 0.7, P.fov + 10, e); } },
  temblor_suave: { label: 'Vibracion de concierto', icon: 'wobble', desc: 'Vibracion continua y suave, como desde el foso con altavoces al lado.',
    run: (r, u, P) => { r.shake = 0.25 * P.amount; r.hand = 0.5; } },
  circulo_bajo: { label: 'Orbita rasante', icon: 'orbitlow', desc: 'Da la vuelta completa al musico desde muy abajo.',
    run: (r, u, P) => { r.azim = P.azim + 360 * ease(u) * Math.min(1, P.amount); r.height = 0.4; r.aim = P.aim + 0.1; r.fov = P.fov + 10; r.hand = 0.2; } },
  acecho: { label: 'Por detras del hombro', icon: 'shoulder', desc: 'Desde detras del musico hacia el publico, rodeandolo despacio.',
    run: (r, u, P) => { r.azim = 160 - 40 * ease(u) * P.amount; r.dist = 1.4; r.height = P.aim + 0.25; r.hand = 0.35; } },
  caida: { label: 'Caida libre', icon: 'drop', desc: 'Cae desde muy alto y frena de golpe a la altura del musico.',
    run: (r, u, P) => { const k = 1 - (1 - Math.min(1, u * 1.6)) ** 3; r.height = lerp(P.height + 6 * P.amount, P.height, k); r.dist = P.dist * 1.2; r.shake = u > 0.6 && u < 0.75 ? 0.6 : 0; } },
  respiracion: { label: 'Respiracion', icon: 'breathe', desc: 'Zoom que entra y sale lentamente, como si respirase.',
    run: (r, u, P) => { r.fov = P.fov * (1 - 0.12 * P.amount * (0.5 - 0.5 * Math.cos(u * Math.PI * 4))); r.hand = 0.3; } },
  vuelta360: { label: 'Vuelta de 360', icon: 'orbitfull', desc: 'Orbita completa a la altura del musico.',
    run: (r, u, P) => { r.azim = P.azim + 360 * ease(u); r.hand = 0.15; } },
  pendulo: { label: 'Pendulo', icon: 'pendulum', desc: 'Va y viene alrededor del musico, sin parar.',
    run: (r, u, P) => { r.azim = P.azim + 40 * P.amount * Math.sin(u * Math.PI * 2); r.hand = 0.2; } },
};

// Efectos de imagen. Cada uno: categoria, descripcion y sus AJUSTES (params). Todos los ajustes se exportan al .edfx
// ("clave=valor") y el juego los lee (re-wor/src/de/native_fx.cpp y edfx.cpp). Ademas, todos tienen fundido de
// entrada y salida (COMMON_PARAMS).
//   S(etiqueta, min, max, paso, defecto)  deslizador
//   O(etiqueta, [[valor, texto]...], defecto)  lista
//   C(etiqueta, '#rrggbb')  color (se exporta como <clave>r/g/b de 0 a 1)
const S = (label, min, max, step, def) => ({ label, min, max, step, def });
const O = (label, options, def) => ({ label, options, def });
const C = (label, def) => ({ label, color: true, def });
const COL = { col: C('Color', '#ffffff') };

export const FX_CATS = {
  ajustes: 'Ajustes de imagen', look: 'Looks de color', luz: 'Luz y resplandor', lente: 'Lente y enfoque',
  camara: 'Movimiento de camara', transicion: 'Transiciones', textura: 'Textura y pantalla', particulas: 'Particulas y clima',
  edicion: 'Edicion de video', experimental: 'Experimentales (no verificados)',
};

export const COMMON_PARAMS = { fin: S('Fundido de entrada (s)', 0, 5, 0.05, 0), fout: S('Fundido de salida (s)', 0, 5, 0.05, 0) };

export const EFFECTS = {
  // ---- ajustes de imagen (bright_Sat del motor) ----
  grade: { cat: 'ajustes', label: 'Ajustes de imagen', desc: 'Exposicion, contraste, saturacion, temperatura, tinte y tono, como en un editor de video',
    params: { exposure: S('Exposicion (pasos)', -2, 2, 0.05, 0), contrast: S('Contraste', 0.3, 2.5, 0.05, 1),
      saturation: S('Saturacion', 0, 2.5, 0.05, 1), temperature: S('Temperatura (frio/calido)', -1, 1, 0.05, 0),
      tint: S('Tinte (magenta/verde)', -1, 1, 0.05, 0), hue: S('Tono (grados)', -180, 180, 1, 0), vignette: S('Vineta', 0, 1, 0.05, 0) } },
  exposure: { cat: 'ajustes', label: 'Brillo / exposicion', desc: 'Sube o baja la luz de la imagen', params: { stops: S('Pasos de exposicion', -3, 3, 0.05, 0.6) } },
  contrast: { cat: 'ajustes', label: 'Alto contraste', desc: 'Sombras mas negras y luces mas blancas',
    params: { amount: S('Contraste', 0, 2, 0.05, 0.6), vig: S('Vineta', 0, 1, 0.05, 0.6) } },
  saturate: { cat: 'ajustes', label: 'Saturacion', desc: 'Colores mas vivos (o apagados con valores negativos)', params: { amount: S('Saturacion', -1, 2, 0.05, 0.8) } },
  bw: { cat: 'ajustes', label: 'Blanco y negro', desc: '', params: { contrast: S('Contraste', 0, 1, 0.05, 0.15) } },
  sepia: { cat: 'ajustes', label: 'Sepia', desc: 'Foto antigua' },
  warm: { cat: 'ajustes', label: 'Tono calido', desc: 'Mas rojo/naranja, menos azul', params: { amount: S('Cantidad', 0, 2, 0.05, 1) } },
  cold: { cat: 'ajustes', label: 'Tono frio', desc: 'Mas azul', params: { amount: S('Cantidad', 0, 2, 0.05, 1) } },
  tint: { cat: 'ajustes', label: 'Tinte de color', desc: 'Tine toda la imagen de un color (monocromo de color con mezcla al 100%)',
    params: { col: C('Color', '#ff8a30'), mix: S('Mezcla', 0, 1, 0.05, 0.5) } },
  hueshift: { cat: 'ajustes', label: 'Cambio de tono', desc: 'Gira todos los colores', params: { deg: S('Grados', -180, 180, 1, 120) } },

  // ---- looks (matriz de color + extras nativos) ----
  teal: { cat: 'look', label: 'Teal & orange', desc: 'Pieles calidas y sombras azul verdoso (cine de accion)' },
  blockbuster: { cat: 'look', label: 'Blockbuster frio', desc: 'Azul acerado y contraste duro' },
  bleach: { cat: 'look', label: 'Bleach bypass', desc: 'Colores lavados y contraste duro (cine belico)' },
  noir: { cat: 'look', label: 'Cine negro', desc: 'Blanco y negro duro con vineta y grano' },
  dramatic: { cat: 'look', label: 'Dramatico', desc: 'Contraste fuerte, color contenido y vineta' },
  film: { cat: 'look', label: 'Pelicula', desc: 'Emulsion de cine: calido, contraste y grano' },
  kodak: { cat: 'look', label: 'Kodak', desc: 'Pelicula de color calida y viva' },
  polaroid: { cat: 'look', label: 'Polaroid', desc: 'Instantanea: colores lavados y vineta' },
  retro70: { cat: 'look', label: 'Retro 70s', desc: 'Calido y desvaido, con grano' },
  technicolor: { cat: 'look', label: 'Technicolor', desc: 'Color de pelicula clasica muy saturado' },
  lomo: { cat: 'look', label: 'Lomo', desc: 'Colores saturados, contraste y vineta fuerte' },
  vaporwave: { cat: 'look', label: 'Vaporwave', desc: 'Rosa y cian, brillo suave, grano y lineas de TV' },
  miami: { cat: 'look', label: 'Miami', desc: 'Rosa y turquesa de neon, con resplandor' },
  cyberpunk: { cat: 'look', label: 'Cyberpunk', desc: 'Magenta y azul neon con resplandor' },
  sunset: { cat: 'look', label: 'Atardecer', desc: 'Dorado calido' },
  golden: { cat: 'look', label: 'Hora dorada', desc: 'Luz de oro con resplandor suave' },
  desert: { cat: 'look', label: 'Desierto', desc: 'Naranja quemado (cine del oeste, Mad Max)' },
  ice: { cat: 'look', label: 'Hielo', desc: 'Frio, claro y poco saturado' },
  toxic: { cat: 'look', label: 'Toxico', desc: 'Verde amarillento enfermizo' },
  blood: { cat: 'look', label: 'Sangre', desc: 'Rojo de terror con vineta' },
  matrix: { cat: 'look', label: 'Codigo verde', desc: 'Tinte verde frio de pelicula de hackers' },
  night: { cat: 'look', label: 'Vision nocturna', desc: 'Verde de camara nocturna con grano y vineta' },
  infrared: { cat: 'look', label: 'Infrarrojo', desc: 'Colores imposibles de camara infrarroja' },
  pastel: { cat: 'look', label: 'Pastel', desc: 'Suave, claro y poco saturado' },
  dream: { cat: 'look', label: 'Sueno', desc: 'Velo luminoso, resplandor y bordes suaves' },
  rainbow: { cat: 'look', label: 'Arcoiris', desc: 'Tinte que recorre todos los colores', params: { speed: S('Velocidad', 0.1, 4, 0.1, 1) } },

  // ---- luz ----
  glow: { cat: 'luz', label: 'Glow', desc: 'Resplandor real del motor: las luces brillan y se expanden',
    params: { threshold: S('Umbral (que brilla)', 0, 1, 0.01, 0.3), intensity: S('Intensidad', 0, 3, 0.05, 1.2),
      radius: S('Tamano', 0.25, 4, 0.05, 3), ...COL, bright: S('Brillo extra', 0, 1, 0.05, 0) } },
  bloom: { cat: 'luz', label: 'Bloom suave', desc: 'Resplandor discreto solo en las luces fuertes',
    params: { threshold: S('Umbral', 0, 1, 0.01, 0.45), intensity: S('Intensidad', 0, 3, 0.05, 0.8), radius: S('Tamano', 0.25, 4, 0.05, 3), ...COL } },
  godrays: { cat: 'luz', label: 'Rayos de luz', desc: 'Haces volumetricos del motor desde arriba',
    params: { height: S('Altura de la luz', -10, 40, 0.5, 12), side: S('Lado', -30, 30, 0.5, 0), col: C('Color', '#fff5de') } },
  flash: { cat: 'luz', label: 'Destello', desc: 'Golpe de luz blanca que se apaga',
    params: { power: S('Fuerza', 0.5, 5, 0.1, 2.5), len: S('Duracion del golpe (s)', 0.05, 2, 0.05, 0.35) } },
  strobe: { cat: 'luz', label: 'Estroboscopio', desc: 'Destellos rapidos', params: { rate: S('Destellos por segundo', 1, 20, 0.5, 8) } },
  lightning: { cat: 'luz', label: 'Rayos', desc: 'Relampagos', params: { every: S('Cada (s)', 0.3, 5, 0.1, 1.6) } },
  flicker: { cat: 'luz', label: 'Parpadeo', desc: 'La luz tiembla como un fluorescente roto', params: { speed: S('Velocidad', 0.2, 3, 0.1, 1) } },
  redflash: { cat: 'luz', label: 'Golpe rojo', desc: 'Destello rojo que se apaga (impacto)' },
  heartbeat: { cat: 'luz', label: 'Latido', desc: 'Vineta roja que late' },
  danger: { cat: 'luz', label: 'Peligro', desc: 'Vineta roja que late' },

  // ---- lente ----
  vignette: { cat: 'lente', label: 'Vineta', desc: 'Oscurece (o tine) las esquinas',
    params: { size: S('Tamano', 0, 1, 0.05, 0.5), soft: S('Suavidad', 0, 1, 0.05, 0.5), col: C('Color', '#000000') } },
  focus: { cat: 'lente', label: 'Enfoque', desc: 'Profundidad de campo: nitido a una distancia, el resto desenfocado',
    params: { dist: S('Distancia de enfoque (m)', 0.5, 40, 0.5, 6), range: S('Zona nitida (m)', 0.2, 20, 0.2, 3), strength: S('Desenfoque', 0, 1, 0.05, 0.8) } },
  rackfocus: { cat: 'lente', label: 'Cambio de foco', desc: 'El foco viaja de una distancia a otra',
    params: { from: S('Desde (m)', 0.5, 40, 0.5, 1), to: S('Hasta (m)', 0.5, 40, 0.5, 12) } },
  blur: { cat: 'lente', label: 'Desenfoque', desc: 'Todo desenfocado' },
  tiltshift: { cat: 'lente', label: 'Miniatura (tilt-shift)', desc: 'Solo el centro enfocado: el escenario parece una maqueta' },
  motionblur: { cat: 'lente', label: 'Desenfoque de movimiento', desc: 'Estela en lo que se mueve (preset del juego)' },
  letterbox: { cat: 'lente', label: 'Franjas de cine', desc: 'El motor recorta la imagen del escenario (el traste no se toca)',
    params: { ratio: O('Formato', [[2.39, 'Cinemascope 2.39:1'], [2.0, 'Univisium 2:1'], [1.85, 'Panoramico 1.85:1'], [2.76, 'Ultra Panavision 2.76:1'], [3.5, 'Franja extrema']], 2.39) } },

  // ---- camara ----
  shake: { cat: 'camara', label: 'Temblor de camara', desc: 'Sacude la camara del juego', params: { speed: S('Velocidad', 0.2, 3, 0.1, 1) } },
  handheld: { cat: 'camara', label: 'Camara en mano', desc: 'Balanceo de operador', params: { speed: S('Velocidad', 0.2, 3, 0.1, 1) } },
  dutch: { cat: 'camara', label: 'Plano holandes', desc: 'Camara inclinada', params: { angle: S('Angulo', -45, 45, 1, 15) } },
  zoompunch: { cat: 'camara', label: 'Golpe de zoom', desc: 'Zoom brusco que vuelve',
    params: { zoom: S('Cuanto zoom', 0.05, 0.8, 0.01, 0.3), len: S('Vuelta (s)', 0.05, 3, 0.05, 0.6) } },
  vertigo: { cat: 'camara', label: 'Efecto vertigo', desc: 'Dolly zoom de Hitchcock', params: { amount: S('Cantidad', 0.1, 1.2, 0.05, 0.45) } },

  // ---- transiciones ----
  fadein: { cat: 'transicion', label: 'Desde negro', desc: 'Empieza en negro' },
  fadeout: { cat: 'transicion', label: 'A negro', desc: 'Termina en negro' },
  fadewhite: { cat: 'transicion', label: 'A blanco', desc: 'Se quema a blanco hasta el final del efecto' },
  blackout: { cat: 'transicion', label: 'Apagon', desc: 'Pantalla en negro durante el efecto' },
  iris: { cat: 'transicion', label: 'Iris', desc: 'Se abre en circulo y se cierra al acabar (cine mudo)' },
  spotlight: { cat: 'transicion', label: 'Foco de busqueda', desc: 'Todo a oscuras salvo un circulo de luz' },

  // ---- textura (dibujadas encima, sin tapar el traste) ----
  grain: { cat: 'textura', label: 'Grano de pelicula', desc: 'Ruido de pelicula', params: { size: S('Tamano del grano (0 = auto)', 0, 4, 0.1, 0) } },
  scanlines: { cat: 'textura', label: 'Lineas de TV', desc: 'Lineas horizontales de monitor antiguo' },
  vhs: { cat: 'textura', label: 'Cinta VHS', desc: 'Lineas, bandas de tracking, ruido y color corrido' },
  chroma: { cat: 'textura', label: 'Aberracion de color', desc: 'Separacion RGB en los bordes' },

  // ---- particulas (dibujadas encima) ----
  confetti: { cat: 'particulas', label: 'Confeti', desc: 'Lluvia de confeti de colores' },
  sparks: { cat: 'particulas', label: 'Chispas', desc: 'Brasas que suben' },
  snow: { cat: 'particulas', label: 'Nieve', desc: 'Copos cayendo' },
  rain: { cat: 'particulas', label: 'Lluvia', desc: 'Lluvia inclinada' },
  smoke: { cat: 'particulas', label: 'Humo', desc: 'Nubes de humo desde abajo' },
  lasers: { cat: 'particulas', label: 'Laseres', desc: 'Haces de laser desde las esquinas' },

  // ---- edicion de video ----
  split: { cat: 'edicion', label: 'Pantalla dividida', desc: 'Franjas con una camara cada una, que aparecen al ritmo (viewports reales del motor)',
    params: { n: S('Franjas', 2, 4, 1, 4), dir: O('Direccion', [[0, 'Horizontales'], [1, 'Verticales'], [2, 'Cuadricula 2x2']], 0),
      beats: S('Pulsos entre franjas', 1, 8, 1, 2), mode: O('Entrada de cada franja', [[0, 'Corte seco'], [1, 'Barrido']], 1) } },

  // ---- experimentales: parametros del motor sin verificar ----
  fisheye: { cat: 'experimental', hidden: true, label: 'Ojo de pez', desc: 'Lente deformada (sin verificar)' },
  wobble: { cat: 'experimental', hidden: true, label: 'Mareo', desc: 'Ondulacion (sin verificar)' },
  shockwave: { cat: 'experimental', hidden: true, label: 'Onda expansiva', desc: 'Distorsion (sin verificar)' },
  sharpen: { cat: 'experimental', hidden: true, label: 'Nitidez', desc: '(sin verificar)' },
  emboss: { cat: 'experimental', hidden: true, label: 'Relieve', desc: '(sin verificar)' },
  edges: { cat: 'experimental', hidden: true, label: 'Contornos', desc: '(sin verificar)' },
  comic: { cat: 'experimental', hidden: true, label: 'Comic', desc: '(sin verificar)' },
};
// Efectos que el juego hace con su propio motor (no se dibujan encima)
export const NATIVE_FX = new Set(Object.entries(EFFECTS).filter(([, v]) => !['textura', 'particulas'].includes(v.cat) || v === EFFECTS.grain).map(([k]) => k));

// valores por defecto de los ajustes de un efecto (con los comunes)
export function fxDefaults(type) {
  const out = {};
  for (const [k, p] of Object.entries({ ...(EFFECTS[type]?.params || {}), ...COMMON_PARAMS })) out[k] = p.def;
  return out;
}

// Presets: varios efectos ya ajustados que se anaden juntos como un GRUPO (se editan juntos o por separado).
// Cada entrada: 'tipo' o ['tipo', intensidad, {ajustes}]
export const FX_COMBOS = {
  cine: { label: 'Cine', dur: 12000, fx: [['letterbox', 1, { fin: 0.5, fout: 0.5 }], ['teal', 0.7], ['grain', 0.35], ['vignette', 0.5]] },
  blockbuster: { label: 'Blockbuster', dur: 10000, fx: [['blockbuster', 0.8], ['glow', 0.5, { threshold: 0.5 }], ['letterbox', 1, { fin: 0.4 }], ['contrast', 0.4, { vig: 0.3 }]] },
  videoclip: { label: 'Videoclip', dur: 8000, fx: [['saturate', 0.6], ['glow', 0.7], ['vignette', 0.4], ['handheld', 0.6]] },
  retro80: { label: 'Retro 80s', dur: 10000, fx: [['vaporwave', 0.9], ['glow', 0.8, { col: '#ff7ad9' }], ['vhs', 0.5], ['grain', 0.4]] },
  miami: { label: 'Miami Vice', dur: 10000, fx: [['miami', 1], ['glow', 0.9, { col: '#ff6ec7', threshold: 0.35 }], ['letterbox', 1, { ratio: 2.0 }]] },
  neon: { label: 'Neon', dur: 8000, fx: [['cyberpunk', 1], ['glow', 1, { threshold: 0.25, col: '#7a6bff' }], ['contrast', 0.4, { vig: 0.5 }]] },
  videoclip90: { label: 'Videoclip 90s', dur: 8000, fx: [['vhs', 0.8], ['saturate', 0.5], ['letterbox', 1, { ratio: 1.85 }]] },
  noir: { label: 'Cine negro', dur: 10000, fx: [['noir', 1], ['vignette', 0.8, { size: 0.7 }], ['grain', 0.6]] },
  cine_mudo: { label: 'Cine mudo', dur: 10000, fx: [['sepia', 0.8], ['grain', 0.8], ['iris', 1], ['flicker', 0.35]] },
  polaroid: { label: 'Recuerdo', dur: 8000, fx: [['polaroid', 1], ['dream', 0.5], ['grain', 0.3]] },
  verano: { label: 'Verano', dur: 10000, fx: [['golden', 1], ['glow', 0.7, { col: '#ffd18a' }], ['godrays', 0.6]] },
  epico: { label: 'Epico', dur: 12000, fx: [['godrays', 0.8], ['glow', 0.6], ['letterbox', 1, { fin: 0.6 }], ['teal', 0.6]] },
  accion: { label: 'Accion', dur: 8000, fx: [['teal', 0.8], ['handheld', 0.8], ['motionblur', 0.6], ['contrast', 0.5, { vig: 0.3 }]] },
  terror: { label: 'Terror', dur: 10000, fx: [['blood', 0.7], ['flicker', 0.6], ['danger', 0.6], ['handheld', 0.7], ['grain', 0.5]] },
  tormenta: { label: 'Tormenta', dur: 10000, fx: [['rain', 1], ['lightning', 1], ['cold', 1], ['vignette', 0.6]] },
  invierno: { label: 'Invierno', dur: 10000, fx: [['snow', 1], ['ice', 0.9], ['glow', 0.5, { col: '#cfe6ff' }]] },
  infierno: { label: 'Infierno', dur: 10000, fx: [['sparks', 1], ['smoke', 0.8], ['desert', 0.9], ['heartbeat', 0.6]] },
  fiesta: { label: 'Fiesta', dur: 8000, fx: [['confetti', 1], ['lasers', 1], ['glow', 0.9], ['saturate', 0.5]] },
  rave: { label: 'Rave', dur: 8000, fx: [['lasers', 1], ['strobe', 0.7, { rate: 6 }], ['rainbow', 0.8], ['glow', 1]] },
  pesadilla: { label: 'Pesadilla', dur: 10000, fx: [['night', 1], ['grain', 0.6], ['heartbeat', 0.8], ['chroma', 0.6]] },
  impacto: { label: 'Impacto', dur: 1200, fx: [['zoompunch', 1, { zoom: 0.35 }], ['flash', 1, { len: 0.3 }], ['shake', 1]] },
  drop: { label: 'Drop', dur: 2000, fx: [['flash', 1, { power: 3, len: 0.5 }], ['zoompunch', 1], ['glow', 1, { fout: 1.5 }], ['saturate', 0.8, { fout: 1.5 }]] },
  hitchcock: { label: 'Hitchcock', dur: 6000, fx: [['vertigo', 1], ['bw', 1], ['vignette', 0.6]] },
  maqueta: { label: 'Maqueta', dur: 8000, fx: [['tiltshift', 1], ['saturate', 0.6], ['exposure', 0.5, { stops: 0.3 }]] },
  hackers: { label: 'Hackers', dur: 8000, fx: [['matrix', 1], ['scanlines', 0.7], ['grain', 0.4]] },
};

// Altura (m) del punto al que miran las tomas, por musico
export const ROLE_AIM = { guitarist: 1.35, bassist: 1.35, vocalist: 1.5, drummer: 1.05 };
