#pragma once
// =============================================================================
// qb_bridge.hpp - Puente de desarrollo con el motor de scripts QB de Neversoft.
//
// Todo lo que toca al motor se ejecuta en el hilo del juego (frame logico): las
// ordenes se encolan desde cualquier hilo (panel F8, consola, puerto local) y se
// despachan en OnLogicFrame. Lo que hace cada orden:
//   run <script|cfunc> [parametros]   lanza un script QB (como SpawnScriptLater) o
//                                     llama a una funcion nativa del motor
//   exists <nombre>                   que es ese simbolo (script, cfunc, global...)
//   get <global> / set <global> <v>   leer / cambiar globales enteras, reales o nombres
//   debugmenu                         abre el menu de depuracion original de Neversoft
//   debugbuild on|off                 lo que responde DebugBuild a los scripts
//   loadqb <archivo.qb.xen>           registra un archivo de scripts compilado (tools/qbc.py)
//   rename <simbolo> <nuevo>          cambia el nombre de un simbolo de la tabla global
// Parametros al estilo QB: clave=valor, valor = 12 | 1.5 | "texto" | nombre | {anidado}
// y nombres sueltos como banderas: run ui_event event=menu_change data={state=UIstate_debug}
//
// Direcciones (ejecutable de WoR, tabla de CFuncs en cache/re/cfuncs.tsv):
//   tabla de simbolos global  *0x82986970 (cubos de 4 bytes, indice rotl(clave,2) & 0x1FFFC)
//   entrada: +2 tipo, +4 clave, +12 valor (o destino del alias), +16 siguiente
//   tipos: 1 entero, 2 real, 3 texto, 7 script, 8 cfunc, 10 estructura, 12 array, 13 nombre, 26 alias
//   sub_823A5450 crear script (r3 clave, r4 CStruct* parametros; si es cfunc la llama al momento)
// =============================================================================

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct PPCContext;

namespace re_wor::de::qb_bridge {

// Clave QB (CRC32 sin inversion final, minusculas, '/' -> '\')
uint32_t CalculateQbKey(const std::string& name);

// Tipo del simbolo global (0 = no existe). Lectura directa de la tabla: se puede llamar desde la interfaz.
int SymbolType(uint32_t checksum, uint8_t* base);
const char* SymbolTypeName(int type);
bool ScriptExists(uint32_t checksum, uint8_t* base);
bool ScriptExists(const std::string& name, uint8_t* base);

// Orden de texto (ver cabecera). done (opcional) recibe la respuesta en el hilo del juego.
void Submit(const std::string& line, std::function<void(bool ok, const std::string& reply)> done = {});

// Atajos usados por el panel
void ExecuteScript(const std::string& name_and_params);
void ExecuteScriptQuiet(const std::string& name_and_params);   // igual, sin registro si sale bien
void OpenDebugMenu();
void UnlockEverything();   // scripts de desbloqueo del menu debug: CAMBIA la partida guardada

enum class BuiltinCheat { kHyperspeed, kInvincible, kAlwaysSlide, kPerformanceMode, kAutoPlay, kHideHud, kFreeDrum };
void TriggerCheat(BuiltinCheat cheat);

// Respuesta de DebugBuild (sub_8259EBA8) a los scripts
void SetDebugBuild(bool enabled);
bool IsDebugBuild();

// Hilo del juego, cada frame logico (OnLogicFrameExtras)
void OnLogicFrame(PPCContext& ctx, uint8_t* base);

// Puerto local para el editor (127.0.0.1, protocolo de lineas con clave). port 0 = no arrancar.
void StartServer(int port);
void StopServer();
int ServerPort();   // 0 si no esta escuchando

size_t GetLoadedScriptCount(uint8_t* base);

// Scripts propios: <carpeta del .exe>/qb/*.qb.xen (compilados con tools/qbc.py) se registran en cuanto
// el juego termina de arrancar. Si el archivo trae "re_hooks_<nombre>", cada script de esa lista se
// renombra antes a re_orig_<script> para que la version nueva pueda llamar al original.
void SetGuestAllocator(std::function<uint32_t(uint32_t)> alloc);   // memoria del juego (permanente)
bool ScriptsLoaded();
// Globales enteras (las usa el menu Options > Graphics del juego). false si no existe o no es entera.
bool ReadIntGlobal(const std::string& name, uint8_t* base, int* out);
bool WriteIntGlobal(const std::string& name, uint8_t* base, int value);
bool WriteFloatGlobal(const std::string& name, uint8_t* base, float value);
void ApplyEngineLimitsPatches(uint8_t* base);

// Valor actual de una global para mostrarlo ("30 (entero)"; vacio si no existe). Solo lee memoria.
std::string PeekGlobal(const std::string& name, uint8_t* base);

// Pantallas del menu de depuracion de Neversoft (estado de interfaz, titulo)
struct DebugScreen {
  const char* state;
  const char* title;
};
const std::vector<DebugScreen>& DebugScreens();

struct LogEntry {
  std::string message;
  bool success;
  double timestamp;
};
std::vector<LogEntry> GetRecentLogs();

}  // namespace re_wor::de::qb_bridge
