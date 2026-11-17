#pragma once
// Piezas de la maquina de wui compartidas entre wui_vm.cpp y wui_stdlib.cpp (no es api publica).
#include <string>
#include <unordered_map>
#include <vector>

#include "de/wui_vm.hpp"

namespace wui {

struct ParamSig {
  const char* name;
  int kind;          // 0 normal, 1 kwonly, 2 *args, 3 **props
  bool has;          // tiene valor por defecto
  Value dflt;
};
struct MethodSig {
  const char* name;  // "Layer.glow"
  std::vector<ParamSig> params;
};
struct PropSig {
  const char* name;
  int type;          // 0 numero, 1 bool, 2 texto, 3 color
  bool writable, anim;
  Value dflt;
};

const std::vector<MethodSig>& Methods();
const std::vector<PropSig>& Props();
const PropSig* FindProp(const std::string& name);
const std::vector<std::string>& ButtonNames();
const std::vector<std::string>& SoundNames();
const std::vector<std::string>& PlayerFields();
const std::vector<std::string>& BandFields();

const std::unordered_map<std::string, BuiltinFn>& Stdlib();

// orden como Python: -1, 0, 1; *ok = false si no se pueden comparar
int Cmp(const Value& a, const Value& b, bool* ok);
std::string FixedStr(double x, int p);
std::vector<uint32_t> Utf8Decode(const std::string& s);
std::string Utf8Encode(const std::vector<uint32_t>& cps);
std::string ReceiverOf(const Value& v);

}  // namespace wui
