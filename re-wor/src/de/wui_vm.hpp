#pragma once
// =============================================================================
// Maquina de wui en C++: ejecuta en el juego los programas (.wuib) que escribe el editor de interfaces de
// DE Studio en Python. Es una traduccion directa de la maquina de referencia (tools/wui/wui/runtime, Python) y
// de la del editor (DEStudio/web/src/wui, JS): las pruebas de tools/wui/tests/conformance comparan las tres.
//
// No depende de re-wor: quien dibuja (ui3d.cpp en el juego, o el ejecutor de pruebas) implementa Host.
// Todo se usa desde un solo hilo.
// =============================================================================
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace wui {

struct ListObj;
struct TaskObj;
using ListPtr = std::shared_ptr<ListObj>;
using TaskPtr = std::shared_ptr<TaskObj>;

struct Value {
  enum class T : uint8_t { None, Bool, Int, Float, Str, List, Layer, Func, Task, Unset };
  T t = T::None;
  bool b = false;
  int64_t i = 0;      // entero, o indice de funcion
  double f = 0;
  std::string s;      // texto, nombre de capa o de funcion
  ListPtr l;
  TaskPtr task;

  static Value N() { return {}; }
  static Value B(bool v) { Value x; x.t = T::Bool; x.b = v; return x; }
  static Value I(int64_t v) { Value x; x.t = T::Int; x.i = v; return x; }
  static Value F(double v) { Value x; x.t = T::Float; x.f = v; return x; }
  static Value S(std::string v) { Value x; x.t = T::Str; x.s = std::move(v); return x; }
  static Value Lst(std::vector<Value> v, bool frozen = false);
  static Value Lay(std::string name) { Value x; x.t = T::Layer; x.s = std::move(name); return x; }
  static Value Fn(int index, std::string name) { Value x; x.t = T::Func; x.i = index; x.s = std::move(name); return x; }
  static Value Tk(TaskPtr p) { Value x; x.t = T::Task; x.task = std::move(p); return x; }
  static Value Unset() { Value x; x.t = T::Unset; return x; }

  bool none() const { return t == T::None; }
  bool num() const { return t == T::Int || t == T::Float; }       // sin bool (is_number)
  bool numb() const { return num() || t == T::Bool; }             // con bool (operadores)
  double d() const { return t == T::Float ? f : t == T::Bool ? (b ? 1.0 : 0.0) : static_cast<double>(i); }
};
using V = Value;

struct ListObj {
  std::vector<Value> v;
  bool frozen = false;     // constante de la libreria (FRETS...) o del programa
};
struct TaskObj {
  int id = 0;
  std::string name;
  Value result;
};
inline Value Value::Lst(std::vector<Value> v, bool frozen) {
  Value x;
  x.t = T::List;
  x.l = std::make_shared<ListObj>();
  x.l->v = std::move(v);
  x.l->frozen = frozen;
  return x;
}

// Error del programa al ejecutar: para solo la tarea que lo provoca.
struct Fault {
  std::string message;
};

// ---------------------------------------------------------------- programa
struct Ins {
  uint8_t op, a;
  uint16_t b;
  int32_t c;
};
struct Function {
  std::string name;
  int nparams = 0, nlocals = 0, flags = 0;
  std::vector<Ins> code;
  std::vector<std::pair<uint32_t, uint32_t>> lines;
  std::vector<Value> defaults;
  std::vector<std::string> local_names;
  int LineAt(uint32_t pc) const;
};
struct Handler {
  std::string event;
  int func = 0;
  std::vector<Value> args;
};
struct Program {
  std::string source;
  std::vector<Value> consts;
  std::vector<std::string> names, builtins, globals;
  std::vector<Function> functions;
  std::vector<Handler> handlers;
  int init = 0xFFFF;
};
// false y el motivo en *error si los bytes no son un programa valido
bool LoadProgram(const uint8_t* data, size_t size, Program* out, std::string* error);

// ---------------------------------------------------------------- texto y valores
std::string TypeName(const Value& v);
std::string ToText(const Value& v);
std::string ReprFloat(double x);
bool Truthy(const Value& v);
bool Eq(const Value& a, const Value& b);
std::string FormatValue(const Value& v, const std::string& spec);
bool ParseColor(const Value& v, int rgb[3]);
std::string ColorHex(const int rgb[3]);
double Ease(const std::string& name, double t);

// ---------------------------------------------------------------- anfitrion
class Host {
 public:
  virtual ~Host() = default;
  virtual std::vector<std::string> LayerNames() { return {}; }
  virtual Value LayerGet(const std::string&, const std::string&) { return {}; }
  virtual void LayerSet(const std::string&, const std::string&, const Value&) {}
  virtual std::map<std::string, Value> LayerDesign(const std::string&) { return {}; }
  virtual bool LayerContains(const std::string&, double, double) { return false; }
  virtual std::optional<std::string> LayerAt(double, double) { return std::nullopt; }
  virtual std::vector<std::string> ScreenNames() { return {}; }
  virtual void ShowScreen(const std::string&, const std::string&, const std::optional<std::string>&) {}
  virtual void Close() {}
  virtual void SetFocus(const std::optional<std::string>&) {}
  virtual void MoveFocus(const std::string&) {}
  virtual void LockFocus(bool) {}
  virtual Value GameGet(const std::string&) { return {}; }
  virtual void GameSet(const std::string&, const Value&) {}
  virtual Value GameCall(const std::string&, const std::vector<std::pair<std::string, Value>>&) { return {}; }
  virtual Value PlayerInfo(int, const std::string&) { return {}; }
  virtual Value BandInfo(const std::string&) { return {}; }
  virtual Value GameState(const std::string&) { return {}; }
  virtual void PlaySound(const std::string&, double) {}
  virtual void Vibrate(int, double, double) {}
  virtual void Log(const std::string&, const std::string&) {}
};

// Estado del mando de un fotograma
struct Inputs {
  std::map<int, std::set<std::string>> buttons;            // jugador -> botones pulsados
  std::map<int, std::map<std::string, double>> axes;       // jugador -> whammy, tilt...
  double mouse_x = 0, mouse_y = 0;
  bool mouse_down = false;
  std::map<int, std::string> kinds;
};

class Machine;
// Funcion de la libreria que duerme la tarea: poll(vm, &valor) -> terminado
using Block = std::function<bool(Machine&, Value*)>;

struct ErrorRecord {
  std::string message, function;
  int line = 0;
  double time = 0;
  std::vector<std::string> trace;
};

struct Task;
struct Ctx {
  Machine& vm;
  Task* task;
  Block block;          // la funcion pide dormir la tarea
};
using Kw = std::vector<std::pair<std::string, Value>>;
using BuiltinFn = std::function<Value(Ctx&, std::vector<Value>&, Kw&)>;

// ---------------------------------------------------------------- maquina
class Machine {
 public:
  Machine(Program program, Host* host, uint64_t seed = 0, int budget = 200000, int max_tasks = 256);
  ~Machine();
  // false si el programa usa funciones que esta maquina no tiene (y por que)
  bool Ok(std::string* why = nullptr) const;

  void Start(const std::string& screen = "");
  void Step(double dt, const Inputs& inputs);
  void NotifyFocus(const std::optional<std::string>& name);
  void NotifyClick(const std::string& name);
  bool Closed() const { return closed_; }
  const std::optional<std::string>& Screen() const { return screen_; }
  const std::optional<std::string>& Focused() const { return focused_; }

  const std::vector<ErrorRecord>& Errors() const { return errors_; }
  const std::vector<std::pair<std::string, std::string>>& Logs() const { return logs_; }
  Value GlobalValue(const std::string& name) const;
  const Program& Prog() const { return prog_; }

  // ---- eventos y errores (tambien para la libreria)
  void Fire(const std::string& event, std::vector<Value> args, const std::function<bool(const Handler&)>& match = {});
  void Error(const std::string& message, const std::string& function, int line, std::vector<std::string> trace);

  // ---- api para la libreria (stdlib)
  [[noreturn]] void Raise(const std::string& message) const { throw Fault{message}; }
  void LogMsg(const std::string& level, const std::string& message);
  void CheckLayer(const std::string& name);
  Value LayerGetP(const std::string& name, const std::string& prop);
  void LayerSetP(const std::string& name, const std::string& prop, Value v, bool from_anim = false);
  void Goto(const std::string& name, const std::string& transition, bool push = true);
  void Back(const std::string& transition);
  void CloseUi();
  void SetFocus(const std::optional<std::string>& name);
  void Emit(const std::string& name, const Value& v);
  Value Spawn(const Value& fn, std::vector<Value> args, double delay = 0, std::optional<double> every = std::nullopt);
  void StopTask(const TaskPtr& ref);
  void StopAll(Task* except);
  bool TaskRunning(const TaskPtr& ref) const;
  Value CallNow(const Value& fn, std::vector<Value> args);

  // tween(capa, prop, a, segundos, curva, retraso, repeticiones, ida y vuelta, al acabar, funcion, acumular)
  int Tween(const std::string& layer, const std::string& prop, Value to, double time, const std::string& ease = "out",
            double delay = 0, int loop = 0, bool yoyo = false, std::function<void(Machine&)> on_end = {},
            std::function<Value(double)> fn = {}, bool accumulate = false);
  void StopAnim(const std::string& layer, const std::vector<std::string>* props = nullptr);
  bool AnimBusy(const std::string& layer) const;
  Value AnimTarget(const std::string& layer, const std::string& prop);
  Block WaitFor(std::vector<int> ids);

  // mando
  bool Pressed(const std::string& b, int p) const;
  bool JustPressed(const std::string& b, int p) const;
  bool JustReleased(const std::string& b, int p) const;
  double HeldTime(const std::string& b, int p) const;
  std::set<std::string> Buttons(int p) const;
  double Axis(const std::string& n, int p) const;
  bool Connected(int p) const;
  std::string Kind(int p) const;

  // azar (igual que random.Random(semilla) de Python)
  double Random();
  uint64_t Below(uint64_t n);

  Host* host;
  double time = 0, dt = 0;
  int64_t frame = 0;
  double mouse_x = 0, mouse_y = 0;
  bool mouse_down = false;
  bool focus_locked = false;
  std::vector<std::string> history;

 private:
  struct Impl;
  std::unique_ptr<Impl> im_;
  Program prog_;
  std::vector<Value> globals_;
  std::optional<std::string> screen_, focused_, hovered_;
  bool closed_ = false, started_ = false;
  std::vector<ErrorRecord> errors_;
  std::vector<std::pair<std::string, std::string>> logs_;
  void SpawnHandler(const Handler& h, const std::vector<Value>& args);
  friend struct Impl;
};

}  // namespace wui
