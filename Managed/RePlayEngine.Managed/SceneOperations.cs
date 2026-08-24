namespace ReplayEngine;

// yield return Runtime.LoadSceneAsync(guid) で待てるScene遷移操作。
public sealed class SceneLoadOperation : IYieldInstruction
{
    private readonly ScriptRuntimeContext runtime;

    internal SceneLoadOperation(ScriptRuntimeContext runtime, RuntimeStatus requestStatus)
    {
        this.runtime = runtime;
        Status = requestStatus == RuntimeStatus.Ok
            ? RuntimeStatus.TransitionInProgress
            : requestStatus;
        IsDone = requestStatus != RuntimeStatus.Ok;
    }

    public bool IsDone { get; private set; }
    public bool Succeeded => IsDone && Status == RuntimeStatus.Ok;
    public RuntimeStatus Status { get; private set; }
    public float Progress { get; private set; }
    public string SceneGuid { get; private set; } = string.Empty;

    public bool KeepWaiting(float deltaTime)
    {
        if (IsDone) return false;

        var state = runtime.SceneTransition;
        if (!state.Succeeded)
        {
            Status = state.Status;
            IsDone = true;
            return false;
        }

        Progress = state.Value.Progress;
        if (state.Value.InProgress) return true;

        Status = state.Value.Status;
        IsDone = true;
        if (Status == RuntimeStatus.Ok)
        {
            var current = runtime.CurrentSceneGuid();
            if (current.Succeeded) SceneGuid = current.Value;
            Progress = 1.0f;
        }
        return false;
    }
}
