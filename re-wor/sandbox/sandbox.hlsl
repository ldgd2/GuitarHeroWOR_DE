// SANDBOX DE RENDER de re-wor: se ejecuta en la GPU del PC justo despues de que el juego dibuja el escenario y
// antes de la autopista y el HUD. Port de los efectos de DE Studio (DEStudio/web/src/viewport/fx.js y fx2d.js): la cadena de
// filtros CSS llega ya compuesta como matriz de color (C++: render_sandbox.cpp), lo demas se calcula aqui.
//
// Se recarga EN CALIENTE: guarda este archivo con el juego abierto y se recompila (errores en re_wor_trace.log).
// Puedes meter cualquier cosa: t0 = la imagen del escenario, b0 = parametros (ver Fx abajo).
//   VSMain/PSMain : pasada a pantalla completa (escribe la imagen final del escenario)
//   VSPart/PSPart : particulas instanciadas con mezcla alfa (humo, lluvia, nieve, confeti, chispas, laseres, rayo)

cbuffer Fx : register(b0) {
  float4 g_res;       // ancho, alto (px), T (s de cancion), particulas
  float4 g_m0;        // matriz de color: fila R (r, g, b, desplazamiento)
  float4 g_m1;        // fila G
  float4 g_m2;        // fila B
  float4 g_blur;      // desenfoque (px a 1080), chroma, bloom intensidad, bloom umbral
  float4 g_vig;       // vineta: opacidad, interior %, exterior %, peligro
  float4 g_vigcol;    // vineta color rgb, franja de cine (fraccion de alto)
  float4 g_tint;      // tinte rgb, alfa
  float4 g_flash;     // destello rgb, alfa
  float4 g_misc;      // fundido a negro, arcoiris, scanlines, grano
  float4 g_misc2;     // vhs, latido, foco (spotlight), iris k
  float4 g_misc3;     // iris radio (0..1 de media diagonal), rayo k, rayo el, -
  float4 g_xf;        // 2D: rotacion (grados), escala, tx (%), ty (%)
  float4 g_pk;        // particulas k: humo, lluvia, nieve, confeti
  float4 g_pk2;       // chispas k, laseres k, -, -
  float4 g_pel;       // particulas el: humo, lluvia, nieve, confeti
  float4 g_pel2;      // chispas el, -, -, -
  float4 g_tex;       // fraccion de la textura que ocupa la imagen (x, y): el RT del Xenos es mas grande (pitch EDRAM)
};

Texture2D<float4> g_img : register(t0);
SamplerState g_lin : register(s0);
SamplerState g_pt : register(s1);

static const float PI = 3.14159265;
float fr(float x) { return x - floor(x); }
// mismo hash que fx2d.js (en float: misma distribucion, no los mismos numeros exactos)
float hash(float i, float s) { return fr(sin(fmod(i * 12.9898 + s * 78.233, 6283.18530718)) * 43758.5453); }
float smooth01(float k) { k = saturate(k); return k * k * (3 - 2 * k); }
float3 hsv(float h) {
  float3 n = float3(5, 3, 1);
  float3 x = fmod(n + h * 6, 6);
  return 1 - saturate(min(x, 4 - x));
}

struct VOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VOut VSMain(uint id : SV_VertexID) {
  VOut o;
  float2 uv = float2((id << 1) & 2, id & 2);
  o.uv = uv;
  o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
  return o;
}

// uv 0..1 = la zona dibujada (W x H); se escala a la textura real y se sujeta para no leer fuera de la imagen
float3 Sample(float2 uv) {
  float2 lim = float2(1, 1) - 0.5 / g_res.xy;
  return g_img.SampleLevel(g_lin, clamp(uv, 0.5 / g_res.xy, lim) * g_tex.xy, 0).rgb;
}

// oscurecimiento por capas negras (fundido, iris, foco): tambien se aplica a las particulas
float Dark(float2 p) {
  if (g_misc2.z <= 0 && g_misc2.w <= 0 && g_misc.x <= 0) return 0;
  float W = g_res.x, H = g_res.y, T = g_res.z, D = sqrt(W * W + H * H);
  float a = 0;
  if (g_misc2.z > 0) {   // foco que pasea
    float2 c = float2(W / 2 + sin(T * 0.9) * W * 0.3, H / 2 + cos(T * 0.7) * H * 0.15);
    float R = H * 0.35, d = length(p - c);
    a = d > R * 1.25 ? 0.8 * g_misc2.z : (d > R ? 0.4 * g_misc2.z : 0);
  }
  if (g_misc2.w > 0 && length(p - float2(W, H) / 2) > g_misc3.x * D / 2) a = 1 - (1 - a) * (1 - g_misc2.w);
  a = 1 - (1 - a) * (1 - g_misc.x);
  return a;
}

