using System;
using ReplayEngine;

namespace Game.TiltStrike;

[ReplayGuid("7f3bb2c80b8e4b03b8b40d42c4cc1f18")]
public sealed class TiltStrikeGameDirector : ScriptBehaviour
{
    private const float RoundSeconds = 45.0f;
    private const float CountdownSeconds = 2.8f;
    private const float KnockedUpThreshold = 0.67f;
    private const float KnockConfirmSeconds = 0.24f;
    private const float RetryDelaySeconds = 0.8f;
    private const int PinCount = 6;
    private const string StartSound = "resources/Game/TiltStrike/Audio/start.wav";
    private const string PinSound = "resources/Game/TiltStrike/Audio/pin.wav";
    private const string VictorySound = "resources/Game/TiltStrike/Audio/victory.wav";
    private const string TimeUpSound = "resources/Game/TiltStrike/Audio/time_up.wav";

    private readonly ObjectHandle[] pins = new ObjectHandle[PinCount];
    private readonly float[] knockTimers = new float[PinCount];
    private readonly bool[] knocked = new bool[PinCount];

    private ObjectHandle ball;
    private RigidbodyComponent ballBody;
    private ObjectHandle timerText;
    private ObjectHandle pinsText;
    private ObjectHandle messageText;
    private ObjectHandle timeFill;
    private ObjectHandle resultOverlay;
    private ObjectHandle resultTitle;
    private ObjectHandle resultDetail;
    private TiltBoardController? board;

    private float elapsed;
    private float remaining = RoundSeconds;
    private float resultElapsed;
    private float messageHold;
    private int knockedCount;
    private GameState state = GameState.Countdown;

    private enum GameState
    {
        Countdown,
        Running,
        Result
    }

    public override void Start()
    {
        var ready = ResolveScene();
        if (!ready)
        {
            Runtime.LogError("TiltStrike: gameplay scene is incomplete", GameObject);
            return;
        }

        if (TryFind("MainCamera", out var camera))
            Runtime.Transform(camera).LookAt(new Vector3(0.0f, 0.0f, 0.75f));

        Runtime.SetEnabled(resultOverlay, false);
        board!.InputLocked = true;
        SetMessage("GET READY", 0.0f);
        UpdateHud();
    }

    public override void Update(float deltaTime)
    {
        var dt = MathF.Max(0.0f, deltaTime);
        elapsed += dt;

        if (messageHold > 0.0f)
        {
            messageHold -= dt;
            if (messageHold <= 0.0f && state == GameState.Running)
                Runtime.SetUIText(messageText, "DRAG MOUSE TO TILT");
        }

        switch (state)
        {
            case GameState.Countdown:
                UpdateCountdown();
                break;
            case GameState.Running:
                UpdateRound(dt);
                break;
            case GameState.Result:
                UpdateResult(dt);
                break;
        }
    }

    private void UpdateCountdown()
    {
        var left = CountdownSeconds - elapsed;
        if (left > 2.0f) Runtime.SetUIText(messageText, "3");
        else if (left > 1.0f) Runtime.SetUIText(messageText, "2");
        else if (left > 0.0f) Runtime.SetUIText(messageText, "1");
        else
        {
            state = GameState.Running;
            board!.InputLocked = false;
            SetMessage("GO!  DRAG MOUSE TO TILT", 1.2f);
            PlaySound(StartSound, 0.72f, 1.0f);
        }
    }

    private void UpdateRound(float dt)
    {
        remaining = MathF.Max(0.0f, remaining - dt);
        CheckPins(dt);
        CheckBallBounds();
        UpdateHud();

        if (knockedCount >= PinCount)
            FinishRound(true);
        else if (remaining <= 0.0f)
            FinishRound(false);
    }

    private void CheckPins(float dt)
    {
        for (var index = 0; index < pins.Length; ++index)
        {
            if (knocked[index]) continue;

            var transform = Runtime.Transform(pins[index]);
            var tipped = MathF.Abs(transform.Up.Y) < KnockedUpThreshold ||
                         transform.Position.Y < 0.30f;
            knockTimers[index] = tipped ? knockTimers[index] + dt : 0.0f;
            if (knockTimers[index] < KnockConfirmSeconds) continue;

            knocked[index] = true;
            ++knockedCount;
            if (Runtime.TryGetComponent<PrimitiveMeshRendererComponent>(pins[index], out var renderer))
            {
                renderer.Tint = new Color(1.0f, 0.72f, 0.18f, 1.0f);
                renderer.EmissiveColor = new Vector3(1.0f, 0.22f, 0.03f);
                renderer.EmissiveStrength = 1.35f;
            }
            SetMessage($"PIN DOWN  {knockedCount} / {PinCount}", 0.9f);
            PlaySound(PinSound, 0.58f, 0.92f + knockedCount * 0.035f);
        }
    }

