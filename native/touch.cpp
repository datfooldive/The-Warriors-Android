// The on-screen controls, laid out as the original Xbox pad's: the left stick walks, the four face buttons and White
// and Black are the pad's, each drawn as what it does in the game with the pad's own letter or colour on it, and a
// finger held on the right is the right stick. Touches come from
// SDL's finger events in window coordinates; what they leave is overlaid on the pad the keyboard left
// (src/common/keyboard.cpp), so the game reads one report as on the console.
#include <SDL3/SDL.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

#include <imgui.h>

#include "app/app.h"
#include "common/options.h"
#include "core/input.h"
#include "core/log.h"
#include "host.h"
#include "loose.h"
#include "ui_icons.h"
#include "ui_paths.h"
#include "vk_ui.h"

namespace touch {

namespace {

// what a finger is doing; the gear is past the pad's buttons
enum Role : int { kNone = -1, kStick = 0, kCamera = 1, kButton = 2 };
// the pad's analog buttons, in input::Pad::analog order
enum Analog : int { kA = 0, kB = 1, kX = 2, kY = 3, kBlack = 4, kWhite = 5, kLeftTrigger = 6, kRightTrigger = 7 };
enum Bit : uint16_t { kUp = 1 << 0, kDown = 1 << 1, kLeft = 1 << 2, kRight = 1 << 3, kStart = 1 << 4, kBack = 1 << 5,
                       kLeftThumb = 1 << 6, kRightThumb = 1 << 7 };
// the on-screen buttons, in the order the layout and the art have them
enum Button : int { kBtnA = 0, kBtnB = 1, kBtnX = 2, kBtnY = 3, kBtnStart = 4, kBtnBack = 5, kBtnDpad = 6,
                     kBtnLT = 7, kBtnRT = 8, kBtnL3 = 9, kBtnR3 = 10, kBtnWhite = 11, kBtnBlack = 12, kBtnCount = 13 };

std::atomic<float> g_lx{0}, g_ly{0};  // the stick, -1 to 1, y up
std::atomic<float> g_rx{0}, g_ry{0};  // the right stick, -1 to 1, y up
std::atomic<bool> g_cam{false};       // a finger is on the right stick: where it went down, for the ring drawn there
std::atomic<float> g_cam_x{0}, g_cam_y{0};
std::atomic<uint64_t> g_analog{0};    // one byte per analog button
std::atomic<uint32_t> g_buttons{0};   // Bit
std::atomic<bool> g_pad{false};       // a controller is plugged in: the screen's controls are hidden while one is

struct Touch {
    SDL_FingerID id;
    int role;
    float x, y;    // window pixels
    float ox, oy;  // the right stick's middle: where the finger went down, dragged along past the stick's reach
};
std::mutex g_lock;
std::vector<Touch> g_touches;

// what the fingers held lets go: the panel opened, or a controller took over
void ReleaseAll() {  // g_lock held
    g_touches.clear();
    g_analog = 0;
    g_buttons = 0;
    g_lx = g_ly = g_rx = g_ry = 0;
    g_cam = false;
}

struct Layout {
    float w, h, s;
    float stick[2], stick_r;
    float rstick[2];         // the fixed right stick's middle, the left stick's size
    float btn[4][2], btn_r;  // A B X Y, a diamond as the pad has them: Y up, X left, B right, A down
    float cam_r;             // the right stick's reach
    float dpad[2];           // the arrows' middle
    float arrow[4][2], arrow_r;  // up, down, left, right: apart, each a button of its own
    float pill[kBtnCount][2];  // START BACK LT RT, by their button
    float pill_w, pill_h;
    float gear[2], gear_r;  // the settings panel's button, between BACK and START
};
constexpr int kGear = kButton + kBtnCount;
constexpr int kArrow = kGear + 1;  // the four arrows after it, up down left right
constexpr uint32_t kArrowBits[4] = {kUp, kDown, kLeft, kRight};

// where each control sits: its centre as a fraction of the screen, in this order. These are the pad's own layout,
// what a fresh install gets and what the editor's RESET puts back.
enum Unit : int {
    kUnitStick, kUnitA, kUnitB, kUnitX, kUnitY, kUnitDpad, kUnitStart, kUnitBack, kUnitLT, kUnitRT, kUnitL3, kUnitR3,
    kUnitGear, kUnitWhite, kUnitBlack, kUnitRStick, kUnitCount  // the later ones last, so an older saved layout keeps its places
};
struct Point {
    float x, y;
};
constexpr Point kDefaultUnits[kUnitCount] = {
    {0.130f, 0.760f},  // the stick
    {0.855f, 0.820f},  // A, down
    {0.909f, 0.700f},  // B, right
    {0.801f, 0.700f},  // X, left
    {0.855f, 0.580f},  // Y, up
    {0.130f, 0.393f},  // the arrows' middle
    {0.580f, 0.090f},  // START
    {0.420f, 0.090f},  // BACK
    {0.090f, 0.090f},  // LT
    {0.910f, 0.090f},  // RT
    {0.235f, 0.920f},  // L3
    {0.700f, 0.920f},  // R3
    {0.500f, 0.090f},  // the gear
    {0.812f, 0.450f},  // White, LOOK: the pad's small pair, over the diamond's top corners
    {0.898f, 0.450f},  // Black, GUARD
    {0.640f, 0.700f},  // the fixed right stick, between the stick clicks and the diamond
};
std::array<Point, kUnitCount> g_units = [] {
    std::array<Point, kUnitCount> a{};
    std::copy(std::begin(kDefaultUnits), std::end(kDefaultUnits), a.begin());
    return a;
}();
// the pills, in the order the editor numbers them
constexpr int kPillCount = 8;
constexpr int kPillButton[kPillCount] = {kBtnStart, kBtnBack, kBtnLT, kBtnRT, kBtnL3, kBtnR3, kBtnWhite, kBtnBlack};
constexpr int kPillUnit[kPillCount] = {kUnitStart, kUnitBack, kUnitLT, kUnitRT, kUnitL3, kUnitR3, kUnitWhite, kUnitBlack};

std::atomic<bool> g_editing{false};
int g_drag = -1;            // the unit a finger is moving
SDL_FingerID g_drag_id = 0;
float g_grab[2] = {0, 0};   // where the finger took it, from its centre

// The controls' art (native/ui): what each button does in the game as a line icon, drawn as vectors at the gear's
// weight (ui_paths.h, tools/ui_paths.py), and the pad's letters and button names as labels, white coverage the draw
// tints (tools/ui_icons.sh), made into textures the first frame they are drawn.
enum Art : int {
    kArtLetterA, kArtLetterB, kArtLetterX, kArtLetterY,
    kArtLT, kArtRT, kArtL3, kArtR3, kArtStart, kArtBack, kArtCount, kArtNone = -1
};
struct ArtImage {
    ImTextureID tex = ImTextureID();
    float u0 = 0, u1 = 1;  // the columns the glyph covers: a label is drawn at its own width, centred
    float aspect = 1;      // the covered part's width over its height
};
ArtImage g_art[kArtCount];
bool g_art_made = false;

void EnsureArt() {
    if (g_art_made) return;
    g_art_made = true;  // one attempt: art that will not be made is not tried again every frame
    using namespace ui_icons;
    const struct {
        const char* bytes;
        int w, h;
    } kSources[kArtCount] = {
        {letter_a, kSize, kSize}, {letter_b, kSize, kSize}, {letter_x, kSize, kSize},
        {letter_y, kSize, kSize}, {label_lt, kLabelW, kLabelH}, {label_rt, kLabelW, kLabelH},
        {label_l3, kLabelW, kLabelH}, {label_r3, kLabelW, kLabelH}, {label_start, kLabelW, kLabelH},
        {label_back, kLabelW, kLabelH},
    };
    std::vector<uint8_t> rgba;
    for (int i = 0; i < kArtCount; i++) {
        const auto& src = kSources[i];
        const uint8_t* a = reinterpret_cast<const uint8_t*>(src.bytes);
        rgba.assign(size_t(src.w) * src.h * 4, 255);
        int first = src.w, last = -1;
        for (int y = 0; y < src.h; y++)
            for (int x = 0; x < src.w; x++) {
                uint8_t c = a[size_t(y) * src.w + x];
                rgba[(size_t(y) * src.w + x) * 4 + 3] = c;
                if (c) first = std::min(first, x), last = std::max(last, x);
            }
        ArtImage& img = g_art[i];
        img.tex = vkui::Texture(rgba.data(), uint32_t(src.w), uint32_t(src.h));
        // a label keeps only its lettering's columns; a letter its whole square, so all of them line up
        if (src.w != src.h && last >= first) {
            img.u0 = float(first) / float(src.w);
            img.u1 = float(last + 1) / float(src.w);
        }
        img.aspect = (img.u1 - img.u0) * float(src.w) / float(src.h);
    }
}

// art as wide as its aspect at height h, centred on c, in col
void DrawArt(ImDrawList* dl, int art, ImVec2 c, float h, ImU32 col) {
    if (art < 0 || !g_art[art].tex) return;
    const ArtImage& img = g_art[art];
    float w = h * img.aspect;
    dl->AddImage(img.tex, ImVec2(c.x - w / 2, c.y - h / 2), ImVec2(c.x + w / 2, c.y + h / 2), ImVec2(img.u0, 0),
                 ImVec2(img.u1, 1), col);
}

// A line icon (ui_paths.h) drawn into a texture of the very size it is shown at, its lines size / 24 thick as the gear's
// are. Each pixel's coverage comes from its distance to the nearest line, so where the lines meet they join as one
// stroke with round ends and corners: drawn as separate translucent lines, every join came out darker and thicker.
// Made once for each size the controls take, kept by size in steps of 4 px, so dragging the panel's button size makes a
// few dozen at most.
struct IconTexture {
    const float* icon = nullptr;
    int size = 0;
    ImTextureID tex = ImTextureID();
};
std::vector<IconTexture> g_icon_tex;

ImTextureID RasterIcon(const float* d, int size) {
    struct Segment {
        float ax, ay, bx, by;
    };
    struct Disc {
        float x, y, r;
    };
    const float k = float(size) / 24.0f, half = std::max(float(size) * 0.042f, 1.5f) / 2;
    std::vector<Segment> lines;
    std::vector<Disc> discs;
    for (;;) {
        int kind = int(*d++);
        if (kind < 0) break;
        if (kind >= 2) {
            float x = d[0] * k, y = d[1] * k, r = d[2] * k;
            if (kind == 3) {
                discs.push_back({x, y, r});
            } else {
                constexpr int kSides = 48;
                for (int i = 0; i < kSides; i++) {
                    float a0 = 6.2831853f * float(i) / kSides, a1 = 6.2831853f * float(i + 1) / kSides;
                    lines.push_back({x + r * std::cos(a0), y + r * std::sin(a0), x + r * std::cos(a1),
                                     y + r * std::sin(a1)});
                }
            }
            d += 3;
            continue;
        }
        int n = int(*d++);
        for (int i = 0; i + 1 < n; i++)
            lines.push_back({d[i * 2] * k, d[i * 2 + 1] * k, d[i * 2 + 2] * k, d[i * 2 + 3] * k});
        if (kind == 1 && n > 2) lines.push_back({d[n * 2 - 2] * k, d[n * 2 - 1] * k, d[0] * k, d[1] * k});
        d += n * 2;
    }
    std::vector<uint8_t> rgba(size_t(size) * size * 4, 255);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float px = float(x) + 0.5f, py = float(y) + 0.5f, best = 1e9f;
            for (const Segment& s : lines) {
                float vx = s.bx - s.ax, vy = s.by - s.ay, wx = px - s.ax, wy = py - s.ay;
                float len = vx * vx + vy * vy;
                float t = len > 0 ? std::clamp((wx * vx + wy * vy) / len, 0.0f, 1.0f) : 0.0f;
                float dx = wx - vx * t, dy = wy - vy * t;
                best = std::min(best, dx * dx + dy * dy);
            }
            float cover = std::clamp(half + 0.5f - std::sqrt(best), 0.0f, 1.0f);
            for (const Disc& c : discs)
                cover = std::max(cover, std::clamp(c.r + 0.5f - std::hypot(px - c.x, py - c.y), 0.0f, 1.0f));
            rgba[(size_t(y) * size + x) * 4 + 3] = uint8_t(std::lround(cover * 255.0f));
        }
    return vkui::Texture(rgba.data(), uint32_t(size), uint32_t(size));
}

// a line icon size across, centred on c, in col, on whole pixels so its texture is drawn one to one
void DrawIcon(ImDrawList* dl, const float* d, ImVec2 c, float size, ImU32 col) {
    if (!d) return;
    int px = std::max(int(std::lround(size / 4)) * 4, 8);
    IconTexture* it = nullptr;
    for (IconTexture& t : g_icon_tex)
        if (t.icon == d && t.size == px) it = &t;
    if (!it) it = &g_icon_tex.emplace_back(IconTexture{d, px, RasterIcon(d, px)});
    if (!it->tex) return;
    ImVec2 a(std::floor(c.x - float(px) / 2), std::floor(c.y - float(px) / 2));
    dl->AddImage(it->tex, a, ImVec2(a.x + float(px), a.y + float(px)), ImVec2(0, 0), ImVec2(1, 1), col);
}

// the on-screen right stick's way of working, the panel's RIGHT STICK
enum CameraMode : uint32_t { kFloating = 0, kFixed = 1, kSwipe = 2 };
CameraMode Mode() { return CameraMode(std::min<uint32_t>(game::Opt().camera_mode, kSwipe)); }
float Sensitivity() { return std::clamp(game::Opt().camera_sensitivity / 100.0f, 0.5f, 3.0f); }

Layout Make(float w, float h) {
    Layout l{};
    l.w = w;
    l.h = h;
    l.s = std::min(w, h);
    // the panel's size setting scales the controls, not where they sit
    const float k = std::clamp(game::Opt().controls_scale / 100.0f, 0.5f, 2.0f);
    l.stick_r = l.s * 0.11f * k;
    l.btn_r = l.s * 0.075f * k;
    l.arrow_r = l.s * 0.057f * k;
    l.pill_w = l.s * 0.165f * k;
    l.pill_h = l.s * 0.075f * k;
    l.gear_r = l.s * 0.0322f * k;
    // the right stick's reach: the more sensitive, the shorter the drag to a full push
    l.cam_r = l.s * 0.10f * k / Sensitivity();
    auto place = [&](int u, float out[2]) {
        out[0] = g_units[size_t(u)].x * w;
        out[1] = g_units[size_t(u)].y * h;
    };
    place(kUnitStick, l.stick);
    place(kUnitA, l.btn[kBtnA]);
    place(kUnitB, l.btn[kBtnB]);
    place(kUnitX, l.btn[kBtnX]);
    place(kUnitY, l.btn[kBtnY]);
    place(kUnitDpad, l.dpad);
    place(kUnitStart, l.pill[kBtnStart]);
    place(kUnitBack, l.pill[kBtnBack]);
    place(kUnitLT, l.pill[kBtnLT]);
    place(kUnitRT, l.pill[kBtnRT]);
    place(kUnitL3, l.pill[kBtnL3]);
    place(kUnitR3, l.pill[kBtnR3]);
    place(kUnitGear, l.gear);
    place(kUnitWhite, l.pill[kBtnWhite]);
    place(kUnitBlack, l.pill[kBtnBlack]);
    place(kUnitRStick, l.rstick);
    // the arrows keep their ring around the d-pad's middle
    const float off = l.s * 0.100f * k;
    const float dirs[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    for (int a = 0; a < 4; a++) {
        l.arrow[a][0] = l.dpad[0] + dirs[a][0] * off;
        l.arrow[a][1] = l.dpad[1] + dirs[a][1] * off;
    }
    return l;
}

bool InCircle(float x, float y, const float c[2], float r) {
    return (x - c[0]) * (x - c[0]) + (y - c[1]) * (y - c[1]) <= r * r;
}

bool InPill(const Layout& l, float x, float y, const float c[2]) {
    return std::fabs(x - c[0]) <= l.pill_w / 2 && std::fabs(y - c[1]) <= l.pill_h / 2;
}

// the control under a finger, for the editor: -1 when none is
int HitUnit(const Layout& l, float x, float y) {
    if (InCircle(x, y, l.gear, l.gear_r * 1.4f)) return kUnitGear;
    for (int i = 0; i < kPillCount; i++)
        if (InPill(l, x, y, l.pill[kPillButton[i]])) return kPillUnit[i];
    for (int b = 0; b < 4; b++)
        if (InCircle(x, y, l.btn[b], l.btn_r)) return kUnitA + b;
    if (InCircle(x, y, l.dpad, l.arrow_r * 2.0f)) return kUnitDpad;
    if (InCircle(x, y, l.stick, l.stick_r)) return kUnitStick;
    if (Mode() == kFixed && InCircle(x, y, l.rstick, l.stick_r)) return kUnitRStick;
    return -1;
}

int HitTest(const Layout& l, float x, float y) {
    if (InCircle(x, y, l.gear, l.gear_r * 1.4f)) return kGear;
    // a pad takes every touch while one is plugged in, whether SDL opened it or the phone handed it over as a
    // virtual device; the settings panel is the activity's own window and takes its touches before they get here
    if (input::Connected() || loose::Connected()) return kNone;
    for (int b = 0; b < 4; b++)
        if (InCircle(x, y, l.btn[b], l.btn_r * 1.15f)) return kButton + b;
    for (int i = 0; i < kPillCount; i++)
        if (InPill(l, x, y, l.pill[kPillButton[i]])) return kButton + kPillButton[i];
    for (int a = 0; a < 4; a++)
        if (InCircle(x, y, l.arrow[a], l.arrow_r * 1.1f)) return kArrow + a;
    if (x < l.w * 0.45f && InCircle(x, y, l.stick, l.stick_r * 1.7f)) return kStick;
    if (Mode() == kFixed) return InCircle(x, y, l.rstick, l.stick_r * 1.7f) ? kCamera : kNone;
    if (x > l.w * 0.45f) return kCamera;
    return kNone;
}

void PressButton(int index, bool down) {
    static const int kAnalogOf[4] = {kA, kB, kX, kY};
    if (index < 4) {
        uint64_t v = g_analog.load();
        uint32_t shift = uint32_t(kAnalogOf[index]) * 8;
        v = down ? (v | (0xFFull << shift)) : (v & ~(0xFFull << shift));
        g_analog = v;
    } else if (index == kBtnStart || index == kBtnBack || index == kBtnL3 || index == kBtnR3) {
        uint16_t bit = index == kBtnStart ? kStart
                     : index == kBtnBack  ? kBack
                     : index == kBtnL3    ? kLeftThumb
                                          : kRightThumb;
        uint32_t v = g_buttons.load();
        v = down ? (v | bit) : (v & ~uint32_t(bit));
        g_buttons = v;
    } else if (index == kBtnLT || index == kBtnRT || index == kBtnWhite || index == kBtnBlack) {
        // the triggers and the pad's two small buttons are all analog in the pad's own state
        const int which = index == kBtnLT    ? kLeftTrigger
                        : index == kBtnRT    ? kRightTrigger
                        : index == kBtnWhite ? kWhite
                                             : kBlack;
        uint64_t v = g_analog.load();
        uint32_t shift = uint32_t(which) * 8;
        v = down ? (v | (0xFFull << shift)) : (v & ~(0xFFull << shift));
        g_analog = v;
    }
}

// an arrow held or let go: its bit alone, the others as they are
void PressArrow(int arrow, bool down) {
    uint32_t v = g_buttons.load();
    v = down ? (v | kArrowBits[arrow]) : (v & ~kArrowBits[arrow]);
    g_buttons = v;
}

void StickTo(float x, float y, const Layout& l) {
    float dx = (x - l.stick[0]) / l.stick_r;
    float dy = (y - l.stick[1]) / l.stick_r;
    float len = std::hypot(dx, dy);
    if (len > 1) {
        dx /= len;
        dy /= len;
    }
    g_lx = std::clamp(dx, -1.0f, 1.0f);
    g_ly = std::clamp(-dy, -1.0f, 1.0f);  // screen y down, the stick's up
}

// the fixed right stick: its push from its middle, a full one at the sensitivity's reach
void RightStickTo(float x, float y, float cx, float cy, const Layout& l) {
    float sx = (x - cx) / l.cam_r, sy = (y - cy) / l.cam_r;
    float len = std::hypot(sx, sy);
    if (len > 1) {
        sx /= len;
        sy /= len;
    }
    g_rx = sx;
    g_ry = -sy;
}

}  // namespace

void Init() { LOGI("touch: on-screen controls"); }

void Event(const SDL_Event& e) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(host::Window(), &w, &h);
    Layout lay = Make(float(std::max(w, 1)), float(std::max(h, 1)));
    // the layout editor takes the fingers: one moves the control it took, and nothing reaches the game
    if (g_editing.load(std::memory_order_relaxed)) {
        switch (e.type) {
            case SDL_EVENT_FINGER_DOWN: {
                float x = e.tfinger.x * lay.w, y = e.tfinger.y * lay.h;
                int u = HitUnit(lay, x, y);
                if (u < 0) break;
                g_drag = u;
                g_drag_id = e.tfinger.fingerID;
                g_grab[0] = x - g_units[size_t(u)].x * lay.w;
                g_grab[1] = y - g_units[size_t(u)].y * lay.h;
                break;
            }
            case SDL_EVENT_FINGER_MOTION: {
                if (g_drag < 0 || e.tfinger.fingerID != g_drag_id) break;
                float x = e.tfinger.x * lay.w - g_grab[0], y = e.tfinger.y * lay.h - g_grab[1];
                g_units[size_t(g_drag)].x = std::clamp(x / lay.w, 0.0f, 1.0f);
                g_units[size_t(g_drag)].y = std::clamp(y / lay.h, 0.0f, 1.0f);
                break;
            }
            case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_CANCELED:
                if (g_drag >= 0 && e.tfinger.fingerID == g_drag_id) {
                    g_drag = -1;
                    app::Save();
                }
                break;
            default:
                break;
        }
        return;
    }
    switch (e.type) {
        case SDL_EVENT_FINGER_DOWN: {
            float x = e.tfinger.x * lay.w, y = e.tfinger.y * lay.h;
            int role = HitTest(lay, x, y);
            if (role == kNone) return;
            std::lock_guard lock(g_lock);
            if (role == kGear) {
                // what the fingers held lets go: the panel opens over the game, which reads no touch until it closes
                ReleaseAll();
                host::ShowSettings();
                return;
            }
            g_touches.push_back({e.tfinger.fingerID, role, x, y, x, y});
            if (role == kStick) StickTo(x, y, lay);
            else if (role == kCamera && Mode() == kFixed) RightStickTo(x, y, lay.rstick[0], lay.rstick[1], lay);
            else if (role == kCamera && Mode() == kFloating) {
                g_cam_x = x;
                g_cam_y = y;
                g_cam = true;
            }
            else if (role >= kArrow) PressArrow(role - kArrow, true);
            else if (role >= kButton && role < kButton + kBtnCount) PressButton(role - kButton, true);
            break;
        }
        case SDL_EVENT_FINGER_MOTION: {
            std::lock_guard lock(g_lock);
            for (Touch& t : g_touches) {
                if (t.id != e.tfinger.fingerID) continue;
                float x = e.tfinger.x * lay.w, y = e.tfinger.y * lay.h;
                const float dx = x - t.x, dy = y - t.y;
                t.x = x;
                t.y = y;
                if (t.role == kStick) {
                    StickTo(x, y, lay);
                } else if (t.role == kCamera && Mode() == kFixed) {
                    RightStickTo(x, y, lay.rstick[0], lay.rstick[1], lay);
                } else if (t.role == kCamera && Mode() == kSwipe) {
                    // a drag turns the view: each pixel adds to the stick's push, which Apply eases back, so the turn
                    // follows the swipe's speed; at 300% sensitivity a twenty-fifth of the short side is a full push
                    const float full = lay.s * 0.12f / Sensitivity();
                    g_rx = std::clamp(g_rx.load() + dx / full, -1.0f, 1.0f);
                    g_ry = std::clamp(g_ry.load() - dy / full, -1.0f, 1.0f);
                } else if (t.role == kCamera) {
                    // the right stick, its middle where the finger went down: held over as long as the finger
                    // stays, as the command menu's choice needs; past its reach the middle follows the finger
                    float sx = (x - t.ox) / lay.cam_r, sy = (y - t.oy) / lay.cam_r;
                    float len = std::hypot(sx, sy);
                    if (len > 1) {
                        sx /= len;
                        sy /= len;
                        t.ox = x - sx * lay.cam_r;
                        t.oy = y - sy * lay.cam_r;
                        g_cam_x = t.ox;
                        g_cam_y = t.oy;
                    }
                    g_rx = sx;
                    g_ry = -sy;  // screen y down, the stick's up
                }
                // an arrow stays the one first touched, wherever the thumb slides: no direction changes by itself
                break;
            }
            break;
        }
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            std::lock_guard lock(g_lock);
            for (size_t i = 0; i < g_touches.size(); i++) {
                Touch& t = g_touches[i];
                if (t.id != e.tfinger.fingerID) continue;
                if (t.role == kStick) {
                    g_lx = 0;
                    g_ly = 0;
                } else if (t.role == kCamera) {
                    // a swipe's turn eases out in Apply, as the flick it was; a stick held lets go at once
                    if (Mode() != kSwipe) g_rx = g_ry = 0;
                    g_cam = false;
                } else if (t.role >= kArrow) {
                    PressArrow(t.role - kArrow, false);
                } else if (t.role >= kButton && t.role < kButton + kBtnCount) {
                    PressButton(t.role - kButton, false);
                }
                g_touches.erase(g_touches.begin() + long(i));
                break;
            }
            break;
        }
        default:
            break;
    }
}

