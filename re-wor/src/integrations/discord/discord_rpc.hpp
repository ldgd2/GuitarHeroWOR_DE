#pragma once

#include <string>

namespace re_wor::integrations::discord {

struct PresenceInfo {
  std::string song_title;
  std::string artist;
  std::string difficulty;
  std::string instrument;
  std::string game_mode;
  int score = 0;
  int streak = 0;
  int stars = 0;
};

void initialize();
void update();
void update_song_presence(const PresenceInfo& info);
void shutdown();

}  // namespace re_wor::integrations::discord

