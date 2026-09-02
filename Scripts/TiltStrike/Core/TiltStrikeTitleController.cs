using System;
using ReplayEngine;

namespace Game.TiltStrike;

[ReplayGuid("9d8fe0fb1f5a4f70a89bdce831a2b4e1")]
public sealed class TiltStrikeTitleController : ScriptBehaviour
{
    private const string GameSceneGuid = "b73f4d98626743b58d60d5c9473907e2";

    private ObjectHandle startPrompt;
    private ObjectHandle titleBall;
    private Vector3 titleBallOrigin;
    private float elapsed;
    private bool transitionRequested;

    public override void Start()
    {
        TryFind("StartPrompt", out startPrompt);
        if (TryFind("TitleBall", out titleBall))
            titleBallOrigin = Runtime.Transform(titleBall).Position;
    }

    public override void Update(float deltaTime)
    {
        elapsed += MathF.Max(0.0f, deltaTime);

        if (!startPrompt.IsEmpty && Runtime.TryGetComponent<UITextComponent>(startPrompt, out var text))
        {
            var pulse = 0.62f + 0.38f * (0.5f + 0.5f * MathF.Sin(elapsed * 4.2f));
            text.Color = new Color(0.76f, 0.96f, 0.87f, pulse);
        }

        if (!titleBall.IsEmpty)
        {
            var transform = Runtime.Transform(titleBall);
            transform.Position = titleBallOrigin + new Vector3(
                MathF.Sin(elapsed * 0.85f) * 1.25f,
                MathF.Sin(elapsed * 1.7f) * 0.18f,
                0.0f);
            transform.LocalRotationEuler = new Vector3(elapsed * 0.7f, elapsed, elapsed * 0.45f);
        }

        if (!transitionRequested && elapsed >= 0.35f &&
            Input.GetMouseButtonDown(MouseButton.Left))
        {
            transitionRequested = true;
            if (!startPrompt.IsEmpty)
                Runtime.SetUIText(startPrompt, "LOADING TABLE...");

            var status = Runtime.LoadScene(GameSceneGuid);
            if (status != RuntimeStatus.Ok)
            {
                transitionRequested = false;
                Runtime.LogError($"TiltStrike: gameplay scene request failed ({status})", GameObject);
            }
        }
    }

    private bool TryFind(string name, out ObjectHandle handle)
    {
        var result = Runtime.FindGameObject(name);
        handle = result.Value;
        return result.Succeeded && !handle.IsEmpty;
    }
}
