using ReplayEngine;

namespace ValidationScripts;

// C# から地形を制御できることの検証用。
// 高さの読み書き・補間サンプル・彫刻・レイを 1 回ずつ通し、結果を field に残す。
// C++ 側の --validate-landscape-script がこの field を読んで判定する。
[ReplayGuid("c9e7fa5b241638ac0d15be7392f4a86d")]
public class ValidationLandscapeBehaviour : MonoBehaviour
{
    [ReadOnly] public int Width;
    [ReadOnly] public int Height;
    [ReadOnly] public float CellSize;
    [ReadOnly] public bool Found;

    [ReadOnly] public float HeightBeforeSet;
    [ReadOnly] public float HeightAfterSet;
    [ReadOnly] public float SampledBetween;

    [ReadOnly] public bool SculptApplied;
    [ReadOnly] public float PeakAfterSculpt;

    [ReadOnly] public bool RaycastHit;
    [ReadOnly] public float RaycastHeight;

    void Start()
    {
        // 地形は同じ GameObject に付いている。Unity と同じ入口で引く。
        var ground = GetComponent<Landscape>();
        Found = ground != null;
        if (ground == null)
        {
            Debug.LogError("Landscape が見つかりません", this);
            return;
        }

        Width = ground.width;
        Height = ground.height;
        CellSize = ground.cellSize;

        // ---- 高さの読み書き -------------------------------------------------
        var cx = Width / 2;
        var cz = Height / 2;
        HeightBeforeSet = ground.GetHeight(cx, cz);
        ground.SetHeight(cx, cz, 4.25f);
        HeightAfterSet = ground.GetHeight(cx, cz);

        // 格子の間を補間して取れるか。隣は 0 のままなので中間の値になる。
        var localX = (cx - (Width - 1) * 0.5f) * CellSize + CellSize * 0.5f;
        var localZ = (cz - (Height - 1) * 0.5f) * CellSize;
        SampledBetween = ground.SampleHeight(localX, localZ);

        // ---- 彫刻 -----------------------------------------------------------
        // 既存の LandscapeEditorTool がそのまま動く。
        // deltaTime を明示して、フレーム時間に依存しない結果にする。
        var corner = new Vector3(-6.0f * CellSize, 0.0f, -6.0f * CellSize);
        SculptApplied = ground.Sculpt(corner, SculptMode.Raise,
            radius: 4.0f, strength: 6.0f, falloff: 0.5f, deltaTime: 1.0f);

        // 盛った中心付近がいちばん高くなっているはず。
        var gx = (int)(corner.X / CellSize + (Width - 1) * 0.5f);
        var gz = (int)(corner.Z / CellSize + (Height - 1) * 0.5f);
        PeakAfterSculpt = ground.GetHeight(gx, gz);

        // ---- レイ -----------------------------------------------------------
        // 彫った直後の形に、その場で当たる。
        RaycastHit = ground.Raycast(new Vector3(corner.X, 40.0f, corner.Z),
            new Vector3(0.0f, -1.0f, 0.0f), out var hit, 200.0f);
        RaycastHeight = hit.point.Y;

        Debug.Log($"Landscape: {Width}x{Height} cell={CellSize} peak={PeakAfterSculpt}", this);
    }
}
