#pragma once
// =============================================================================
// EDFX: lo que el editor de canciones (EditorSong) pone en una cancion y WoR no sabe hacer por si mismo:
// efectos de imagen (franjas de cine, fundidos, destellos, vineta, filtros...), objetos 3D y textos.
//
// Como llega al juego:
//   - El editor escribe "<cancion>.edfx" junto al PAK de la cancion, en la carpeta del mod.
//   - En la pista de eventos de la cancion mete eventos de sincronia cuyo script es un nombre unico
//     (edfx_<cancion>_<n>, con su QBKey en el .edfx). El juego los dispara en el momento exacto de la
//     cancion (respeta pausas, reinicios y la practica) y busca ese simbolo.
//   - RELOJ: el hook lee cada frame el reloj de la cancion del motor (songtime, ver OnLogicFrame) y el
//     hook de FileOpen sabe que cancion se cargo. Con eso basta; los eventos de sincronia son respaldo.
//   - El hook de busqueda de simbolos (sub_8239E2A0) reconoce esas claves: anota "el evento n ha sonado"
//     (= la cancion va por el ms t_n) y devuelve el script vacio del juego (nullscript) para que no pase nada.
//   - La capa EdfxOverlay dibuja encima de la imagen del juego, con el reloj anclado al ultimo evento.
// =============================================================================
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <rex/ui/imgui_dialog.h>

namespace re_wor::de::edfx {

// Lee todos los .edfx de la carpeta de mods (se vuelve a llamar sola si cambian).
void Rescan(const std::filesystem::path& mods_root);

// Desde el hook de simbolos: true si la clave es un evento de sincronia de EDFX (y lo anota).
// (Respaldo: el reloj principal es el de la cancion del propio juego, ver OnLogicFrame.)
bool OnSymbolLookup(uint32_t checksum);

// Desde FileStream::Open: si es "songs/<nombre>_song.pak.xen" anota que esa cancion se esta cargando.
void OnFileOpen(const char* path);

// Desde el frame logico del juego (hilo del juego): lee el reloj de la cancion del motor.
// WoR lo guarda como double (segundos) en 0x82993A58; lo avanza el CFunc IncrementSongTimer,
// lo leen GetSongTime / TimeMarkerReached y lo pone a cero ResetSongTimer.
// Tambien lee la camara del viewport principal (para poner los objetos 3D sobre cualquier plano del juego)
// y, durante las tomas del editor, ESCRIBE esa camara en el motor (la toma se ve aunque el clip nativo no).
void OnLogicFrame(uint8_t* base);

// Efectos activos ahora (segun el reloj del motor) para aplicarlos con el ScreenFX nativo (native_fx.cpp)
struct FxState {
  std::string type;
  float k, el, left;
  std::vector<std::pair<std::string, float>> params;   // ajustes del efecto (exposicion, franjas...)
  float P(const char* name, float def) const {
    for (const auto& [n, v] : params) if (n == name) return v;
    return def;
  }
};
// Direccion de la camara del viewport principal (0 si no hay)
bool MainCamera(uint32_t& addr);
// Camara propia de la franja i de la pantalla dividida activa (formato de la camara del motor); false = no tiene
bool StripCamera(int i, float p[3], float side[3], float U[3], float F[3], float& hfov_raw);
std::vector<FxState> ActiveFxNow();
double SongMsNow();

// Objetos NATIVOS activos ahora (los crea el motor: luces, particulas, modelos del juego). false = sin reloj.
struct NativeItem { int id; std::string kind; float p[3], q[4], rgb[3], v[3]; std::string asset; };
bool NativeNow(std::string& song, double& ms, std::vector<NativeItem>& out);
// true si ese escenario (zones/<z>.pak.xen) se abrio hace poco (sus modelos estan en memoria)
bool ZoneLoaded(const std::string& zone);
// native_fx avisa de que las franjas de cine se hacen recortando el viewport del escenario
void SetNativeLetterbox(bool on);

// Capa siempre presente que dibuja los efectos (no crea ventanas ni bloquea la entrada).
class EdfxOverlay final : public rex::ui::ImGuiDialog {
 public:
  explicit EdfxOverlay(rex::ui::ImGuiDrawer* drawer);

 protected:
  void OnDraw(ImGuiIO& io) override;
};

}  // namespace re_wor::de::edfx
