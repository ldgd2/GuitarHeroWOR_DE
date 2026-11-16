#pragma once

#include <fstream>
#include <sstream>
#include <iostream>
#include <string>

namespace re_wor::core {

std::ofstream& get_trace_log();

// Encola una linea para re_wor_trace.log. La escribe un hilo aparte cada 100 ms: el hilo que llama
// (juego, carga de archivos, audio) nunca espera al disco.
void log_trace_message(const std::string& msg);

// Vuelca ya todo lo pendiente (crash, salida del proceso).
void flush_trace();

void install_crash_handlers();

// Texto extra para el registro de un fallo (p. ej. por donde iba un script propio). Se llama dentro del
// manejador de excepciones: solo debe leer memoria.
void set_crash_note(std::string (*note)());

// TRACE(a << b << std::endl): cada hilo arma su linea en un buffer propio (antes todos escribian
// directo en el archivo sin cerrojo y con flush por linea) y al llegar std::endl la encola.
class TraceStream {
 public:
  template <typename T>
  TraceStream& operator<<(const T& val) {
    line() << val;
    return *this;
  }

  TraceStream& operator<<(std::ostream& (*manip)(std::ostream&)) {
    if (manip == static_cast<std::ostream& (*)(std::ostream&)>(std::endl<char, std::char_traits<char>>)) {
      end_line();
    } else {
      manip(line());
    }
    return *this;
  }

 private:
  static std::ostringstream& line();
  static void end_line();
};

TraceStream& get_trace_stream();

}  // namespace re_wor::core

#define TRACE(msg) do { \
  ::re_wor::core::get_trace_stream() << msg << std::endl; \
} while(0)

