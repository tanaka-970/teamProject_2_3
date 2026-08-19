using ReplayEngine;

namespace Game;

[ReplayGuid("d7e27d1cc9a44cdbb616ee27f4ad0873")]
public static class PersonaEngineIds
{
    // Component Type ID は C++ クラス名の FNV-1a 32bit。
    // 仕様 2-2 に従い、この計算はここ 1 か所だけに置く。
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

    public static readonly uint ScriptComponent = TypeId("ScriptComponent");
    public static readonly uint CanvasComponent = TypeId("CanvasComponent");
    public static readonly uint RectTransformComponent = TypeId("RectTransformComponent");
    public static readonly uint UITextComponent = TypeId("UITextComponent");
    public static readonly uint UIImageComponent = TypeId("UIImageComponent");
    public static readonly uint UISelectableComponent = TypeId("UISelectableComponent");
    public static readonly uint UIScrollViewComponent = TypeId("UIScrollViewComponent");
    public static readonly uint UIEffectStackComponent = TypeId("UIEffectStackComponent");
}
