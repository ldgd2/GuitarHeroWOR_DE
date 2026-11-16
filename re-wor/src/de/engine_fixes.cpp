// Arreglos de fallos del motor original (WoR) que en la consola pasaban de largo y en PC cierran el juego.
//
// SongFileManager (CFunc 0x821AF560): los scripts de la cancion le piden datos del archivo de la cancion (notas,
// efectos, luces...) por nombre. Para cada peticion busca en una tabla hash con sub_821AE590. Si el nombre no esta
// (pasa con canciones custom a las que les falta alguna seccion), sub_821AE590 devuelve basura de la pila (lee
// -16(r1) sin inicializar) y luego +12 de ella. En la Xbox esa basura solia ser un puntero valido; aqui es 0 y el
// juego se cierra (Access Violation en __imp__sub_821AE590 + 0x213, target 0x10000000c).
//
// Arreglo: dentro de SongFileManager la busqueda se hace aqui con comprobaciones. Si el nombre no esta, esa peticion
// se cancela: se restauran los registros del juego y SongFileManager devuelve 1 sin dar resultado, que para el
// script es lo mismo que una seccion vacia. Se apunta en re_wor_trace.log. Fuera de SongFileManager sub_821AE590
// funciona como siempre.
#include <csetjmp>
#include <cstdint>
#include <mutex>
#include <set>

#include "core/crash_handler.hpp"
#include "generated/re_wor_pch.h"

namespace {
thread_local std::jmp_buf* t_song_file_escape = nullptr;

void LogMissing(uint32_t key) {
  static std::mutex mx;
  static std::set<uint32_t> seen;
  std::lock_guard lk(mx);
  if (seen.size() >= 64 || !seen.insert(key).second) return;
  TRACE("[Arreglos] SongFileManager: la cancion no tiene la seccion 0x" << std::hex << key << std::dec
                                                                       << "; se ignora esa peticion (antes cerraba el juego)");
}
}  // namespace

extern "C" void __imp__sub_821AE590(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_821AE590) {
  if (!t_song_file_escape) {
    __imp__sub_821AE590(ctx, base);
    return;
  }
  const uint32_t table = ctx.r3.u32;
  const uint32_t key = ctx.r4.u32;
  const uint32_t begin = table ? REX_LOAD_U32(table + 144) : 0;
  const uint32_t end = table ? REX_LOAD_U32(table + 148) : 0;
  if (begin && end > begin) {
    const uint32_t buckets = (end - begin) / 12;
    if (buckets) {
      const uint32_t bucket = begin + (key % buckets) * 12;
      const uint32_t sentinel = bucket + 4;
      uint32_t node = REX_LOAD_U32(bucket + 8);
      for (int guard = 0; node && node != sentinel && guard < 100000; ++guard) {
        if (REX_LOAD_U32(node + 8) == key) {
          ctx.r3.u64 = REX_LOAD_U32(node + 12);
          return;
        }
        node = REX_LOAD_U32(node + 4);
      }
    }
  }
  LogMissing(key);
  std::longjmp(*t_song_file_escape, 1);
}

extern "C" void __imp__sub_821AF560(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_821AF560) {   // SongFileManager
  const PPCContext saved = ctx;
  std::jmp_buf escape;
  std::jmp_buf* const prev = t_song_file_escape;
  t_song_file_escape = &escape;
  if (setjmp(escape) == 0) {
    __imp__sub_821AF560(ctx, base);
  } else {
    ctx = saved;        // registros y pila del juego como antes de la peticion
    ctx.r3.u64 = 1;     // el script sigue; simplemente no recibe esa seccion
  }
  t_song_file_escape = prev;
}
