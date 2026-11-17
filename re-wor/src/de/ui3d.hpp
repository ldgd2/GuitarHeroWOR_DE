#pragma once
// =============================================================================
// UI3D: interfaces hechas con el editor de interfaces de DE Studio (DEStudio, tipos 2D, 2.5D y 3D).
//
// Como llega al juego:
//   - El editor exporta <carpeta del .exe>/ui3d/<nombre>/scene.ui3d (todas las pantallas ya horneadas a 30
//     fotogramas por segundo: camara y las 4 esquinas en el mundo de cada plano, con su color y opacidad) y las
//     imagenes que usa (PNG: del juego, propias, SVG, textos y formas ya dibujados por el editor).
//   - Y un script QB (qb/ui_<nombre>.qb.xen): re_ui_<nombre>_show / _hide / _hide_now / _goto, que cambian
//     globales enteras: re_ui3d_<nombre>_cmd (1 mostrar, 2 salida, 3 quitar ya, 4 ir a la pantalla _arg) y
//     re_ui3d_<nombre>_seq (sube en cada orden). Si hay botones, un elemento invisible con el foco recibe el
//     mando y escribe re_ui3d_<nombre>_key (1 arriba, 2 abajo, 3 izq, 4 der, 5 elegir, 6 atras) y _kseq.
//     OnLogicFrame lo lee cada frame en el hilo del juego.
//   - La capa Ui3dOverlay lo dibuja encima de la imagen del juego (ImGui, quads con textura, perspectiva real
//     subdividiendo cada plano), con su propia camara: no toca la del juego. Tambien atiende al raton.
//
// Formato de scene.ui3d (texto, una orden por linea; version 1 = una sola pantalla sin botones):
//   ui3d 2 / name <n> / fps <f> / screens <n> / start <pantalla>
//   tex <i> <archivo.png>
//   screen <i> <nombre> <atras: auto|back|close|none>       (lo que sigue es de esa pantalla)
//   obj <i> <tex o -1> <orden>                               (i global entre pantallas)
//   objui <i> <boton o -1> <visible con el foco de boton o -1> <variable o -> <op> <valor>
//   defaultfocus <boton o -1>
//   focus <b> <obj> <arriba> <abajo> <izq> <der> <sonido 0|1> <escala> <r> <g> <b> <a> <dx> <dy>
//   act <b> none | goto <pantalla> | back | close | set <variable> <valor> | script <script y parametros>
//   clip <intro|loop|outro> <fotogramas>     y despues, por fotograma:
//     f
//     cam px py pz  rx ry rz  ux uy uz  fx fy fz  fov     (base de la camara: derecha, arriba, adelante; fov vertical)
//     o <i> r g b a  x0 y0 z0  x1 y1 z1  x2 y2 z2  x3 y3 z3   (esquinas: arriba-izq, arriba-der, abajo-der, abajo-izq)
//   objinfo <i> <tipo> <x> <y> <ancho> <alto> <nombre de la capa...>   (diseno en px del editor, para la logica)
//
// Logica (opcional): ui3d/<nombre>/logic.wuib, el programa en Python de wui (tools/wui) que el editor compila.
// Mientras la interfaz esta abierta, una maquina de wui (wui_vm.hpp) corre en el hilo del juego con el mando
// real; sus cambios a las capas (posicion, escala, giro, opacidad, color, brillo, resplandor, desenfoque,
// sombra, contorno, relleno, texto, imagen) se aplican al dibujar encima de la animacion horneada.
// =============================================================================
#include <cstdint>

#include <rex/ui/imgui_dialog.h>

namespace rex { class Runtime; }

namespace re_wor::de::ui3d {

// Para leer el mando (la logica de las interfaces). Se llama una vez al arrancar.
void SetRuntime(rex::Runtime* runtime);

// Hilo del juego, cada frame logico: ordenes de los scripts, mando, variables y recarga de escenas exportadas.
void OnLogicFrame(uint8_t* base);

// Capa siempre presente que dibuja las interfaces abiertas (no crea ventanas ni bloquea la entrada).
class Ui3dOverlay final : public rex::ui::ImGuiDialog {
 public:
  explicit Ui3dOverlay(rex::ui::ImGuiDrawer* drawer);

 protected:
  void OnDraw(ImGuiIO& io) override;
};

}  // namespace re_wor::de::ui3d
