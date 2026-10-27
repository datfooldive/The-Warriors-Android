// The title's own cheat codes, played into port 1 as if pressed on the pad: the settings panel's CHEATS tab picks
// one, and the game takes it as it does from a controller. The codes are the game's table at 0x458A40 (27 of six
// PS2 buttons each), pressed as the Xbox buttons the title turns into those (0x6FEB1, README: A Cross, B Circle,
// X Square, Y Triangle, Black R1, White L3, LT L2, RT L1, left stick click R2, BACK Select).
//
// The checker, sub_0007E890, runs each frame of play: past a player who is on screen and not held (a pause, a
// cutscene), it adds each new press to the last six and compares them with the table. A match goes to the
// mission script's cheat callback (0x4B1C28, SetCheatCallback), which applies it; with none set the code does
// nothing. The tab's codes take a tap only while the checker gets that far and a callback is set.
#include <jni.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <mutex>

#include "core/input.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace game {

namespace {

constexpr uint32_t kCheatTable = 0x458A40;  // six uint32 PS2 buttons a code, zero after the last
constexpr uint32_t kCheatStride = 0x18;
constexpr int kSteps = 6;
constexpr uint32_t kPlayer = 0x5B9F78;          // the player the checker reads, 0 before one exists
constexpr uint32_t kPlayerOnScreen = 0x220;     // int16, above 0 while one is in play
constexpr uint32_t kPlayerHeld = 0x3FC;         // byte, set while play is held (a pause, a cutscene)
constexpr uint32_t kCheatCallback = 0x4B1C28;   // the mission script's, 0 while none takes cheats

// the panel's rows: the table's index and what the code does; the codes no list names are left out
struct Cheat {
    int index;
    const char* name;
};
constexpr Cheat kCheats[] = {
    {1, "INFINITE HEALTH"},
    {5, "INFINITE RAGE"},
    {2, "INFINITE SPRINT"},
    {0, "$200, FLASH AND SPRAY CANS"},
    {3, "LOSE THE POLICE"},
    {16, "HANDCUFF KEYS"},
    {18, "BREAK FREE OF HANDCUFFS"},
    {22, "FLASH UPGRADE"},
    {24, "HOBO ALLIANCE"},
    {14, "BRASS KNUCKLES"},
    {6, "KNIFE"},
    {7, "BAT"},
    {11, "UNBREAKABLE BAT"},
    {8, "PIPE"},
    {9, "MACHETE"},
    {12, "COMPLETE CURRENT MISSION"},
    {13, "100% COMPLETION"},
    {26, "ARMIES OF THE NIGHT: 99 CREDITS"},
};

// a PS2 button of the table as the Xbox press the title reads it from
enum Button : uint16_t { kUp = 1 << 0, kDown = 1 << 1, kLeft = 1 << 2, kRight = 1 << 3, kStart = 1 << 4, kBack = 1 << 5,
                         kLeftThumb = 1 << 6, kRightThumb = 1 << 7 };
enum Analog : int { kA, kB, kX, kY, kBlack, kWhite, kLeftTrigger, kRightTrigger };
struct Press {
    uint16_t buttons;
    int analog;  // -1 none
};
Press XboxPress(uint32_t ps2) {
    switch (ps2) {
        case 0x0100: return {kBack, -1};         // Select
        case 0x0200: return {0, kWhite};         // L3
        case 0x0400: return {kRightThumb, -1};   // R3
        case 0x0800: return {kStart, -1};
        case 0x1000: return {kUp, -1};
        case 0x2000: return {kRight, -1};
        case 0x4000: return {kDown, -1};
        case 0x8000: return {kLeft, -1};
        case 0x0001: return {0, kLeftTrigger};   // L2
        case 0x0002: return {kLeftThumb, -1};    // R2
        case 0x0004: return {0, kRightTrigger};  // L1
        case 0x0008: return {0, kBlack};         // R1
        case 0x0010: return {0, kY};             // Triangle
        case 0x0020: return {0, kB};             // Circle
        case 0x0040: return {0, kA};             // Cross
        case 0x0080: return {0, kX};             // Square
        default: return {0, -1};
    }
}

// each button held for a few of the title's polls and let go as long, after a pause for the panel to close
constexpr double kDelay = 0.4, kHold = 0.12, kGap = 0.12;
// the checker ran past its guards this recently: play is on
constexpr auto kAvailableFor = std::chrono::milliseconds(500);

using Clock = std::chrono::steady_clock;
std::mutex g_lock;
int g_playing = -1;  // the table's index
Clock::time_point g_start;
std::atomic<int64_t> g_checked{0};  // when the checker last could take a code, steady clock ticks

}  // namespace

// GameInfo::pad: the cheat being played, over whatever else holds port 1
void Cheats(input::Pad& p) {
    std::lock_guard lock(g_lock);
    if (g_playing < 0) return;
    double t = std::chrono::duration<double>(Clock::now() - g_start).count() - kDelay;
    if (t < 0) return;
    int step = int(t / (kHold + kGap));
    if (step >= kSteps) {
        g_playing = -1;
        return;
    }
    if (t - step * (kHold + kGap) >= kHold) return;
    Press press = XboxPress(kernel::R32(kCheatTable + uint32_t(g_playing) * kCheatStride + uint32_t(step) * 4));
    p.connected = true;
    p.buttons |= press.buttons;
    if (press.analog >= 0) p.analog[press.analog] = 0xFF;
}

}  // namespace game

// the checker's own guards, read before it runs: a code entered now reaches a script that applies it
X86_EXTERN_FUNC(__imp__sub_0007E890);
X86_FUNC(sub_0007E890) {
    uint32_t player = kernel::R32(game::kPlayer);
    if (player && int16_t(kernel::R16(player + game::kPlayerOnScreen)) > 0 &&
        !kernel::G<uint8_t>(player + game::kPlayerHeld) && kernel::R32(game::kCheatCallback)) {
        game::g_checked = game::Clock::now().time_since_epoch().count();
    }
    __imp__sub_0007E890(ctx, base);
}

// the CHEATS tab's rows, by name
extern "C" JNIEXPORT jobjectArray JNICALL Java_com_datfooldive_warriors_WarriorsActivity_nativeCheats(JNIEnv* env, jclass) {
    jclass string = env->FindClass("java/lang/String");
    jobjectArray names = env->NewObjectArray(jsize(std::size(game::kCheats)), string, nullptr);
    for (size_t i = 0; i < std::size(game::kCheats); i++) {
        jstring s = env->NewStringUTF(game::kCheats[i].name);
        env->SetObjectArrayElement(names, jsize(i), s);
        env->DeleteLocalRef(s);
    }
    return names;
}

// whether a code entered now would take: the panel's codes take a tap only then
extern "C" JNIEXPORT jboolean JNICALL Java_com_datfooldive_warriors_WarriorsActivity_nativeCheatsAvailable(JNIEnv*, jclass) {
    auto checked = game::Clock::time_point(game::Clock::duration(game::g_checked.load()));
    return game::g_checked.load() && game::Clock::now() - checked < game::kAvailableFor;
}

extern "C" JNIEXPORT void JNICALL Java_com_datfooldive_warriors_WarriorsActivity_nativeCheat(JNIEnv*, jclass, jint row) {
    if (row < 0 || size_t(row) >= std::size(game::kCheats)) return;
    std::lock_guard lock(game::g_lock);
    game::g_playing = game::kCheats[row].index;
    game::g_start = game::Clock::now();
}
