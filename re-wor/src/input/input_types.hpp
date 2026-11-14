#pragma once

#include <cstdint>
#include <string>

namespace re_wor::input {

enum class InstrumentType {
  None,
  Guitar5Fret,
  Guitar6Fret,
  Bass,
  Drums4Lane,   // Rock Band / 4 pads + kick
  Drums5Lane,   // Guitar Hero / 5 pads + kick
  Microphone,
  Gamepad
};

enum class ControllerSlot : uint8_t {
  Player1 = 0,
  Player2 = 1,
  Player3 = 2,
  Player4 = 3,
  Count = 4
};

// Flags de trastes de Guitarra / Bajo (5 trastes estándar y 6 trastes GHLive)
enum FretFlags : uint16_t {
  Fret_None    = 0,
  Fret_Green   = 1 << 0,
  Fret_Red     = 1 << 1,
  Fret_Yellow  = 1 << 2,
  Fret_Blue    = 1 << 3,
  Fret_Orange  = 1 << 4,
  Fret_Open    = 1 << 5, // Nota abierta de Bajo
  Fret_Black1  = 1 << 6, // 6-Fret superior
  Fret_Black2  = 1 << 7,
  Fret_Black3  = 1 << 8,
  Fret_White1  = 1 << 9, // 6-Fret inferior
  Fret_White2  = 1 << 10,
  Fret_White3  = 1 << 11,
};

// Flags de pads y pedales de Batería
enum DrumFlags : uint16_t {
  Drum_None       = 0,
  Drum_RedPad     = 1 << 0, // Snare
  Drum_YellowPad  = 1 << 1, // Hi-Hat / Tom 1
  Drum_BluePad    = 1 << 2, // Tom 2 / Ride
  Drum_GreenPad   = 1 << 3, // Floor Tom / Crash
  Drum_OrangePad  = 1 << 4, // GH 5-Lane Cymbal / Crash
  Drum_Kick1      = 1 << 5, // Bass Drum Pedal
  Drum_Kick2      = 1 << 6, // Double Bass Pedal
  Drum_HiHatPedal = 1 << 7, // Pedal de charles
};

struct InputDeviceState {
  InstrumentType instrument = InstrumentType::None;
  bool is_connected = false;
  std::string device_name;
  uint32_t buttons_pressed = 0;
  float whammy_axis = 0.0f;       // -1.0 a 1.0 (Guitarra)
  float tilt_axis = 0.0f;         // 0.0 a 1.0 (Star Power)
  float slider_position = 0.0f;   // Touch strip del mástil
  float mic_pitch = 0.0f;         // Tono de micrófono en Hz / semitonos
  float mic_amplitude = 0.0f;     // Amplitud / volumen
  uint8_t drum_velocities[8] = {0}; // Sensibilidad a la fuerza de golpeo
};

}  // namespace re_wor::input

