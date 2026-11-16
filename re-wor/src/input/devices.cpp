#include "input/devices.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <propkeydef.h>
#include <devpkey.h>
#include <functiondiscoverykeys_devpkey.h>
#include <hidsdi.h>
#include <setupapi.h>

#include <rex/input/input_driver.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <mutex>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

namespace re_wor::input {

namespace {

std::string Narrow(const wchar_t* w) {
  if (!w || !*w) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s(n > 0 ? n - 1 : 0, '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring Widen(const std::string& s) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(n > 0 ? n - 1 : 0, L'\0');
  if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
  return w;
}

std::string Trim(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
  size_t i = 0;
  while (i < s.size() && s[i] == ' ') i++;
  return s.substr(i);
}

// Nombre del producto de un teclado: el que da el HID, si no la descripcion del bus USB, si no VID/PID.
std::string FriendlyKeyboardName(const std::string& path) {
  const std::wstring wpath = Widen(path);
  std::string name;
  HANDLE h = CreateFileW(wpath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h != INVALID_HANDLE_VALUE) {
    wchar_t buf[128] = {};
    if (HidD_GetProductString(h, buf, sizeof(buf))) name = Trim(Narrow(buf));
    CloseHandle(h);
  }
  if (name.empty()) {
    HDEVINFO set = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (set != INVALID_HANDLE_VALUE) {
      SP_DEVICE_INTERFACE_DATA iface{sizeof(iface)};
      if (SetupDiOpenDeviceInterfaceW(set, wpath.c_str(), 0, &iface)) {
        SP_DEVINFO_DATA dev{sizeof(dev)};
        if (SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, nullptr, &dev) ||
            GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
          wchar_t buf[256] = {};
          DEVPROPTYPE type = 0;
          if (SetupDiGetDevicePropertyW(set, &dev, &DEVPKEY_Device_BusReportedDeviceDesc, &type,
                                        reinterpret_cast<PBYTE>(buf), sizeof(buf) - 2, nullptr, 0)) {
            name = Trim(Narrow(buf));
          }
        }
      }
      SetupDiDestroyDeviceInfoList(set);
    }
  }
  std::string upper = path;
  std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return char(std::toupper(c)); });
  const size_t v = upper.find("VID_");
  const size_t p = upper.find("PID_");
  if (name.empty()) {
    if (upper.find("ACPI#") != std::string::npos || upper.find("PS2") != std::string::npos) {
      name = "Teclado integrado";
    } else if (v != std::string::npos && p != std::string::npos) {
      name = "Teclado USB " + upper.substr(v + 4, 4) + ":" + upper.substr(p + 4, 4);
    } else {
      name = "Teclado";
    }
  }
  return name;
}

std::mutex g_kb_mutex;
std::map<uint32_t, std::string> g_path_by_id;
std::map<std::string, std::string> g_name_by_path;

std::string RawDevicePath(HANDLE h) {
  UINT size = 0;
  if (GetRawInputDeviceInfoA(h, RIDI_DEVICENAME, nullptr, &size) != 0 || size == 0) return {};
  std::string s(size, '\0');
  if (GetRawInputDeviceInfoA(h, RIDI_DEVICENAME, s.data(), &size) == static_cast<UINT>(-1)) return {};
  s.resize(std::strlen(s.c_str()));
  return s;
}

}  // namespace

std::vector<KeyboardInfo> ListKeyboards() {
  std::vector<KeyboardInfo> out;
  UINT count = 0;
  if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) != 0 || count == 0) return out;
  std::vector<RAWINPUTDEVICELIST> list(count);
  count = GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST));
  if (count == static_cast<UINT>(-1)) return out;
  for (UINT i = 0; i < count; i++) {
    if (list[i].dwType != RIM_TYPEKEYBOARD) continue;
    KeyboardInfo k;
    k.id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(list[i].hDevice));
    k.path = RawDevicePath(list[i].hDevice);
    if (k.path.empty()) continue;
    // Windows tambien expone teclados "virtuales" del sistema (Terminal Server, controles multimedia...)
    std::string upper = k.path;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    if (upper.find("ROOT#") != std::string::npos || upper.find("RDP_KBD") != std::string::npos) continue;
    k.name = KeyboardNameForPath(k.path);
    {
      std::lock_guard lock(g_kb_mutex);
      g_path_by_id[k.id] = k.path;
    }
    out.push_back(std::move(k));
  }
  // Un teclado USB puede tener varias interfaces de teclado (teclas multimedia, macro...). Se dejan todas:
  // las teclas pueden llegar por cualquiera. Los nombres repetidos se numeran.
  std::map<std::string, int> seen;
  for (auto& k : out) {
    const int n = ++seen[k.name];
    if (n > 1) k.name += " (" + std::to_string(n) + ")";
  }
  return out;
}