void Apply(input::Pad& pad) {
    if (g_editing.load(std::memory_order_relaxed)) return;
    uint64_t analog = g_analog.load();
    uint32_t buttons = g_buttons.load();
    float lx = g_lx.load(), ly = g_ly.load();
    float rx = g_rx.load(), ry = g_ry.load();
    if (analog || buttons || lx || ly || rx || ry) pad.connected = true;
    for (int i = 0; i < 8; i++) {
        uint8_t v = uint8_t(analog >> (i * 8));
        if (v) pad.analog[i] = v;
    }
    pad.buttons |= uint16_t(buttons);
    if (lx || ly) {
        pad.lx = int16_t(std::lround(std::clamp(lx, -1.0f, 1.0f) * 32767.0f));
        pad.ly = int16_t(std::lround(std::clamp(ly, -1.0f, 1.0f) * 32767.0f));
    }
    if (rx || ry) {
        pad.rx = int16_t(std::lround(std::clamp(rx, -1.0f, 1.0f) * 32767.0f));
        pad.ry = int16_t(std::lround(std::clamp(ry, -1.0f, 1.0f) * 32767.0f));
        // a swipe's push eases out over the next polls, as a stick held for a moment would
        if (Mode() == kSwipe) {
            g_rx = std::fabs(rx) < 0.02f ? 0.0f : rx * 0.8f;
            g_ry = std::fabs(ry) < 0.02f ? 0.0f : ry * 0.8f;
        }
    }
    // a controller the phone took over comes in over the top of both
    loose::Merge(pad);
}

