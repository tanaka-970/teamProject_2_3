#include "free_camera_controller.h"
#include "camera.h"
#include "game_input.h"
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

void FreeCameraController::Update(float elapsed_time, const GameInput::InputState& input)
{
    const float delta_x = input.PointerDeltaX();
    const float delta_y = input.PointerDeltaY();

    if (input.Held("CameraRotate"))
    {
        const float k = 0.005f;
        angleY += delta_x * k;
        angleX += -delta_y * k;
    }

    if (input.Held("CameraZoomDrag"))
    {
        distance += delta_y * 0.25f;
        if (distance < 1.0f) distance = 1.0f;
        if (distance > 800.0f) distance = 800.0f;
    }

    const float keyRotateSpeed = 1.8f * elapsed_time;
    if (input.Held("CameraYawLeft")) angleY -= keyRotateSpeed;
    if (input.Held("CameraYawRight")) angleY += keyRotateSpeed;
    if (input.Held("CameraPitchUp")) angleX += keyRotateSpeed;
    if (input.Held("CameraPitchDown")) angleX -= keyRotateSpeed;

    const float lim = DirectX::XMConvertToRadians(85.0f);
    if (angleX > lim) angleX = lim;
    if (angleX < -lim) angleX = -lim;

    const float panSpeed = 0.15f;
    if (input.Held("CameraPanUp")) focus.y += panSpeed;
    if (input.Held("CameraPanDown")) focus.y -= panSpeed;

    if (input.Held("CameraZoomIn"))
    {
        distance -= 0.3f;
        if (distance < 1.0f) distance = 1.0f;
    }
    if (input.Held("CameraZoomOut"))
    {
        distance += 0.3f;
        if (distance > 80.0f) distance = 80.0f;
    }
}