std::string KeyboardPathForId(uint32_t id) {
  if (!id) return {};
  {
    std::lock_guard lock(g_kb_mutex);
    auto it = g_path_by_id.find(id);
    if (it != g_path_by_id.end()) return it->second;
  }
  std::string path = RawDevicePath(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(id)));
  std::lock_guard lock(g_kb_mutex);
  g_path_by_id[id] = path;
  return path;
}

std::string KeyboardNameForPath(const std::string& path) {
  {
    std::lock_guard lock(g_kb_mutex);
    auto it = g_name_by_path.find(path);
    if (it != g_name_by_path.end()) return it->second;
  }
  std::string name = FriendlyKeyboardName(path);
  std::lock_guard lock(g_kb_mutex);
  g_name_by_path[path] = name;
  return name;
}

std::vector<MicInfo> ListMics() {
  std::vector<MicInfo> out;
  const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  IMMDeviceEnumerator* en = nullptr;
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                 reinterpret_cast<void**>(&en)))) {
    std::wstring def_id;
    IMMDevice* def = nullptr;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(eCapture, eConsole, &def))) {
      LPWSTR id = nullptr;
      if (SUCCEEDED(def->GetId(&id))) {
        def_id = id;
        CoTaskMemFree(id);
      }
      def->Release();
    }
    IMMDeviceCollection* col = nullptr;
    if (SUCCEEDED(en->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &col))) {
      UINT n = 0;
      col->GetCount(&n);
      for (UINT i = 0; i < n; i++) {
        IMMDevice* d = nullptr;
        if (FAILED(col->Item(i, &d))) continue;
        MicInfo m;
        LPWSTR id = nullptr;
        if (SUCCEEDED(d->GetId(&id))) {
          m.id = Narrow(id);
          m.is_default = def_id == id;
          CoTaskMemFree(id);
        }
        IPropertyStore* ps = nullptr;
        if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps))) {
          PROPVARIANT v;
          PropVariantInit(&v);
          if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) m.name = Narrow(v.pwszVal);
          PropVariantClear(&v);
          ps->Release();
        }
        if (m.name.empty()) m.name = "Microfono " + std::to_string(i + 1);
        d->Release();
        out.push_back(std::move(m));
      }
      col->Release();
    }
    en->Release();
  }
  if (SUCCEEDED(co)) CoUninitialize();
  return out;
}

// Mandos: los del driver real (SDL3 o XInput); lo pone input_factory.cpp
rex::input::InputDriver* g_pad_driver = nullptr;

namespace {
std::mutex g_pad_detect_mutex;
std::map<std::string, uint16_t> g_pad_detect_base;   // guid -> botones pulsados al empezar
uint16_t PadButtons(rex::input::DeviceId id) {
  rex::input::X_INPUT_STATE st{};
  if (!g_pad_driver || g_pad_driver->GetDeviceState(id, &st) != 0) return 0;
  uint16_t b = st.gamepad.buttons;
  if (st.gamepad.left_trigger > 100) b |= 0x0400;    // gatillos como botones (bits libres)
  if (st.gamepad.right_trigger > 100) b |= 0x0800;
  return b;
}
}  // namespace

void BeginPadDetect() {
  std::lock_guard lock(g_pad_detect_mutex);
  g_pad_detect_base.clear();
  if (!g_pad_driver) return;
  std::vector<rex::input::DeviceInfo> devs;
  g_pad_driver->EnumerateDevices(devs);
  for (auto& d : devs)
    if (!d.synthetic) g_pad_detect_base[d.guid] = PadButtons(d.id);
}

bool PollPadDetect(PadInfo* out) {
  std::lock_guard lock(g_pad_detect_mutex);
  if (!g_pad_driver) return false;
  std::vector<rex::input::DeviceInfo> devs;
  g_pad_driver->EnumerateDevices(devs);
  for (auto& d : devs) {
    if (d.synthetic) continue;
    const uint16_t now = PadButtons(d.id);
    uint16_t& base = g_pad_detect_base[d.guid];
    const uint16_t fresh = static_cast<uint16_t>(now & ~base);
    base = now;   // soltar y volver a pulsar tambien cuenta
    if (fresh) {
      if (out) *out = {d.guid, d.name.empty() ? std::string("Mando") : d.name, d.ordinal};
      return true;
    }
  }
  return false;
}

std::vector<PadInfo> ListPads() {
  std::vector<PadInfo> out;
  if (!g_pad_driver) return out;
  std::vector<rex::input::DeviceInfo> devs;
  g_pad_driver->EnumerateDevices(devs);
  for (auto& d : devs) {
    if (d.synthetic) continue;
    out.push_back({d.guid, d.name.empty() ? std::string("Mando") : d.name, d.ordinal});
  }
  return out;
}

}  // namespace re_wor::input
