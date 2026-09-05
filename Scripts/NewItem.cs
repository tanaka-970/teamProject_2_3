using ReplayEngine;

namespace Game;

// 定期的にジャンプする。操作中のキャラクターにも、ただのアイテムにも付けられる。
//
// Character Motor があればそちらへジャンプを頼む（物理・接地判定に乗る）。
// 無ければ Transform を放物線で動かす（見た目だけ跳ねる）。
//
// 【見かた】Play 中に Inspector を見る。
//   JumpCount … 跳んだ回数。増えていれば動いている
//   UsingMotor … 1 なら Character Motor 経由、0 なら Transform を直接動かしている
//   Airborne  … 空中なら 1
//
// JumpCount が増えるのに動かないときは、UsingMotor を見る。
// 0 なら Motor など他の Component が位置を上書きしている。
[ReplayGuid("289c6b71fc9f02efed18635bff936541")]
public sealed class NewItem : ScriptBehaviour
{
    [Tooltip("着地してから次に跳ぶまでの秒数")]
    [Range(0.0, 10.0)]
    public float Interval = 1.0f;

    [Tooltip("ジャンプの高さ。Character Motor が無いときだけ効く")]
    [Range(0.1, 20.0)]
    public float JumpHeight = 2.0f;

    [Tooltip("跳んでから着地するまでの秒数。Character Motor が無いときだけ効く")]
    [Range(0.1, 5.0)]
    public float JumpDuration = 0.6f;

    [Tooltip("跳んでいる間の回転量（度／秒）。0 で回さない")]
    public float SpinSpeed = 0.0f;

    public int JumpCount = 0;
    public int UsingMotor = 0;
    public int Airborne = 0;
    public float Height = 0.0f;

    // Transform で跳ぶときの基準位置。
    private Vector3 origin;
    private bool originCaptured = false;

    private float timer = 0.0f;
    private float spin = 0.0f;

    public override void Awake()
    {
        JumpCount = 0;
        UsingMotor = 0;
        Airborne = 0;
        Height = 0.0f;
        timer = 0.0f;
        spin = 0.0f;
        originCaptured = false;
    }

    public override void Update(float deltaTime)
    {
        timer += deltaTime;

        // Character Motor があるなら、そちらへ頼む。
        // 自分で位置を書くと Motor に打ち消されるため。
        if (TryGetComponent<CharacterMotorComponent>(out var motor))
        {

            UsingMotor = 1;
            // 垂直物理が切れていると Motor がジャンプ要求を捨てる。既定は無効なので
            // ここで入れておく（Inspector の「重力とジャンプを有効化」と同じ）。
            if (!motor.VerticalPhysics) motor.VerticalPhysics = true;
            if (timer >= Interval)
            {
                timer = 0.0f;
                motor.RequestJump = true;
                JumpCount += 1;
            }
            Spin(deltaTime);
            return;
        }

        // Rigidbod接続。
        if (TryGetComponent<RigidbodyComponent>(out var body))
        {
            UsingMotor = 2;
            if (timer >= Interval)
            {
                timer = 0.0f;
                // v = sqrt(2gh)。重力に打ち勝って JumpHeight まで上がる初速。
                var gravity = 9.81f * body.GravityScale;
                if (gravity < 0.01f) gravity = 9.81f;
                var speed = (float)System.Math.Sqrt(2.0 * gravity * JumpHeight);
                var v = body.Velocity;
                body.Velocity = new Vector3(v.X, speed, v.Z);
                JumpCount += 1;
            }
            Spin(deltaTime);
            return;
        }

        UsingMotor = 0;
        JumpByTransform(deltaTime);
    }

    // Motor が無いとき用。放物線で上下させるだけ。
    private void JumpByTransform(float deltaTime)
    {
        // 基準位置は最初の Update で取る。Awake では Transform が未確定のことがある。
        if (!originCaptured)
        {
            origin = Transform.LocalPosition;
            originCaptured = true;
        }

        if (Airborne == 0)
        {
            if (timer < Interval) return;
            timer = 0.0f;
            Airborne = 1;
            JumpCount += 1;
        }

        var duration = JumpDuration > 0.01f ? JumpDuration : 0.01f;
        if (timer >= duration)
        {
            timer = 0.0f;
            Airborne = 0;
            Height = 0.0f;
            Transform.LocalPosition = origin;
            return;
        }

        
        var t = timer / duration;
        Height = JumpHeight * 4.0f * t * (1.0f - t);
        Transform.LocalPosition = new Vector3(origin.X, origin.Y + Height, origin.Z);
        Spin(deltaTime);
    }

    private void Spin(float deltaTime)
    {
        if (SpinSpeed == 0.0f) return;
        spin += SpinSpeed * deltaTime;
        Transform.LocalRotationEuler = new Vector3(0.0f, spin, 0.0f);
    }
}