void EndEditing() {
    g_editing = false;
    g_drag = -1;
    app::Save();
}

void ResetUnits() {
    std::copy(std::begin(kDefaultUnits), std::end(kDefaultUnits), g_units.begin());
    app::Save();
}

// the editor: every control ringed, so it is clear what a finger takes. Its Done and Reset are the activity's own bar
// (LayoutEditor.java), at the bottom of the screen, out of the way of the controls.
void DrawEditor(const Layout& l, ImDrawList* dl) {
    const ImU32 ring = IM_COL32(235, 190, 95, 200);
    dl->AddCircle(ImVec2(l.stick[0], l.stick[1]), l.stick_r, ring, 48, 2.0f);
    if (Mode() == kFixed) dl->AddCircle(ImVec2(l.rstick[0], l.rstick[1]), l.stick_r, ring, 48, 2.0f);
    for (int b = 0; b < 4; b++) dl->AddCircle(ImVec2(l.btn[b][0], l.btn[b][1]), l.btn_r, ring, 32, 2.0f);
    dl->AddCircle(ImVec2(l.dpad[0], l.dpad[1]), l.arrow_r * 2.0f, ring, 32, 2.0f);
    for (int i = 0; i < kPillCount; i++) {
        const float* c = l.pill[kPillButton[i]];
        dl->AddRect(ImVec2(c[0] - l.pill_w / 2, c[1] - l.pill_h / 2),
                    ImVec2(c[0] + l.pill_w / 2, c[1] + l.pill_h / 2), ring, l.pill_h / 2, 0, 2.0f);
    }
    dl->AddCircle(ImVec2(l.gear[0], l.gear[1]), l.gear_r * 1.4f, ring, 32, 2.0f);
}