float4 PSMain(VOut i) : SV_Target {
  float W = g_res.x, H = g_res.y, T = g_res.z;
  float2 p = i.uv * float2(W, H);
  float2 uv;
  // Optimizacion: en el 99% de los fotogramas no hay transformacion 2D de camara (dutch/vertigo).
  // Omitir calculo de trigonometria (cos/sin), proyeccion y divisiones por pixel.
  [branch]
  if (abs(g_xf.x) < 0.001 && abs(g_xf.y - 1.0) < 0.001 && abs(g_xf.z) < 0.001 && abs(g_xf.w) < 0.001) {
    uv = i.uv;
  } else {
    float2 c = float2(W, H) / 2;
    float2 q = p - c - float2(g_xf.z * W, g_xf.w * H) / 100;
    float r = -g_xf.x * PI / 180;
    float s, cr;
    sincos(r, s, cr);
    q = float2(q.x * cr - q.y * s, q.x * s + q.y * cr) / max(g_xf.y, 0.01) + c;
    uv = q / float2(W, H);
  }
  float3 col;
  if (any(uv < 0) || any(uv > 1)) col = 0;
  else if (g_blur.x > 0.05) {   // desenfoque: disco de Poisson
    static const float2 K[12] = {float2(-0.326, -0.406), float2(-0.840, -0.074), float2(-0.696, 0.457), float2(-0.203, 0.621),
      float2(0.962, -0.195), float2(0.473, -0.480), float2(0.519, 0.767), float2(0.185, -0.893), float2(0.507, 0.064),
      float2(0.896, 0.412), float2(-0.322, -0.933), float2(-0.792, -0.598)};
    float rad = g_blur.x * 2 * H / 1080;
    col = Sample(uv);
    [unroll] for (int k = 0; k < 12; ++k) col += Sample(uv + K[k] * rad / float2(W, H));
    col /= 13;
  } else col = Sample(uv);
  // filtros de color (CSS del editor, ya compuestos)
  float3 lin = col;
  col = float3(dot(g_m0.xyz, lin), dot(g_m1.xyz, lin), dot(g_m2.xyz, lin)) + float3(g_m0.w, g_m1.w, g_m2.w);
  col = saturate(col);
  // bloom (resplandor)
  if (g_blur.z > 0) {
    float3 b = 0;
    [unroll] for (int k = 0; k < 16; ++k) {
      float a = k * 2.39996, rr = (0.004 + 0.03 * sqrt((k + 0.5) / 16)) * H;
      float3 s = Sample(uv + float2(cos(a), sin(a)) * rr / float2(W, H));
      b += max(0, s - g_blur.w);
    }
    col += b / 16 * g_blur.z * 1.6;
  }
  float2 e = (p - c) / (c * 1.41421356);   // elipse "farthest-corner" de radial-gradient
  float rp = length(e) * 100;
  // tinte (fondo de la capa)
  col = lerp(col, g_tint.rgb, g_tint.a);
  // franjas de cine
  if (g_vigcol.w > 0 && (p.y < g_vigcol.w * H || p.y > H - g_vigcol.w * H)) col = 0;
  // vineta
  if (g_vig.x > 0) {
    float a = g_vig.w > 0 ? 0.9 * saturate((rp - 45) / 55) : 0.95 * saturate((rp - g_vig.y) / max(1, g_vig.z - g_vig.y));
    float3 vc = g_vig.w > 0 ? float3(220, 0, 25) / 255 : g_vigcol.rgb;
    col = lerp(col, vc, a * g_vig.x);
  }
  // aberracion cromatica (capa en modo "screen")
  if (g_blur.y > 0) {
    float4 cc = rp < 55 ? 0 : (rp < 80 ? lerp(float4(0, 0, 0, 0), float4(1, 0, 60 / 255.0, 0.18), (rp - 55) / 25)
                                       : lerp(float4(1, 0, 60 / 255.0, 0.18), float4(0, 160 / 255.0, 1, 0.22), saturate((rp - 80) / 20)));
    float3 s = cc.rgb * cc.a * g_blur.y;
    col = 1 - (1 - col) * (1 - s);
  }
  // destello / estrobo / golpe rojo / fundido a blanco
  col = lerp(col, g_flash.rgb, g_flash.a);
  // ---- capa 2D (fx2d.js) a pantalla completa ----
  if (g_misc.y > 0) col = lerp(col, hsv(fr(T * 0.2)), 0.18 * g_misc.y);
  if (g_misc3.y > 0) {   // relampago: fogonazo
    float ph = fr(g_misc3.z / 1.6);
    if (ph < 0.12) col = lerp(col, float3(210, 225, 255) / 255, 0.7 * g_misc3.y * (0.12 - ph) / 0.12);
  }
  float st = max(2, H / 240);
  if (g_misc2.x > 0) {   // VHS
    float a = g_misc2.x;
    if (fr(p.y / st) < max(1, st / 3) / st) col *= 1 - 0.22 * 0.6 * a;
    [unroll] for (int j = 0; j < 3; ++j) { float y0 = fr(T * 0.13 + j * 0.37) * H; if (p.y >= y0 && p.y < y0 + H * 0.02) col = lerp(col, 1, 0.07 * a); }
    float u = p.x / W;
    if (u < 0.12) col = lerp(col, float3(1, 0, 60 / 255.0), 0.18 * a * (1 - u / 0.12));
    if (u > 0.88) col = lerp(col, float3(0, 160 / 255.0, 1), 0.2 * a * (u - 0.88) / 0.12);
    if (p.y > H * 0.9 && p.y < H * 0.94) {
      float2 cell = floor(p / max(1, H / 480));
      float f = floor(T * 24), h = hash(cell.x * 0.37 + cell.y * 7.1, f);
      if (h < 0.08) col = lerp(col, hash(cell.y, f + 0.7) > 0.5 ? 1 : 0, min(1, 0.7 * a));
    }
  }
  if (g_misc.z > 0 && fr(p.y / st) < max(1, st / 3) / st) col *= 1 - 0.22 * g_misc.z;   // scanlines
  if (g_misc.w > 0) {   // grano: 500 puntos por fotograma a 24 fps (misma densidad que el editor)
    float s = max(1, H / 480);
    float2 cell = floor(p / s);
    float f = floor(T * 24), dens = 500 * s * s / (W * H);
    if (hash(cell.x * 0.731 + cell.y * 0.137, f + 0.31) < dens)
      col = lerp(col, hash(cell.x + cell.y * 3.1, f + 0.7) > 0.5 ? 1 : 0, 0.35 * g_misc.w);
  }
  if (g_misc2.y > 0) {   // latido
    float pp = max(0, 1 - fr(T * 1.2) * 4), D = sqrt(W * W + H * H);
    float d = length(p - c), a = saturate((d - H * 0.3) / (D / 2 - H * 0.3));
    col = lerp(col, float3(200, 0, 20) / 255, a * (0.25 + 0.5 * pp) * g_misc2.y);
  }
  if (g_misc2.z > 0 || g_misc2.w > 0 || g_misc.x > 0) col *= 1 - Dark(p);
  return float4(col, 1);
}

