#include "integrations/discord/discord_rpc.hpp"
#include "core/crash_handler.hpp"

#if defined(REWOR_HAS_DISCORD_RPC)
#include <cstdint>
#include <ctime>

extern "C" {

struct DiscordUser {
  const char* userId;
  const char* username;
  const char* discriminator;
  const char* avatar;
};

struct DiscordEventHandlers {
  void (*ready)(const DiscordUser* request);
  void (*disconnected)(int errorCode, const char* message);
  void (*errored)(int errorCode, const char* message);
  void (*joinGame)(const char* joinSecret);
  void (*spectateGame)(const char* spectateSecret);
  void (*joinRequest)(const DiscordUser* request);
};

struct DiscordRichPresence {
  const char* state;
  const char* details;
  std::int64_t startTimestamp;
  std::int64_t endTimestamp;
  const char* largeImageKey;
  const char* largeImageText;
  const char* smallImageKey;
  const char* smallImageText;
  const char* partyId;
  int partySize;
  int partyMax;
  int partyPrivacy;
  const char* matchSecret;
  const char* joinSecret;
  const char* spectateSecret;
  std::int8_t instance;
};

void Discord_Initialize(const char* applicationId,
                        DiscordEventHandlers* handlers,
                        int autoRegister,
                        const char* optionalSteamId);
void Discord_Shutdown(void);
void Discord_RunCallbacks(void);
void Discord_UpdatePresence(const DiscordRichPresence* presence);

} // extern "C"
#endif

namespace re_wor::integrations::discord {

namespace {

constexpr const char* kDiscordApplicationId = "1477849970194251979";

#if defined(REWOR_HAS_DISCORD_RPC)
bool g_discord_initialized = false;

void handle_ready(const DiscordUser*) {
  TRACE("[DiscordRPC] Connected to Discord Gateway");
}
void handle_disconnected(int code, const char* msg) {
  TRACE("[DiscordRPC] Disconnected: " << code << " - " << (msg ? msg : ""));
}
void handle_error(int code, const char* msg) {
  TRACE("[DiscordRPC] Error: " << code << " - " << (msg ? msg : ""));
}
#endif

} // namespace

void initialize() {
#if defined(REWOR_HAS_DISCORD_RPC)
  if (g_discord_initialized) {
    return;
  }

  DiscordEventHandlers handlers{};
  handlers.ready = &handle_ready;
  handlers.disconnected = &handle_disconnected;
  handlers.errored = &handle_error;

  Discord_Initialize(kDiscordApplicationId, &handlers, 0, nullptr);

  DiscordRichPresence presence{};
  presence.state = "Warriors of Rock Definitive Edition";
  presence.details = "Main Menu";
  presence.largeImageKey = "game_icon";
  presence.largeImageText = "Guitar Hero: Warriors of Rock DE";
  presence.startTimestamp = static_cast<std::int64_t>(std::time(nullptr));

  Discord_UpdatePresence(&presence);
  g_discord_initialized = true;
  TRACE("[DiscordRPC] Initialized successfully");
#endif
}

void update() {
#if defined(REWOR_HAS_DISCORD_RPC)
  if (g_discord_initialized) {
    Discord_RunCallbacks();
  }
#endif
}

void update_song_presence(const PresenceInfo& info) {
#if defined(REWOR_HAS_DISCORD_RPC)
  if (!g_discord_initialized) return;

  std::string details = info.song_title.empty() ? "Playing" : (info.song_title + " - " + info.artist);
  std::string state = info.difficulty + " " + info.instrument;
  if (info.game_mode.length() > 0) state += " (" + info.game_mode + ")";

  DiscordRichPresence presence{};
  presence.details = details.c_str();
  presence.state = state.c_str();
  presence.largeImageKey = "game_icon";
  presence.largeImageText = "Guitar Hero: Warriors of Rock DE";
  presence.startTimestamp = static_cast<std::int64_t>(std::time(nullptr));

  Discord_UpdatePresence(&presence);
#else
  (void)info;
#endif
}

void shutdown() {
#if defined(REWOR_HAS_DISCORD_RPC)
  if (g_discord_initialized) {
    Discord_Shutdown();
    g_discord_initialized = false;
    TRACE("[DiscordRPC] Shutdown cleanly");
  }
#endif
}

}  // namespace re_wor::integrations::discord