void Draw() {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;
    ImGuiIO& io = ImGui::GetIO();
    Layout l = Make(io.DisplaySize.x, io.DisplaySize.y);
    // the panel's opacity: everything the controls are drawn with, the gear too, is its own alpha times this
    const float fade = std::clamp(game::Opt().controls_opacity / 100.0f, 0.2f, 1.0f);
    auto at = [fade](int a) { return int(std::lround(float(a) * fade)); };
    const ImU32 line = IM_COL32(255, 255, 255, at(80));
    const ImU32 label = IM_COL32(255, 255, 255, at(210));

    // the gear, drawn as the pills are: a dark disc with a thin rim, the gear's outline in it, gold while the panel
    // is open
    {
        ImVec2 c(l.gear[0], l.gear[1]);
        float r = l.gear_r;
        ImU32 stroke = label;
        dl->AddCircleFilled(c, r * 1.3f, IM_COL32(20, 16, 14, at(165)), 40);
        dl->AddCircle(c, r * 1.3f, line, 40, 1.5f);
        // eight teeth: each a flat top at the outer radius and a flat valley at the inner one, slanted sides between
        constexpr int kTeeth = 8;
        constexpr float kStep = 6.2831853f / kTeeth;
        float outer = r * 0.86f, inner = r * 0.64f;
        ImVec2 pts[kTeeth * 4];
        for (int i = 0; i < kTeeth; i++) {
            float a = float(i) * kStep;
            const float corners[4][2] = {{-0.30f, inner}, {-0.17f, outer}, {0.17f, outer}, {0.30f, inner}};
            for (int k = 0; k < 4; k++) {
                float ak = a + corners[k][0] * kStep;
                pts[i * 4 + k] = ImVec2(c.x + std::cos(ak) * corners[k][1], c.y + std::sin(ak) * corners[k][1]);
            }
        }
        float width = std::max(r * 0.11f, 1.5f);
        dl->AddPolyline(pts, kTeeth * 4, stroke, ImDrawFlags_Closed, width);
        dl->AddCircle(c, r * 0.27f, stroke, 24, width);
    }
    // a controller plugged in takes the pad's place: its controls are not drawn and take no touch, the gear staying
    // for the panel; whatever a finger held when it came in lets go
    bool pad = input::Connected() || loose::Connected();
    if (pad != g_pad.load()) {
        g_pad = pad;
        if (pad) {
            std::lock_guard lock(g_lock);
            ReleaseAll();
        }
    }
    // a controller takes the pad's place: its controls are not drawn while one is plugged in
    if (pad) return;

    EnsureArt();
    // The look: dark discs of a warm black, as the gang's leather, with a thin rim and a soft shadow
    // under it; what a button does is its icon in white, and which of the pad's buttons it is rides on its rim. Held,
    // a control warms to brown with a gold rim.
    const ImU32 kGold = IM_COL32(235, 190, 95, at(240));
    const ImU32 shadow = IM_COL32(0, 0, 0, at(70)), base = IM_COL32(20, 16, 14, at(165));
    const ImU32 lit = IM_COL32(74, 50, 32, at(210));
    const ImU32 rim = IM_COL32(255, 255, 255, at(72)), ink = IM_COL32(255, 255, 255, at(235));
    auto disc = [&](ImVec2 c, float r, bool on) {
        dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.07f), r * 1.02f, shadow, 48);
        dl->AddCircleFilled(c, r, on ? lit : base, 48);
        dl->AddCircle(c, r, on ? kGold : rim, 48, on ? std::max(r * 0.045f, 2.0f) : std::max(r * 0.022f, 1.2f));
    };
    auto stick = [&](const float at_c[2], float x, float y) {
        ImVec2 c(at_c[0], at_c[1]);
        bool on = x || y;
        disc(c, l.stick_r, false);
        ImVec2 k(c.x + x * l.stick_r * 0.62f, c.y - y * l.stick_r * 0.62f);
        float kr = l.stick_r * 0.42f;
        dl->AddCircleFilled(ImVec2(k.x, k.y + kr * 0.1f), kr * 1.04f, shadow, 40);
        dl->AddCircleFilled(k, kr, on ? lit : IM_COL32(44, 36, 32, at(220)), 40);
        dl->AddCircle(k, kr, on ? kGold : IM_COL32(255, 255, 255, at(110)), 40, std::max(kr * 0.05f, 1.2f));
    };

    // the stick
    stick(l.stick, g_lx.load(), g_ly.load());

    // the four face buttons: what each does in the game, and the pad's own letter on its badge, in the pad's colours
    {
        const uint64_t held = g_analog.load();
        static const int kAnalogOf[4] = {kA, kB, kX, kY};
        // A heavy attack, B grab and throw, X light attack, Y the action: the PS2 game's Cross, Circle, Square, Triangle
        static const float* const kIcon[4] = {ui_paths::kHeavy, ui_paths::kGrab, ui_paths::kLight, ui_paths::kAction};
        static const int kLetter[4] = {kArtLetterA, kArtLetterB, kArtLetterX, kArtLetterY};
        static const int kFace[4][3] = {{96, 176, 72}, {214, 64, 52}, {52, 120, 204}, {236, 186, 52}};
        for (int b = 0; b < 4; b++) {
            ImVec2 c(l.btn[b][0], l.btn[b][1]);
            float r = l.btn_r;
            bool on = (held >> (kAnalogOf[b] * 8)) & 0xFF;
            disc(c, r, on);
            DrawIcon(dl, kIcon[b], ImVec2(c.x - r * 0.05f, c.y - r * 0.05f), r * 1.12f, on ? kGold : ink);
            // the badge on the rim, low on the right, set off from the disc by a ring of the backdrop's colour
            ImVec2 bc(c.x + r * 0.70f, c.y + r * 0.70f);
            float br = r * 0.36f;
            dl->AddCircleFilled(bc, br + std::max(r * 0.06f, 2.0f), IM_COL32(12, 10, 9, at(220)), 32);
            dl->AddCircleFilled(bc, br, IM_COL32(kFace[b][0], kFace[b][1], kFace[b][2], at(240)), 32);
            ImU32 letter = b == 3 ? IM_COL32(40, 30, 10, at(240)) : IM_COL32(255, 255, 255, at(245));
            DrawArt(dl, kLetter[b], bc, br * 1.45f, letter);
        }
    }

    // the right stick: a stick of its own when fixed, else while a finger holds it its reach around where the finger
    // went down, and the knob
    if (Mode() == kFixed) {
        stick(l.rstick, g_rx.load(), g_ry.load());
    } else if (g_cam.load()) {
        ImVec2 c(g_cam_x.load(), g_cam_y.load());
        dl->AddCircleFilled(c, l.cam_r, IM_COL32(20, 16, 14, at(90)), 48);
        dl->AddCircle(c, l.cam_r, rim, 48, 2.0f);
        ImVec2 k(c.x + g_rx.load() * l.cam_r, c.y - g_ry.load() * l.cam_r);
        dl->AddCircleFilled(k, l.cam_r * 0.36f, lit, 32);
        dl->AddCircle(k, l.cam_r * 0.36f, kGold, 32, 2.0f);
    }

    // the arrows: four small discs apart, each with its chevron pointing away from the middle
    {
        uint32_t held = g_buttons.load();
        const float dirs[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        for (int a = 0; a < 4; a++) {
            ImVec2 c(l.arrow[a][0], l.arrow[a][1]);
            float r = l.arrow_r;
            bool on = held & kArrowBits[a];
            disc(c, r, on);
            float dx = dirs[a][0], dy = dirs[a][1], t = r * 0.36f;
            ImVec2 tip(c.x + dx * t * 1.1f, c.y + dy * t * 1.1f);
            ImVec2 b1(c.x - dx * t * 0.5f - dy * t, c.y - dy * t * 0.5f + dx * t);
            ImVec2 b2(c.x - dx * t * 0.5f + dy * t, c.y - dy * t * 0.5f - dx * t);
            ImVec2 pts[3] = {b1, tip, b2};
            dl->AddPolyline(pts, 3, on ? kGold : ink, ImDrawFlags_None, std::max(r * 0.11f, 2.0f));
        }
    }

    // the pad's other buttons as pills: an icon for what it does in the game beside the pad's name for it, or for White
    // and Black (the first person look and the guard) a dot in the button's colour; held, a pill's rim is gold
    {
        static const struct {
            int button;
            const float* icon;
            int label;
        } kPills[] = {{kBtnBack, nullptr, kArtBack},          {kBtnStart, nullptr, kArtStart},
                      {kBtnLT, ui_paths::kSprint, kArtLT},    {kBtnRT, ui_paths::kTarget, kArtRT},
                      {kBtnL3, ui_paths::kCommand, kArtL3},   {kBtnR3, nullptr, kArtR3},
                      {kBtnWhite, ui_paths::kLook, kArtNone}, {kBtnBlack, ui_paths::kGuard, kArtNone}};
        const uint64_t analog = g_analog.load();
        const uint32_t buttons = g_buttons.load();
        for (const auto& p : kPills) {
            ImVec2 c(l.pill[p.button][0], l.pill[p.button][1]);
            const float w = l.pill_w, h = l.pill_h;
            bool on = false;
            switch (p.button) {
                case kBtnLT: on = (analog >> (kLeftTrigger * 8)) & 0xFF; break;
                case kBtnRT: on = (analog >> (kRightTrigger * 8)) & 0xFF; break;
                case kBtnWhite: on = (analog >> (kWhite * 8)) & 0xFF; break;
                case kBtnBlack: on = (analog >> (kBlack * 8)) & 0xFF; break;
                case kBtnStart: on = buttons & kStart; break;
                case kBtnBack: on = buttons & kBack; break;
                case kBtnL3: on = buttons & kLeftThumb; break;
                case kBtnR3: on = buttons & kRightThumb; break;
                default: break;
            }
            // White and Black are pills as the rest, the pad's button a dot in its colour beside the icon, as the face
            // buttons' badges are
            const bool white = p.button == kBtnWhite, black = p.button == kBtnBlack;
            ImU32 fg = on ? kGold : ink;
            ImVec2 a(c.x - w / 2, c.y - h / 2), z(c.x + w / 2, c.y + h / 2);
            dl->AddRectFilled(ImVec2(a.x, a.y + h * 0.07f), ImVec2(z.x, z.y + h * 0.07f), shadow, h / 2);
            dl->AddRectFilled(a, z, on ? lit : base, h / 2);
            dl->AddRect(a, z, on ? kGold : rim, h / 2, 0, on ? std::max(h * 0.045f, 2.0f) : std::max(h * 0.022f, 1.2f));
            // the icon and the label (or the dot) side by side, centred together
            float ih = h * 0.64f, lh = h * 0.50f, gap = h * 0.14f, dot = h * 0.17f;
            float iw = p.icon ? ih : 0;
            float lw = p.label >= 0 ? lh * g_art[p.label].aspect : white || black ? dot * 2 : 0;
            float total = iw + lw + (iw > 0 && lw > 0 ? gap : 0);
            float x = c.x - total / 2;
            if (p.icon) {
                DrawIcon(dl, p.icon, ImVec2(x + iw / 2, c.y), ih, fg);
                x += iw + gap;
            }
            if (p.label >= 0) {
                DrawArt(dl, p.label, ImVec2(x + lw / 2, c.y), lh, fg);
            } else if (white || black) {
                ImVec2 o(x + dot, c.y);
                dl->AddCircleFilled(o, dot, white ? IM_COL32(240, 238, 232, at(240)) : IM_COL32(4, 4, 4, at(240)), 24);
                dl->AddCircle(o, dot, white ? IM_COL32(0, 0, 0, at(90)) : IM_COL32(255, 255, 255, at(170)), 24,
                              std::max(h * 0.025f, 1.2f));
            }
        }
    }
    if (g_editing.load(std::memory_order_relaxed)) DrawEditor(l, dl);
}

