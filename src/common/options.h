#pragma once
#include <atomic>
#include <cstdint>

// The renderer's own settings. The title has no options screen here, so these are the
// defaults a fresh install would use; they are read every frame by d3d/vulkan_linux.cpp.
namespace game {

struct Options {
    bool bloom = true;        // the glow added over bright parts of the frame
    bool fog = true;
    uint32_t msaa = 0;        // samples a pixel of the scene, 0 off
    bool smaa = false;        // over the finished frame
    bool hdr_output = false;  // HDR10 on a display that offers it
    bool fps_counter = true;     // the frame rate in the top left corner (Android)
    bool frame_skip = false;     // frames left undrawn while the drawing is behind, the game at its speed (Android)
    uint32_t aspect = 0;         // the frame's shape on the screen: 0 the TV's, 1 16:9, 2 the whole screen stretched,
                                 // 3 the whole screen covered, what overflows cut (Android)
    uint32_t controls_opacity = 100;  // the on-screen controls' opacity, 20 to 100% (Android)
    uint32_t controls_scale = 100;    // their size, 50 to 150% (Android); the layout editor sets where they sit
    uint32_t camera_sensitivity = 300;  // the on-screen right stick, 50 to 300%: at 100 a full push is a tenth of the
                                        // screen's short side from where the finger went down (Android)
    uint32_t camera_mode = 0;  // the on-screen right stick: 0 floating, centred where the finger went down; 1 fixed,
                               // a stick of its own the layout editor places; 2 swipe, a drag turns the view (Android)
    uint32_t frame_rate = 0;   // 0 auto, the settings file's cap; 1 held to 30 a second (Android)
};

inline Options& Opt() {
    static Options options;
    return options;
}

// the frames a second the game is held to: 30 when the panel locks it, the settings file's cap otherwise (0 none)
inline uint32_t FrameCap(uint32_t file_cap) { return Opt().frame_rate == 1 ? 30 : file_cap; }

inline std::atomic<bool> g_hdr_display{false};  // the window's display has HDR on, from the renderer

}  // namespace game
