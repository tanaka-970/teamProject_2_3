namespace Game;

// C++ Component の TypeId とエンジンイベント GUID を 1 か所に集約する。
public readonly struct PersonaEngineIds
{
    public const string MotionEventGuid = "a1000000000000000000000000000008";
    public const string MotionHitEventName = "persona.hit";

    public static readonly uint ScriptComponent = TypeId("ScriptComponent");
    public static readonly uint MotionPlayerComponent = TypeId("MotionPlayerComponent");

    public static uint TypeId(string name)
    {
        uint hash = 2166136261u;
        foreach (char c in name)
        {
            hash ^= (byte)c;
            hash *= 16777619u;
        }
        return hash == 0u ? 1u : hash;
    }
}
