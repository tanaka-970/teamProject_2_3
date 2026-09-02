using System;
using ReplayEngine;

namespace Game.TiltStrike;

[ReplayGuid("2d7b511b56ac4eaaa3e75e8e0152211e")]
public sealed class TiltBoardController : ScriptBehaviour
{
    private const float MaximumTiltDegrees = 10.5f;
    private const float DegreesPerPixel = 0.040f;
    private const float FollowSharpness = 10.0f;
    private const float ReturnSharpness = 4.5f;
    private static readonly Vector2 TiltCursorOrigin = new(-790.0f, -375.0f);

    private ObjectHandle collisionMesh;
    private ObjectHandle visualRoot;
    private ObjectHandle tiltCursor;

    private Vector2 dragStart;
    private float dragStartPitch;
    private float dragStartRoll;
    private float targetPitch;
    private float targetRoll;
    private float currentPitch;
    private float currentRoll;
    private bool dragging;

    public bool InputLocked { get; set; } = true;
    public float CurrentPitchDegrees => currentPitch;
    public float CurrentRollDegrees => currentRoll;

    public override void Start()
    {
        FindRequired("TiltCollision", out collisionMesh);
        FindRequired("VisualBoardRoot", out visualRoot);
        FindOptional("TiltCursor", out tiltCursor);
        ApplyBoardPose();
    }

    public override void Update(float deltaTime)
    {
        if (InputLocked)
        {
            dragging = false;
            targetPitch = 0.0f;
            targetRoll = 0.0f;
        }
        else
        {
            if (Input.GetMouseButtonDown(MouseButton.Left))
            {
                dragging = true;
                dragStart = Input.MousePosition;
                dragStartPitch = targetPitch;
                dragStartRoll = targetRoll;
            }

            if (dragging && Input.GetMouseButton(MouseButton.Left))
            {
                var delta = Input.MousePosition - dragStart;
                targetPitch = Mathf.Clamp(
                    dragStartPitch + delta.Y * DegreesPerPixel,
                    -MaximumTiltDegrees, MaximumTiltDegrees);
                targetRoll = Mathf.Clamp(
                    dragStartRoll - delta.X * DegreesPerPixel,
                    -MaximumTiltDegrees, MaximumTiltDegrees);
            }

            if (Input.GetMouseButtonUp(MouseButton.Left))
            {
                dragging = false;
                targetPitch = 0.0f;
                targetRoll = 0.0f;
            }
        }

        UpdateTiltCursor();
    }

    public override void FixedUpdate(float fixedDeltaTime)
    {
        var sharpness = dragging && !InputLocked ? FollowSharpness : ReturnSharpness;
        var blend = 1.0f - MathF.Exp(-sharpness * MathF.Max(0.0f, fixedDeltaTime));
        currentPitch = Mathf.Lerp(currentPitch, targetPitch, blend);
        currentRoll = Mathf.Lerp(currentRoll, targetRoll, blend);
        ApplyBoardPose();
    }

    private void ApplyBoardPose()
    {
        var radians = new Vector3(
            currentPitch * Mathf.Deg2Rad,
            0.0f,
            currentRoll * Mathf.Deg2Rad);
        if (!collisionMesh.IsEmpty)
            Runtime.Transform(collisionMesh).LocalRotationEuler = radians;
        if (!visualRoot.IsEmpty)
            Runtime.Transform(visualRoot).LocalRotationEuler = radians;
    }

    private void UpdateTiltCursor()
    {
        if (tiltCursor.IsEmpty ||
            !Runtime.TryGetComponent<RectTransformComponent>(tiltCursor, out var rect))
            return;

        rect.AnchoredPosition = TiltCursorOrigin + new Vector2(
            -currentRoll / MaximumTiltDegrees * 52.0f,
            -currentPitch / MaximumTiltDegrees * 52.0f);
    }

    private void FindRequired(string name, out ObjectHandle handle)
    {
        if (!FindOptional(name, out handle))
            Runtime.LogError($"TiltStrike: required board object '{name}' is missing", GameObject);
    }

    private bool FindOptional(string name, out ObjectHandle handle)
    {
        var result = Runtime.FindGameObject(name);
        handle = result.Value;
        return result.Succeeded && !handle.IsEmpty;
    }
}
