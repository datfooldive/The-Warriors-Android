// Hooks for default.xbe. A strong definition replaces the recompiled function at that address, for example:
//
// #include "xdk/hle.h"
//
// X86_EXTERN_FUNC(__imp__sub_00012345);
// X86_FUNC(sub_00012345) {
//     __imp__sub_00012345(ctx, base);  // the original
// }
#include <atomic>
#include <chrono>
#include <iterator>
#include <thread>

#include "core/log.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

using namespace kernel;

// The MCPX audio processor's registers. XACT clocks itself from the global sample counter at 0xFE80200C
// (NV_PAPU_XGSCNT, read at 0x3D4D0D in its update, first from XACTEngineCreate's call at 0x6D309), which counts
// 48 kHz frames on the console. The host mixes the sound and has no APU, so the page is mapped here and the
// counter is kept running from the host clock.
namespace {
constexpr uint32_t kApuPage = 0xFE802000;
constexpr uint32_t kApuSampleCount = 0xFE80200C;

// The 3D voice code XACT calls into DSOUND directly (0x3AC3BE, from 0x3D30CE) goes through the HRTF routines at
// 0x3CD868. DirectSoundUseFullHRTF fills them (0x3AFC2B), but it is the host's and does nothing, so the table is
// written here as the library writes it: the routines are the library's own math and touch no device state.
constexpr uint32_t kHrtf = 0x3CD868;
constexpr uint32_t kFullHrtf[] = {0x3AF23B, 0x3AF284, 0x3AF3E7, 0x3AF47D, 0x3AF4C8,
                                  0x3AF5D9, 0x3AF669, 0x3AF738, 0x3AF7DA, 0x3AF853, 3};
}  // namespace

namespace dsound {
void SetSourceLevels(uint32_t obj, uint32_t desc);  // sdk/runtime/xdk/dsound.cpp
}

