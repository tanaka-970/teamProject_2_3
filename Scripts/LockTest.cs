using ReplayEngine;

namespace Game;

[ReplayGuid("1b3cf619027f31e6f4f043b3f65716be")]
public sealed class LockTest : ScriptBehaviour
{
	//完全なデモなので、このスクリプトは実際のゲームでは使用しなくてもいい
	public float Speed = 42.0f;
    public ObjectReference Target;

    public override void Awake()
    {
    }

    public override void Update(float deltaTime)
    {
    }
}
