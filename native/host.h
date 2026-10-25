#pragma once
#include <filesystem>

#include <SDL3/SDL_events.h>

#include "core/input.h"

struct SDL_Window;

// the Android host, shared between native/main.cpp, the Vulkan backend and the touch controls
namespace host {

SDL_Window* Window();
bool Foreground();                 // the activity is in front: the renderer waits while it is not
uint32_t SurfaceEpoch();           // counts returns to the front: a new window, its surface made again
std::filesystem::path FilesDir();  // internal, writable: logs, settings, hdd, shader cache
std::filesystem::path GameDir();   // where the game's files were found, empty until FindGame finds them
void Toast(const char* text);      // the activity's toast, any thread
void ShowSettings();               // the activity's settings panel over the game, any thread

}  // namespace host

namespace touch {

void Init();
void Event(const SDL_Event& e);  // a finger event, window thread
// the layout the editor writes, each control's centre as a fraction of the screen: read from the settings file's
// table before the first frame, written back into it
void LoadLayout(const void* table);
void SaveLayout(void* table);
bool Editing();  // the layout editor is on: a finger moves a control rather than pressing it
void EditBegin();
void EditEnd();
void EditReset();  // the pad's own layout back
// gameplay: the sticks and buttons of the on-screen controls, over the pad the keyboard left
void Apply(input::Pad& pad);
void Draw();  // inside the ImGui frame, UiLock held

}  // namespace touch
