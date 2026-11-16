// =============================================================================
// stubs.cpp - Xbox 360 API stubs for GH:WoR PC recompilation
// =============================================================================

#include <algorithm>
#include <rex/cvar.h>
#include "diagnostics/stubs.hpp"
#include "diagnostics/stub_tracker.hpp"
#include "input/input_manager.hpp"

#include <rex/ppc.h>
#include <rex/memory/utils.h>
#include <cstring>

#ifndef PPC_STUB
#define PPC_STUB(subroutine) REX_STUB(subroutine)
#endif

#ifndef PPC_FUNC
#define PPC_FUNC(subroutine) REX_FUNC(subroutine)
#endif

#ifdef REWOR_DEV
#define PPC_STUB_TRACKED(subroutine) \
  extern "C" REX_FUNC(subroutine) { \
    static auto* s_counter = re_wor::diagnostics::StubTracker::instance().register_stub(#subroutine); \
    s_counter->fetch_add(1, std::memory_order_relaxed); \
    ctx.r3.u64 = 0; \
  }
#else
#define PPC_STUB_TRACKED(subroutine) PPC_STUB(subroutine)
#endif

// --- XCustom (Guitar Hero dashboard hooks) ---
PPC_STUB_TRACKED(__imp__XCustomGetCurrentGamercard);
PPC_STUB_TRACKED(__imp__XCustomSetDynamicActions);
PPC_STUB_TRACKED(__imp__XCustomGetLastActionPressEx);

// --- XamInput ---
PPC_STUB_TRACKED(__imp__XamInputSendStayAliveRequest);

// --- XamAvatar ---
PPC_STUB_TRACKED(__imp__XamAvatarGetManifestLocalUser);
PPC_STUB_TRACKED(__imp__XamAvatarGetMetadataRandom);
PPC_STUB_TRACKED(__imp__XamAvatarManifestGetBodyType);
PPC_STUB_TRACKED(__imp__XamAvatarGetAssetsResultSize);
PPC_STUB_TRACKED(__imp__XamAvatarGetAssets);
PPC_STUB_TRACKED(__imp__XamAvatarLoadAnimation);

// --- XamShow (UI overlays) ---
PPC_STUB_TRACKED(__imp__XamShowFriendsUI);
PPC_STUB_TRACKED(__imp__XamShowPlayersUI);
PPC_STUB_TRACKED(__imp__XamShowGamerCardUIForXUID);
PPC_STUB_TRACKED(__imp__XamShowPlayerReviewUI);
PPC_STUB_TRACKED(__imp__XamShowMarketplaceUI);
PPC_STUB_TRACKED(__imp__XamShowMarketplaceDownloadItemsUI);
PPC_STUB_TRACKED(__imp__XamShowMessageComposeUI);

// --- XamUser ---
PPC_STUB_TRACKED(__imp__XamUserCreateStatsEnumerator);
PPC_STUB_TRACKED(__imp__XamUserGetMembershipTierFromXUID);
PPC_STUB_TRACKED(__imp__XamUserGetOnlineCountryFromXUID);

// --- XamBackground (DLC downloads) ---
PPC_STUB_TRACKED(__imp__XamBackgroundDownloadGetMode);
PPC_STUB_TRACKED(__imp__XamBackgroundDownloadSetMode);
PPC_STUB_TRACKED(__imp__XamBackgroundDownloadItemGetStatus);
PPC_STUB_TRACKED(__imp__XamBackgroundDownloadItemGetHistoryStatus);

// --- XNet ---
PPC_STUB_TRACKED(__imp__XNetLogonGetTitleID);

// --- NetDll ---
PPC_STUB_TRACKED(__imp__NetDll_getsockopt);

// --- XAudio ---
PPC_STUB_TRACKED(__imp__XAudioOverrideSpeakerConfig);

// --- Kernel I/O (xboxkrnl) ---
PPC_STUB_TRACKED(__imp__IoCheckShareAccess);
PPC_STUB_TRACKED(__imp__IoCompleteRequest);
PPC_STUB_TRACKED(__imp__IoDeleteDevice);
PPC_STUB_TRACKED(__imp__IoDismountVolume);
PPC_STUB_TRACKED(__imp__IoInvalidDeviceRequest);

// --- Kernel Object Manager ---
PPC_STUB_TRACKED(__imp__ObReferenceObject);
PPC_STUB_TRACKED(__imp__ObIsTitleObject);

// --- Kernel Runtime ---
PPC_STUB_TRACKED(__imp__RtlUpcaseUnicodeChar);

// --- LDI (Cabinet decompression) ---
PPC_STUB_TRACKED(__imp__LDICreateDecompression);
PPC_STUB_TRACKED(__imp__LDIDecompress);
PPC_STUB_TRACKED(__imp__LDIDestroyDecompression);
PPC_STUB_TRACKED(__imp__LDIResetDecompression);

// --- Additional Kernel/Driver APIs ---
PPC_STUB_TRACKED(__imp__IoRemoveShareAccess);
PPC_STUB_TRACKED(__imp__IoSetShareAccess);
PPC_STUB_TRACKED(__imp__ExAllocatePoolWithTag);

// --- Microphone/Peripheral device ---
PPC_STUB_TRACKED(__imp__MicDeviceRequest);
PPC_STUB_TRACKED(__imp__RmcDeviceRequest);
PPC_STUB_TRACKED(__imp__CurlOpenTitleBackingFile);

// --- XamInput (additional) ---
PPC_STUB_TRACKED(__imp__XamInputControl);
PPC_STUB_TRACKED(__imp__XamInputRawState);

// --- XamVoice ---
// XamVoiceSubmitPacket y el resto de XamVoice: src/input/voice.cpp (microfono)

// --- NetDll (additional networking) ---
PPC_STUB_TRACKED(__imp__NetDll_XNetConnect);
PPC_STUB_TRACKED(__imp__NetDll_XNetGetConnectStatus);
PPC_STUB_TRACKED(__imp__NetDll_XNetQosLookup);
PPC_STUB_TRACKED(__imp__NetDll_XNetServerToInAddr);
PPC_STUB_TRACKED(__imp__NetDll_XNetTsAddrToInAddr);
PPC_STUB_TRACKED(__imp__NetDll_XNetUnregisterInAddr);
PPC_STUB_TRACKED(__imp__NetDll_XNetUnregisterKey);

// --- XeCrypt (Xbox cryptography) ---
PPC_STUB_TRACKED(__imp__XeCryptMd5Init);
PPC_STUB_TRACKED(__imp__XeCryptMd5Update);
PPC_STUB_TRACKED(__imp__XeCryptMd5Final);

extern "C" PPC_FUNC(__imp__XNotifyCreateListener) {
#ifdef REWOR_DEV
  re_wor::diagnostics::StubTracker::instance().record_unmapped(0, 0);
#endif
  ctx.r3.u64 = 0xDEAD0002;
}

extern "C" REX_FUNC(__imp__XGetVideoMode) {
#ifdef REWOR_DEV
  re_wor::diagnostics::StubTracker::instance().record_unmapped(0, 0);
#endif
  uint32_t mode_addr = static_cast<uint32_t>(ctx.r3.u32);
  if (mode_addr == 0 || base == nullptr) return;

  uint8_t* dest = base + mode_addr;
  std::memset(dest, 0, 48);

  // Igual que el release de re-wor: el motor fue disenado para 720p.
  rex::memory::store_and_swap<uint32_t>(base + mode_addr + 0x00, 1280);
  rex::memory::store_and_swap<uint32_t>(base + mode_addr + 0x04, 720);
  rex::memory::store_and_swap<uint32_t>(base + mode_addr + 0x08, 0);     // Progressive
  rex::memory::store_and_swap<uint32_t>(base + mode_addr + 0x0C, 1);     // Widescreen
  rex::memory::store_and_swap<uint32_t>(base + mode_addr + 0x10, 1);     // HiDef

  // RefreshRate = 60.0f, como la consola. El motor cuenta el tiempo de parte del juego en VBlanks de 1/60 s;
  // los FPS altos (Options > Graphics > Frame Rate) no pasan por aqui: ver gfx_options.cpp (FrameCapHz).
  rex::memory::store_and_swap<uint32_t>(base + mode_addr + 0x14, 0x42700000u);

  rex::memory::store_and_swap<uint32_t>(base + mode_addr + 0x18, 1);     // NTSC
}

namespace re_wor::diagnostics {

void register_system_stubs() {
}

void initialize_diagnostics() {
#ifdef REWOR_DEV
  StubTracker::instance().start_reporter(5);
#endif
}

}  // namespace re_wor::diagnostics