// ------------------------------------------------------------------ particulas
// instancias: 0-13 humo, 14-213 lluvia, 214-373 nieve, 374-513 confeti, 514-603 chispas, 604-615 laseres,
// 616-639 rayo. Cada una es un quad (6 vertices); las lineas son quads finos.
struct POut { float4 pos : SV_Position; float2 uv : TEXCOORD0; nointerpolation float4 col : COLOR0; nointerpolation uint kind : KIND; float2 px : TEXCOORD1; };

static const float3 PAL[6] = {float3(255, 60, 80) / 255, float3(255, 210, 40) / 255, float3(60, 220, 120) / 255,
                              float3(60, 160, 255) / 255, float3(200, 90, 255) / 255, float3(255, 140, 30) / 255};

POut Quad(uint v, float2 center, float2 ax, float2 ay, float4 col, uint kind) {
  static const float2 C[6] = {float2(-1, -1), float2(1, -1), float2(-1, 1), float2(1, -1), float2(1, 1), float2(-1, 1)};
  float2 c = C[v];
  float2 p = center + ax * c.x + ay * c.y;
  POut o;
  o.pos = float4(p / g_res.xy * float2(2, -2) + float2(-1, 1), 0, 1);
  o.uv = c;
  o.col = col;
  o.kind = kind;
  o.px = p;
  return o;
}
POut Line(uint v, float2 a, float2 b, float w, float4 col) {
  float2 d = b - a, n = normalize(float2(-d.y, d.x) + 1e-6) * w / 2;
  return Quad(v, (a + b) / 2, d / 2, n, col, 1);
}
POut None() { POut o = (POut)0; o.pos = float4(-9, -9, 0, 1); return o; }

