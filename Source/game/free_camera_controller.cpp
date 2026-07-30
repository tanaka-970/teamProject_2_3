#include "free_camera_controller.h"
#include "camera.h"
#include <windows.h>
#include <cmath>

void FreeCameraController::SyncCameraToController(const Camera& camera)
{
    focus = camera.GetFocus();
    eye   = camera.GetEye();
    up    = camera.GetUp();
    right = camera.GetRight();

    DirectX::XMFLOAT3 v{ eye.x - focus.x, eye.y - focus.y, eye.z - focus.z };
    distance = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);

    angleX = atan2f(v.y, sqrtf(v.x * v.x + v.z * v.z));
    angleY = atan2f(v.x, v.z);
}

void FreeCameraController::SyncControllerToCamera(Camera& camera)
{
    DirectX::XMFLOAT3 newEye;
    newEye.x = focus.x + sinf(angleY) * cosf(angleX) * distance;
    newEye.y = focus.y + sinf(angleX) * distance;
    newEye.z = focus.z + cosf(angleY) * cosf(angleX) * distance;
    camera.SetLookAt(newEye, focus, { 0, 1, 0 });
}

void FreeCameraController::Update(float elapsed_time)
{
    static POINT prevPos{};
    POINT cur; GetCursorPos(&cur);
    POINT delta{ cur.x - prevPos.x, cur.y - prevPos.y };
    prevPos = cur;

    if (GetAsyncKeyState(VK_RBUTTON) & 0x8000)
    {
        const float k = 0.005f;
        angleY +=  delta.x * k;
        angleX += -delta.y * k;
    }

    if (GetAsyncKeyState(VK_MBUTTON) & 0x8000)
    {
        distance += delta.y * 0.25f;
        if (distance < 1.0f)   distance = 1.0f;
        if (distance > 800.0f) distance = 800.0f;
    }

    const float keyRotateSpeed = 1.8f * elapsed_time;
    if (GetAsyncKeyState('J') & 0x8000) angleY -= keyRotateSpeed;
    if (GetAsyncKeyState('L') & 0x8000) angleY += keyRotateSpeed;
    if (GetAsyncKeyState('I') & 0x8000) angleX += keyRotateSpeed;
    if (GetAsyncKeyState('K') & 0x8000) angleX -= keyRotateSpeed;

    const float lim = DirectX::XMConvertToRadians(85.0f);
    if (angleX >  lim) angleX =  lim;
    if (angleX < -lim) angleX = -lim;

    float panSpeed = 0.15f;
    if (GetAsyncKeyState('U') & 0x8000) focus.y += panSpeed;
    if (GetAsyncKeyState('O') & 0x8000) focus.y -= panSpeed;

    if (GetAsyncKeyState(VK_PRIOR) & 0x8000)
    {
        distance -= 0.3f;
        if (distance < 1.0f) distance = 1.0f;
    }
    if (GetAsyncKeyState(VK_NEXT) & 0x8000)
    {
        distance += 0.3f;
        if (distance > 80.0f) distance = 80.0f;
    }
}