    private void CheckBallBounds()
    {
        var position = Runtime.Transform(ball).Position;
        if (position.Y >= -4.0f && MathF.Abs(position.X) <= 18.0f &&
            MathF.Abs(position.Z) <= 20.0f)
            return;

        ballBody.Teleport(new Vector3(0.0f, 0.75f, -5.5f));
        ballBody.SetVelocity(Vector3.Zero);
        ballBody.SetAngularVelocity(Vector3.Zero);
        remaining = MathF.Max(0.0f, remaining - 3.0f);
        SetMessage("BALL RESET  -3 SEC", 1.4f);
    }

    private void FinishRound(bool victory)
    {
        state = GameState.Result;
        resultElapsed = 0.0f;
        board!.InputLocked = true;
        Runtime.SetEnabled(resultOverlay, true);
        Runtime.SetUIText(resultTitle, victory ? "TABLE CLEARED" : "TIME UP");
        Runtime.SetUIText(resultDetail, victory
            ? $"ALL {PinCount} PINS DOWN  /  {remaining:0.0} SEC LEFT"
            : $"{knockedCount} / {PinCount} PINS DOWN\nLEFT CLICK TO RETRY");
        Runtime.SetUIText(messageText, "LEFT CLICK TO PLAY AGAIN");
        PlaySound(victory ? VictorySound : TimeUpSound, 0.82f, 1.0f);
    }

    private void UpdateResult(float dt)
    {
        resultElapsed += dt;
        if (resultElapsed >= RetryDelaySeconds && Input.GetMouseButtonDown(MouseButton.Left))
            Runtime.ReloadScene();
    }

    private void UpdateHud()
    {
        Runtime.SetUIText(timerText, $"TIME  {remaining:00.0}");
        Runtime.SetUIText(pinsText, $"PINS  {knockedCount} / {PinCount}");

        if (Runtime.TryGetComponent<UIImageComponent>(timeFill, out var image))
        {
            image.FillAmount = remaining / RoundSeconds;
            image.Color = remaining > 12.0f
                ? new Color(0.19f, 0.95f, 0.55f, 1.0f)
                : new Color(1.0f, 0.22f, 0.12f, 1.0f);
        }
    }

    private void SetMessage(string text, float holdSeconds)
    {
        Runtime.SetUIText(messageText, text);
        messageHold = holdSeconds;
    }

    private void PlaySound(string path, float volume, float pitch)
    {
        if (!Runtime.AudioAvailable) return;
        Runtime.PlayAudio(path, false, volume, pitch);
    }

    private bool ResolveScene()
    {
        var valid = TryFind("Ball", out ball) &&
                    Runtime.TryGetComponent<RigidbodyComponent>(ball, out ballBody) &&
                    TryFind("TimerText", out timerText) &&
                    TryFind("PinsText", out pinsText) &&
                    TryFind("MessageText", out messageText) &&
                    TryFind("TimeFill", out timeFill) &&
                    TryFind("ResultOverlay", out resultOverlay) &&
                    TryFind("ResultTitle", out resultTitle) &&
                    TryFind("ResultDetail", out resultDetail);

        for (var index = 0; index < pins.Length; ++index)
            valid &= TryFind($"Pin{index + 1:00}", out pins[index]);

        if (!TryFind("BoardController", out var controllerObject) ||
            !Runtime.TryGetBehaviour<TiltBoardController>(controllerObject, out var controller))
            return false;

        board = controller;
        return valid;
    }

    private bool TryFind(string name, out ObjectHandle handle)
    {
        var result = Runtime.FindGameObject(name);
        handle = result.Value;
        if (result.Succeeded && !handle.IsEmpty) return true;
        Runtime.LogError($"TiltStrike: required object '{name}' is missing", GameObject);
        return false;
    }
}