namespace game {
void SetUpAudio() {
    static bool started = false;
    if (started) return;
    started = true;
    for (uint32_t i = 0; i < std::size(kFullHrtf); i++) W32(kHrtf + i * 4, kFullHrtf[i]);
    mem::Commit(kApuPage, mem::kPage);
    std::thread([] {
        auto start = std::chrono::steady_clock::now();
        for (;;) {
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start);
            W32(kApuSampleCount, uint32_t(uint64_t(us.count()) * 48000 / 1000000));
            // diagnostics: the counter as XACT reads it, every 5 seconds
            static uint64_t logged = 0;
            if (uint64_t(us.count()) / 5000000 != logged) {
                logged = uint64_t(us.count()) / 5000000;
                LOGR("apu: sample counter %u at %.1f s", R32(kApuSampleCount), us.count() / 1e6);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }).detach();
}
}  // namespace game

namespace {
// a setting the host does not play: named once in the log, so a sound that is off can be traced to it
template <size_t N>
uint32_t UnplayedOnce(const char (&name)[N], std::atomic<bool>& seen) {
    if (!seen.exchange(true)) LOGR("dsound: %s not played", name);
    return 0;
}
}  // namespace
#define Unplayed(name) [] { static std::atomic<bool> seen; return UnplayedOnce(name, seen); }()

// IDirectSoundBuffer exports xbe_symbols leaves unnamed (named in config/game.toml). Each adjusts the interface to
// its CDirectSoundBuffer (this - 0x1C) and runs the library's code on it, but the buffers are the host's, so that
// code would read voice state that was never made. The host has no version of these: they do nothing.
//   0x3A9E14  SetLFO(this, desc), between SetPitch and SetEG
//   0x3AA00C  SetNotificationPositions(this, count, positions), after Lock
//   0x3AA064  (this, flag), from XACT at 0x3D34A8; 0x3AA048 below is taken
HLE_STDCALL(DSoundBuffer_SetLFO, 2) { return Unplayed("DSoundBuffer_SetLFO"); }
HLE_STDCALL(DSoundBuffer_3AA00C, 3) { return Unplayed("DSoundBuffer_3AA00C"); }
// 0x3AA048(this, desc) is the library's 3D parameter call (0x3A84BB): XACT sends each voice's I3DL2 levels through
// it, which the host mixer takes (dsound::SetSourceLevels). 0x3AA064(this, on) sets a voice flag the host has no use for
HLE_STDCALL(DSoundBuffer_3AA048, 2) {
    dsound::SetSourceLevels(a[0], a[1]);
    return 0;
}
HLE_STDCALL(DSoundBuffer_3AA064, 2) { return Unplayed("DSoundBuffer_3AA064"); }


// The rest of the voice settings with no host version, placed by the order the XDK header declares them in, which
// the named ones between them follow: the 3D settings of both, the stream's LFO, and three
// stream settings after SetMixBinVolumes as the buffer's three above. The host mixes without them.
HLE_STDCALL(DSoundBuffer_SetConeAngles, 4) { return Unplayed("DSoundBuffer_SetConeAngles"); }  // 0x3ABD16
HLE_STDCALL(DSoundBuffer_SetConeOrientation, 5) { return Unplayed("DSoundBuffer_SetConeOrientation"); }  // 0x3ABD3A
HLE_STDCALL(DSoundBuffer_SetConeOutsideVolume, 3) { return Unplayed("DSoundBuffer_SetConeOutsideVolume"); }  // 0x3ABD6F
HLE_STDCALL(DSoundBuffer_SetMode, 3) { return Unplayed("DSoundBuffer_SetMode"); }  // 0x3ABDD7
HLE_STDCALL(DSoundBuffer_SetDistanceFactor, 3) { return Unplayed("DSoundBuffer_SetDistanceFactor"); }  // 0x3ABE61
HLE_STDCALL(DSoundBuffer_SetDopplerFactor, 3) { return Unplayed("DSoundBuffer_SetDopplerFactor"); }  // 0x3ABE85
HLE_STDCALL(DSoundBuffer_SetRolloffFactor, 3) { return Unplayed("DSoundBuffer_SetRolloffFactor"); }  // 0x3ABEA9
HLE_STDCALL(DSoundStream_SetLFO, 2) { return Unplayed("DSoundStream_SetLFO"); }  // 0x3A995A
HLE_STDCALL(DSoundStream_3A9B46, 2) {  // 0x3A9B46: the stream's 3D parameter call, as 0x3AA048
    dsound::SetSourceLevels(a[0], a[1]);
    return 0;
}
HLE_STDCALL(DSoundStream_3A9B98, 2) { return Unplayed("DSoundStream_3A9B98"); }  // 0x3A9B98
HLE_STDCALL(DSoundStream_SetAllParameters, 3) { return Unplayed("DSoundStream_SetAllParameters"); }  // 0x3AB793
HLE_STDCALL(DSoundStream_SetConeAngles, 4) { return Unplayed("DSoundStream_SetConeAngles"); }  // 0x3AB7E9
HLE_STDCALL(DSoundStream_SetConeOrientation, 5) { return Unplayed("DSoundStream_SetConeOrientation"); }  // 0x3AB843
HLE_STDCALL(DSoundStream_SetConeOutsideVolume, 3) { return Unplayed("DSoundStream_SetConeOutsideVolume"); }  // 0x3AB8AE
HLE_STDCALL(DSoundStream_SetMode, 3) { return Unplayed("DSoundStream_SetMode"); }  // 0x3AB9B8
HLE_STDCALL(DSoundStream_SetDistanceFactor, 3) { return Unplayed("DSoundStream_SetDistanceFactor"); }  // 0x3ABAE4
HLE_STDCALL(DSoundStream_SetDopplerFactor, 3) { return Unplayed("DSoundStream_SetDopplerFactor"); }  // 0x3ABB3E
HLE_STDCALL(DSoundStream_SetRolloffFactor, 3) { return Unplayed("DSoundStream_SetRolloffFactor"); }  // 0x3ABB98
HLE_STDCALL(DSoundStream_SetRolloffCurve, 4) { return Unplayed("DSoundStream_SetRolloffCurve"); }  // 0x3ABBF2

// GetVoiceProperties(this, props) of a buffer (0x3AA02C) and a stream (0x3A9AF4), which XACT reads at 0x3D2E43 and
// 0x3D2E4E: the mix bin count, then the bin and volume pairs it walks (0x3D78A0). The host's voices have no bins to
// report there, so the count is zero; left unset XACT walked a count off its stack.
namespace {
uint32_t NoVoiceProperties(uint32_t props) {
    for (uint32_t i = 0; i < 4 + 8 * 8; i += 4) W32(props + i, 0);
    return 0;
}
}  // namespace
HLE_STDCALL(DSoundBuffer_GetVoiceProperties, 2) { return NoVoiceProperties(a[1]); }
HLE_STDCALL(DSoundStream_GetVoiceProperties, 2) { return NoVoiceProperties(a[1]); }
