#pragma once
#include <cstdint>

#include <imgui.h>

// the renderer's side of the on-screen controls: RGBA pixels as a texture ImGui's draw lists take
namespace vkui {

ImTextureID Texture(const uint8_t* rgba, uint32_t w, uint32_t h);
// a texture Texture made, no longer drawn from the next draw lists on
void Release(ImTextureID id);

}  // namespace vkui
