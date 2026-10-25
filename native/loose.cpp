// A controller Android hands over without its device: some phones' game services take a DualShock and pass its
// buttons and sticks on as a virtual device, which SDL takes for no controller. Its input then never reaches the game,
// and the on-screen controls never give way to it. WarriorsActivity sends those events here as Android's key codes and
// axes; they play port 0, over whatever SDL read.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <mutex>

#include <jni.h>

#include "core/input.h"
#include "loose.h"

namespace {

struct Loose {
    uint16_t buttons = 0;
    uint8_t analog[8] = {};
    float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0, hx = 0, hy = 0;
    bool seen = false;
};

std::mutex g_lock;
Loose g_loose;

// Android's key code as the pad's: a button bit (dpad, start, back, thumbs) or an analog button, -1 for neither
void LooseKey(int code, int& bit, int& analog) {
    bit = analog = -1;
    switch (code) {
        case 19: bit = 0; break;    // DPAD_UP
        case 20: bit = 1; break;    // DPAD_DOWN
        case 21: bit = 2; break;    // DPAD_LEFT
        case 22: bit = 3; break;    // DPAD_RIGHT
        case 108: bit = 4; break;   // BUTTON_START (Options)
        case 109: bit = 5; break;   // BUTTON_SELECT (Share)
        case 106: bit = 6; break;   // BUTTON_THUMBL
        case 107: bit = 7; break;   // BUTTON_THUMBR
        case 96: analog = 0; break;   // BUTTON_A (cross)
        case 97: analog = 1; break;   // BUTTON_B (circle)
        case 99: analog = 2; break;   // BUTTON_X (square)
        case 100: analog = 3; break;  // BUTTON_Y (triangle)
        case 103: analog = 4; break;  // BUTTON_R1: black
        case 102: analog = 5; break;  // BUTTON_L1: white
        case 104: analog = 6; break;  // BUTTON_L2
        case 105: analog = 7; break;  // BUTTON_R2
        default: break;
    }
}

int16_t Axis(float v) { return int16_t(std::clamp(v, -1.0f, 1.0f) * 32767.0f); }
uint8_t Trigger(float v) { return uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f); }

}  // namespace

namespace loose {

bool Connected() {
    std::lock_guard lock(g_lock);
    return g_loose.seen;
}

// the loose controller's state on top of what SDL read: buttons added, the stick pushed further taken
void Merge(input::Pad& p) {
    std::lock_guard lock(g_lock);
    const Loose& l = g_loose;
    if (!l.seen) return;
    p.connected = true;
    p.buttons |= l.buttons;
    // the hat as the dpad: x left -1 right 1, y up -1 down 1
    if (l.hy < -0.5f) p.buttons |= 1 << 0;
    if (l.hy > 0.5f) p.buttons |= 1 << 1;
    if (l.hx < -0.5f) p.buttons |= 1 << 2;
    if (l.hx > 0.5f) p.buttons |= 1 << 3;
    for (int i = 0; i < 8; i++) p.analog[i] = std::max(p.analog[i], l.analog[i]);
    p.analog[6] = std::max(p.analog[6], Trigger(l.lt));
    p.analog[7] = std::max(p.analog[7], Trigger(l.rt));
    // Android's y is down, the pad's up
    auto further = [](int16_t& to, int16_t v) {
        if (std::abs(int(v)) > std::abs(int(to))) to = v;
    };
    further(p.lx, Axis(l.lx));
    further(p.ly, Axis(-l.ly));
    further(p.rx, Axis(l.rx));
    further(p.ry, Axis(-l.ry));
}

void Release() {
    std::lock_guard lock(g_lock);
    bool seen = g_loose.seen;
    g_loose = {};
    g_loose.seen = seen;
}

}  // namespace loose

// WarriorsActivity: the loose controller's buttons, sticks and triggers, and its letting go when the app loses the focus
extern "C" JNIEXPORT void JNICALL Java_com_datfooldive_warriors_WarriorsActivity_nativeLooseKey(JNIEnv*, jclass, jint code,
                                                                                     jboolean down) {
    int bit, analog;
    LooseKey(code, bit, analog);
    if (bit < 0 && analog < 0) return;
    std::lock_guard lock(g_lock);
    g_loose.seen = true;
    if (bit >= 0) g_loose.buttons = uint16_t(down ? g_loose.buttons | (1u << bit) : g_loose.buttons & ~(1u << bit));
    if (analog >= 0) g_loose.analog[analog] = down ? 0xFF : 0;
}

extern "C" JNIEXPORT void JNICALL Java_com_datfooldive_warriors_WarriorsActivity_nativeLooseAxes(JNIEnv*, jclass, jfloat lx,
                                                                                      jfloat ly, jfloat rx, jfloat ry,
                                                                                      jfloat lt, jfloat rt, jfloat hx,
                                                                                      jfloat hy) {
    std::lock_guard lock(g_lock);
    g_loose.seen = true;
    g_loose.lx = lx, g_loose.ly = ly, g_loose.rx = rx, g_loose.ry = ry;
    g_loose.lt = lt, g_loose.rt = rt, g_loose.hx = hx, g_loose.hy = hy;
}

extern "C" JNIEXPORT void JNICALL Java_com_datfooldive_warriors_WarriorsActivity_nativeLooseRelease(JNIEnv*, jclass) {
    loose::Release();
}
