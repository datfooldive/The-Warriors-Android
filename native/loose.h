#pragma once
#include "core/input.h"

// A controller Android hands over without its device: the phone's game service passes its buttons and sticks on as a
// virtual device, which SDL takes for no controller, so its input never reaches the game and the on-screen controls
// never give way to it. WarriorsActivity sends those events here; they play port 0, over whatever SDL read.
namespace loose {

bool Connected();            // a controller the phone took over is sending, so the on-screen controls stand down
void Merge(input::Pad& p);   // its state on top of the pad's
void Release();              // the activity lost the focus: nothing stays held

}  // namespace loose