POut VSPart(uint v : SV_VertexID, uint inst : SV_InstanceID) {
  float W = g_res.x, H = g_res.y, T = g_res.z, D = sqrt(W * W + H * H);
  if (inst < 14) {   // humo
    float a = g_pk.x, el = g_pel.x, i = inst;
    if (a <= 0) return None();
    float life = fr(hash(i, 3) + el * 0.05);
    float x = fr(hash(i, 1) + el * 0.02 * (hash(i, 2) - 0.5)) * W, y = H - life * H * 0.6, r = H * (0.15 + 0.15 * hash(i, 4));
    return Quad(v, float2(x, y), float2(r, 0), float2(0, r), float4(200 / 255.0, 200 / 255.0, 205 / 255.0, 0.16 * a * (1 - life)), 2);
  }
  if (inst < 214) {   // lluvia
    float a = g_pk.y, el = g_pel.y, i = inst - 14;
    if (a <= 0) return None();
    float L = H * 0.05, y = fr(hash(i, 3) + el * (1.6 + 0.8 * hash(i, 2))) * (H + L) - L, x = hash(i, 1) * (W + H * 0.3) - y * 0.25;
    return Line(v, float2(x, y), float2(x - L * 0.25, y + L), max(1, H / 720), float4(180 / 255.0, 200 / 255.0, 1, 0.35 * a));
  }
  if (inst < 374) {   // nieve
    float a = g_pk.z, el = g_pel.z, i = inst - 214;
    if (a <= 0) return None();
    float y = fr(hash(i, 3) + el * (0.08 + 0.1 * hash(i, 2))) * H * 1.05, x = hash(i, 1) * W + sin(el * 1.3 + i) * W * 0.015;
    float r = H * (0.003 + 0.004 * hash(i, 4));
    return Quad(v, float2(x, y), float2(r, 0), float2(0, r), float4(1, 1, 1, 0.85 * a), 3);
  }
  if (inst < 514) {   // confeti
    float a = g_pk.w, el = g_pel.w, i = inst - 374;
    if (a <= 0) return None();
    float s = H * 0.012, y = fr(hash(i, 3) + el * (0.15 + 0.25 * hash(i, 2))) * H * 1.1 - H * 0.05;
    float x = hash(i, 1) * W + sin(el * 2 + i) * W * 0.02, an = el * 3 + i, w = s * abs(cos(an * 0.7));
    float2 ax = float2(cos(an), sin(an)), ay = float2(-ax.y, ax.x);
    return Quad(v, float2(x, y), ax * s / 2, ay * max(1, w / 2) / 2, float4(PAL[uint(i) % 6], a), 1);
  }
  if (inst < 604) {   // chispas
    float a = g_pk2.x, el = g_pel2.x, i = inst - 514;
    if (a <= 0) return None();
    float life = fr(hash(i, 3) + el * (0.3 + 0.4 * hash(i, 2))), y = H - life * H * 0.9;
    float x = hash(i, 1) * W + sin(el * 3 + i) * W * 0.01, r = max(0.5, H * 0.004 * (1.2 - life));
    return Quad(v, float2(x, y), float2(r, 0), float2(0, r), float4(1, (150 + 100 * hash(i, 5)) / 255, 40 / 255.0, a * (1 - life)), 3);
  }
  if (inst < 616) {   // laseres: 6 haces x (halo, nucleo)
    float a = g_pk2.y;
    if (a <= 0) return None();
    uint j = (inst - 604) / 2, core = (inst - 604) & 1;
    static const uint LC[6] = {0, 2, 3, 4, 0, 2};
    float ox = (j % 2) ? W : 0, sg = (j % 2) ? -1 : 1;
    float an = ((j % 2) ? PI - 1.0 : 1.0) + sin(T * 1.5 + j) * 0.5 * sg;
    float2 e = float2(ox + cos(an) * D, sin(an) * D);
    return Line(v, float2(ox, 0), e, core ? H * 0.004 : H * 0.014, float4(PAL[LC[j]], (core ? 1 : 0.25) * a));
  }
  {   // rayo: 12 tramos x (halo, nucleo)
    float a = g_misc3.y, el = g_misc3.z;
    float seed = floor(el / 1.6), ph = fr(el / 1.6);
    if (a <= 0 || ph >= 0.12) return None();
    float k = (0.12 - ph) / 0.12;
    uint seg = (inst - 616) / 2, core = (inst - 616) & 1;
    float end = H * (0.6 + 0.3 * hash(seed, 2));
    float x = (0.2 + 0.6 * hash(seed, 1)) * W, x0 = x;
    for (uint jj = 1; jj <= seg; ++jj) x += (hash(seed * 13 + jj, 3) - 0.5) * W * 0.08;
    x0 = x;
    float x1 = x + (hash(seed * 13 + seg + 1, 3) - 0.5) * W * 0.08;
    return Line(v, float2(x0, end * seg / 12), float2(x1, end * (seg + 1) / 12), core ? H * 0.003 : H * 0.012,
                float4(1, 1, 1, (core ? 1 : 0.3) * a * k));
  }
}

float4 PSPart(POut i) : SV_Target {
  float a = i.col.a;
  float d2 = dot(i.uv, i.uv);
  if (d2 >= 1.0) discard;
  if (i.kind == 2) a *= saturate(1.0 - d2);                          // humo: gradiente radial optimizado sin sqrt
  else if (i.kind == 3) a *= saturate((1.0 - sqrt(d2)) * 6.0);       // disco con borde suave
  if (a < 0.005) discard;
  return float4(i.col.rgb * (1 - Dark(i.px)), a);
}