bool Editing() { return g_editing.load(std::memory_order_relaxed); }

void EditBegin() {
    {
        std::lock_guard lock(g_lock);
        ReleaseAll();
    }
    g_editing = true;
}

void EditEnd() { EndEditing(); }

void EditReset() { ResetUnits(); }

// the layout as [[controls]] in the settings file, one entry per unit in the order above
void LoadLayout(const void* table) {
    const toml::table& t = *static_cast<const toml::table*>(table);
    const toml::array* a = t["controls"].as_array();
    if (!a) return;
    for (size_t i = 0; i < a->size() && i < size_t(kUnitCount); i++) {
        const toml::table* e = (*a)[i].as_table();
        if (!e) continue;
        g_units[i].x = float((*e)["x"].value_or(double(kDefaultUnits[i].x)));
        g_units[i].y = float((*e)["y"].value_or(double(kDefaultUnits[i].y)));
    }
}

void SaveLayout(void* table) {
    toml::table& t = *static_cast<toml::table*>(table);
    toml::array a;
    for (int i = 0; i < kUnitCount; i++)
        a.push_back(toml::table{{"x", double(g_units[size_t(i)].x)}, {"y", double(g_units[size_t(i)].y)}});
    t.insert_or_assign("controls", std::move(a));
}

}  // namespace touch
