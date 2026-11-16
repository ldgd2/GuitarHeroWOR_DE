#include "input/mic_capture.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "core/crash_handler.hpp"
#include "de/de_config.hpp"

namespace re_wor::input {

namespace {

std::wstring Widen(const std::string& s) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(n > 0 ? n - 1 : 0, L'\0');
  if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
  return w;
}

class Mic {
 public:
  ~Mic() { Close(); }

  void Open(const std::string& id, const std::string& name, float gain) {
    Close();
    {
      std::lock_guard lock(mutex_);
      id_ = id;
      name_ = name;
      status_ = "abriendo";
      head_ = tail_ = 0;
    }
    gain_ = gain;
    run_ = true;
    thread_ = std::thread([this] { Run(); });
  }

  void Close() {
    run_ = false;
    if (thread_.joinable()) thread_.join();
    std::lock_guard lock(mutex_);
    id_.clear();
    status_.clear();
    open_ = false;
    level_ = 0.f;
  }

  const std::string& id() const { return id_; }
  void set_gain(float g) { gain_ = g; }
  bool open() const { return open_; }
  float level() const { return level_; }
  std::string status() {
    std::lock_guard lock(mutex_);
    return status_;
  }
  size_t Available() {
    std::lock_guard lock(mutex_);
    return (head_ - tail_);
  }
  size_t Read(int16_t* out, size_t n) {
    std::lock_guard lock(mutex_);
    const size_t avail = head_ - tail_;
    n = std::min(n, avail);
    for (size_t i = 0; i < n; i++) out[i] = ring_[(tail_ + i) % ring_.size()];
    tail_ += n;
    return n;
  }

 private:
  void Push(const float* mono16k, size_t n) {
    std::lock_guard lock(mutex_);
    for (size_t i = 0; i < n; i++) {
      const float v = std::clamp(mono16k[i] * gain_, -1.f, 1.f);
      ring_[head_ % ring_.size()] = static_cast<int16_t>(v * 32767.f);
      head_++;
    }
    // si nadie lee (el juego no esta en una cancion), se queda solo lo ultimo: maximo 0.25 s de retraso
    const size_t keep = kMicRate / 4;
    if (head_ - tail_ > keep) tail_ = head_ - keep;
  }

  void Fail(const std::string& why) {
    std::lock_guard lock(mutex_);
    status_ = "error: " + why;
    open_ = false;
  }

