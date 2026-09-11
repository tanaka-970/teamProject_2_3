using System;

namespace ReplayEngine;

// 地形の彫り方。C++ の Landscape::LandscapeBrushMode と同じ並び。
//
// 実際に形を変えるのは既存の LandscapeEditorTool。
// Editor のブラシと同じ実装を通るので、手で彫った地形と
// スクリプトで彫った地形が同じ形になる。
public enum SculptMode
{
    Raise = 0,
    Lower = 1,
    Smooth = 2,
    Flatten = 3,
    Noise = 4,
    Subdivide = 5,
}

// 彫る向き。C++ の Landscape::LandscapeSculptDirection と同じ並び。
public enum SculptDirection
{
    // 地形のローカル Y へ持ち上げる。普通の地面はこちら。
    LocalY = 0,
    // 頂点法線へ押し出す。壁や天井を彫るときに使う。
    VertexNormal = 1,
}

// 地形のレイ結果。
public readonly struct LandscapeHit
{
    internal LandscapeHit(Vector3 point, Vector3 normal, float distance)
    {
        this.point = point;
        this.normal = normal;
        this.distance = distance;
    }

    // 地形ローカル座標。
    public Vector3 point { get; }
    public Vector3 normal { get; }
    public float distance { get; }
}

// 地形の Public 入口。
//
// 高さの読み書きと彫刻を C# から行う。
// 形を変える実装は既存の LandscapeData / LandscapeEditorTool のままで、
// ここは呼び方を Unity 風に整えているだけ。
//
//   var ground = GameObject.Find("Ground").GetComponent<Landscape>();
//   ground.Sculpt(hit.point, SculptMode.Raise, radius: 4.0f, strength: 3.0f);
//
// SetHeight / Sculpt は LandscapeData の Revision を上げる。
// 描画メッシュと衝突形状は、その Revision を見て自動で作り直される。
public sealed class Landscape : NativeComponent
{
    internal Landscape(GameObject owner, ComponentHandle handle) : base(owner, handle) { }

    // ---- 形の情報 -------------------------------------------------------------

    // 格子の列数。地形が未初期化なら 0。
    public int width
    {
        get
        {
            NativeBridge.LandscapeInfo(Handle, out var value, out _, out _);
            return value;
        }
    }

    public int height
    {
        get
        {
            NativeBridge.LandscapeInfo(Handle, out _, out var value, out _);
            return value;
        }
    }

    // 格子 1 マスのワールド距離。
    public float cellSize
    {
        get
        {
            NativeBridge.LandscapeInfo(Handle, out _, out _, out var value);
            return value;
        }
    }

    public bool IsReady
        => NativeBridge.LandscapeInfo(Handle, out var w, out var h, out _) == RuntimeStatus.Ok &&
            w > 0 && h > 0;

    // ---- 高さ -----------------------------------------------------------------

    // 格子座標の高さ。範囲外は 0 を返す。
    public float GetHeight(int x, int z)
    {
        var result = NativeBridge.LandscapeGetHeight(Handle, x, z);
        return result.Succeeded ? result.Value : 0.0f;
    }

    public bool SetHeight(int x, int z, float value)
        => NativeBridge.LandscapeSetHeight(Handle, x, z, value) == RuntimeStatus.Ok;

    // 地形ローカルの XZ から、格子の間を補間した高さを取る。
    // 地面へ物を置くときはこちら。格子座標を意識しなくてよい。
    public float SampleHeight(float localX, float localZ)
    {
        var result = NativeBridge.LandscapeSampleHeight(Handle, localX, localZ);
        return result.Succeeded ? result.Value : 0.0f;
    }

    public float SampleHeight(Vector3 localPosition)
        => SampleHeight(localPosition.X, localPosition.Z);

    // ワールド座標から高さを取る。地形の Transform を通して変換する。
    public float SampleWorldHeight(Vector3 worldPosition)
    {
        var local = WorldToLocal(worldPosition);
        return LocalToWorld(new Vector3(local.X, SampleHeight(local), local.Z)).Y;
    }

    // ---- 彫刻 -----------------------------------------------------------------

    // 既存の LandscapeEditorTool を 1 ストロークとして通す。
    //
    // strength は 1 秒あたりの強さで、deltaTime を掛けた量が実際に入る。
    // 既定で Time.deltaTime を使うので、毎フレーム呼ぶと押している間だけ盛れる。
    public bool Sculpt(Vector3 localCenter, SculptMode mode = SculptMode.Raise,
        float radius = 4.0f, float strength = 2.0f, float falloff = 0.5f,
        float flattenHeight = 0.0f, float noiseScale = 0.35f,
        SculptDirection direction = SculptDirection.LocalY, float deltaTime = -1.0f)
    {
        var step = deltaTime > 0.0f ? deltaTime : Time.deltaTime;
        if (step <= 0.0f) return false;
        return NativeBridge.LandscapeSculpt(Handle, localCenter, (int)mode, (int)direction,
            radius, strength, falloff, flattenHeight, noiseScale, step) == RuntimeStatus.Ok;
    }

    // ワールド座標を受け取る版。呼ぶ側が地形の Transform を意識しなくて済む。
    public bool SculptWorld(Vector3 worldCenter, SculptMode mode = SculptMode.Raise,
        float radius = 4.0f, float strength = 2.0f, float falloff = 0.5f,
        float flattenHeight = 0.0f, float noiseScale = 0.35f,
        SculptDirection direction = SculptDirection.LocalY, float deltaTime = -1.0f)
        => Sculpt(WorldToLocal(worldCenter), mode, radius, strength, falloff,
            flattenHeight, noiseScale, direction, deltaTime);

    // ---- レイ -----------------------------------------------------------------

    // 地形の三角形へ直接当てる。Collider を経由しないので、
    // 彫った直後の形にもその場で当たる。
    public bool Raycast(Vector3 localOrigin, Vector3 localDirection, out LandscapeHit hit,
        float maxDistance = 1000.0f)
    {
        var status = NativeBridge.LandscapeRaycast(Handle, localOrigin, localDirection,
            maxDistance, out var point, out var normal, out var distance);
        hit = new LandscapeHit(point, normal, distance);
        return status == RuntimeStatus.Ok;
    }

    public bool RaycastWorld(Vector3 worldOrigin, Vector3 worldDirection,
        out LandscapeHit hit, float maxDistance = 1000.0f)
    {
        var localOrigin = WorldToLocal(worldOrigin);
        var localTip = WorldToLocal(worldOrigin + worldDirection);
        return Raycast(localOrigin, (localTip - localOrigin).Normalized, out hit, maxDistance);
    }

    // ---- 座標変換 -------------------------------------------------------------
    //
    // 地形は回転せず等倍で置かれるのが普通なので、平行移動だけを扱う。
    // 回転や拡大を掛けた地形は、その分だけずれる。

    public Vector3 WorldToLocal(Vector3 worldPosition) => worldPosition - transform.position;

    public Vector3 LocalToWorld(Vector3 localPosition) => localPosition + transform.position;
}
