#pragma once

#include <windows.h>

namespace GameInput
{
    inline bool Held(int vk)     { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
    inline bool Pressed(int vk)  { return (GetAsyncKeyState(vk) & 0x0001) != 0; }

    inline float AxisX() // A/D or Left/Right
    {
        float x = 0.0f;
        if (Held('D') || Held(VK_RIGHT)) x += 1.0f;
        if (Held('A') || Held(VK_LEFT))  x -= 1.0f;
        return x;
    }
    inline float AxisY() // W/S or Up/Down
    {
        float y = 0.0f;
        if (Held('W') || Held(VK_UP))   y += 1.0f;
        if (Held('S') || Held(VK_DOWN)) y -= 1.0f;
        return y;
    }
    inline bool JumpPressed() { return Pressed(VK_SPACE); }
}