  void Run() {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* en = nullptr;
    IMMDevice* dev = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* cap = nullptr;
    WAVEFORMATEX* fmt = nullptr;
    auto cleanup = [&] {
      if (client) client->Stop();
      if (cap) cap->Release();
      if (client) client->Release();
      if (dev) dev->Release();
      if (en) en->Release();
      if (fmt) CoTaskMemFree(fmt);
      if (SUCCEEDED(co)) CoUninitialize();
    };
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&en)))) {
      Fail("WASAPI no disponible");
      return cleanup();
    }
    HRESULT hr = (id_ == "default" || id_.empty()) ? en->GetDefaultAudioEndpoint(eCapture, eConsole, &dev)
                                                   : en->GetDevice(Widen(id_).c_str(), &dev);
    if (FAILED(hr) || !dev) {
      Fail("no se encuentra el microfono (desconectado?)");
      return cleanup();
    }
    if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client))) ||
        FAILED(client->GetMixFormat(&fmt))) {
      Fail("no se pudo abrir el dispositivo");
      return cleanup();
    }
    // 20 ms de buffer: poca latencia (la afinacion se juzga contra la cancion)
    if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 200000, 0, fmt, nullptr)) ||
        FAILED(client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&cap))) ||
        FAILED(client->Start())) {
      Fail("Windows no dejo capturar (permiso de microfono en Privacidad?)");
      return cleanup();
    }
    // formato de entrada
    bool is_float = fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
      auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(fmt);
      is_float = IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != 0;
    }
    const int channels = std::max<int>(1, fmt->nChannels);
    const int bits = fmt->wBitsPerSample;
    const int bytes = bits / 8;
    const double in_rate = fmt->nSamplesPerSec;
    {
      std::lock_guard lock(mutex_);
      status_ = "abierto: " + name_;
      open_ = true;
    }
    TRACE("[Micro] abierto '" << name_ << "' " << fmt->nSamplesPerSec << " Hz, " << channels << " canales, "
                              << bits << " bits" << (is_float ? " float" : ""));
    std::vector<float> mono, out;
    double phase = 0.0;   // posicion de remuestreo
    float prev = 0.f;
    float peak = 0.f;
    int peak_samples = 0;
    while (run_) {
      Sleep(5);
      UINT32 packet = 0;
      while (run_ && SUCCEEDED(cap->GetNextPacketSize(&packet)) && packet > 0) {
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        if (FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
        mono.assign(frames, 0.f);
        if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
          for (UINT32 f = 0; f < frames; f++) {
            float acc = 0.f;
            for (int c = 0; c < channels; c++) {
              const BYTE* s = data + (static_cast<size_t>(f) * channels + c) * bytes;
              float v = 0.f;
              if (is_float && bits == 32) v = *reinterpret_cast<const float*>(s);
              else if (bits == 16) v = *reinterpret_cast<const int16_t*>(s) / 32768.f;
              else if (bits == 24) v = static_cast<float>((int32_t(s[0] << 8) | int32_t(s[1] << 16) | int32_t(s[2] << 24)) >> 8) / 8388608.f;
              else if (bits == 32) v = *reinterpret_cast<const int32_t*>(s) / 2147483648.f;
              acc += v;
            }
            mono[f] = acc / channels;
          }
        }
        cap->ReleaseBuffer(frames);
        // remuestreo lineal a 16 kHz
        out.clear();
        const double step = in_rate / kMicRate;
        for (float cur : mono) {
          while (phase < 1.0) {
            out.push_back(prev + static_cast<float>(phase) * (cur - prev));
            phase += step;
          }
          phase -= 1.0;
          prev = cur;
        }
        for (float v : out) {
          peak = std::max(peak, std::fabs(v * gain_));
          if (++peak_samples >= kMicRate / 10) {
            level_ = std::min(1.f, peak);
            peak = 0.f;
            peak_samples = 0;
          }
        }
        Push(out.data(), out.size());
      }
    }
    cleanup();
  }

  std::mutex mutex_;
  std::string id_, name_, status_;
  std::vector<int16_t> ring_ = std::vector<int16_t>(kMicRate * 2);
  size_t head_ = 0, tail_ = 0;
  std::atomic<bool> run_{false};
  std::atomic<bool> open_{false};
  std::atomic<float> level_{0.f};
  std::atomic<float> gain_{1.f};
  std::thread thread_;
};

Mic g_mics[re_wor::de::kMaxPlayers];
std::mutex g_apply_mutex;

}  // namespace

void ApplyMicConfig() {
  std::lock_guard lock(g_apply_mutex);
  re_wor::de::PlayerSlotConfig players[re_wor::de::kMaxPlayers];
  {
    std::lock_guard cfg(re_wor::de::Mutex());
    for (int i = 0; i < re_wor::de::kMaxPlayers; i++) players[i] = re_wor::de::Get().input.players[i];
  }
  for (int i = 0; i < re_wor::de::kMaxPlayers; i++) {
    const auto& p = players[i];
    if (p.mic.empty()) {
      if (!g_mics[i].id().empty()) g_mics[i].Close();
      continue;
    }
    g_mics[i].set_gain(p.mic_gain);
    if (g_mics[i].id() != p.mic) {
      g_mics[i].Open(p.mic, p.mic_name.empty() ? std::string("Microfono predeterminado") : p.mic_name, p.mic_gain);
      TRACE("[Micro] jugador " << (i + 1) << ": " << (p.mic_name.empty() ? p.mic : p.mic_name));
    }
  }
}

bool MicActive(int p) { return p >= 0 && p < re_wor::de::kMaxPlayers && g_mics[p].open(); }
size_t MicAvailable(int p) { return p >= 0 && p < re_wor::de::kMaxPlayers ? g_mics[p].Available() : 0; }
size_t MicRead(int p, int16_t* out, size_t n) {
  return p >= 0 && p < re_wor::de::kMaxPlayers ? g_mics[p].Read(out, n) : 0;
}
float MicLevel(int p) { return p >= 0 && p < re_wor::de::kMaxPlayers ? g_mics[p].level() : 0.f; }
std::string MicStatus(int p) { return p >= 0 && p < re_wor::de::kMaxPlayers ? g_mics[p].status() : std::string(); }

}  // namespace re_wor::input
