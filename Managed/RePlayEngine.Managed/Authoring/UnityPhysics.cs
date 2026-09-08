using System;
using System.Collections.Generic;

namespace ReplayEngine;

// 始点と向きの組。Unity の Ray と同じ。
public readonly struct Ray
{
    public Ray(Vector3 origin, Vector3 direction)
    {
        this.origin = origin;
        this.direction = direction.Normalized;
    }

    public Vector3 origin { get; }
    public Vector3 direction { get; }

    public Vector3 GetPoint(float distance) => origin + direction * distance;
}

// 物理問い合わせの Public 入口。
//
// 実体は ScriptRuntimeContext の Raycast / Overlap をそのまま呼ぶ。
// ゲーム制作者に RuntimeResult や ObjectHandle を見せないための層。
public static class Physics
{
    // Unity と同じ既定。無限は Native 側が扱えないので十分大きい値にする。
    private const float DefaultMaxDistance = 1000.0f;

    public static bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hit,
        float maxDistance = DefaultMaxDistance)
    {
        var result = ScriptExecutionContext.Runtime.Raycast(origin, direction, maxDistance);
        hit = result.Succeeded ? result.Value : default;
        return result.Succeeded && hit.Valid;
    }

    public static bool Raycast(Vector3 origin, Vector3 direction,
        float maxDistance = DefaultMaxDistance)
        => Raycast(origin, direction, out _, maxDistance);

    public static bool Raycast(Ray ray, out RaycastHit hit,
        float maxDistance = DefaultMaxDistance)
        => Raycast(ray.origin, ray.direction, out hit, maxDistance);

    // 全ヒット。要素は PhysicsHit だが、point / normal / gameObject は
    // RaycastHit と同じ名前で読める。
    public static PhysicsHit[] RaycastAll(Vector3 origin, Vector3 direction,
        float maxDistance = DefaultMaxDistance)
    {
        var result = ScriptExecutionContext.Runtime.RaycastAll(origin, direction, maxDistance);
        return result.Succeeded ? result.Value : Array.Empty<PhysicsHit>();
    }

    public static bool SphereCast(Vector3 origin, float radius, Vector3 direction,
        out PhysicsHit hit, float maxDistance = DefaultMaxDistance)
    {
        var result = ScriptExecutionContext.Runtime.SphereCast(origin, radius, direction,
            maxDistance);
        hit = result.Succeeded ? result.Value : default;
        return result.Succeeded && hit.Valid;
    }

    public static bool BoxCast(Vector3 center, Vector3 halfExtents, Vector3 direction,
        out PhysicsHit hit, float maxDistance = DefaultMaxDistance)
    {
        var result = ScriptExecutionContext.Runtime.BoxCast(center, halfExtents,
            Quaternion.Identity, direction, maxDistance);
        hit = result.Succeeded ? result.Value : default;
        return result.Succeeded && hit.Valid;
    }

    public static bool CapsuleCast(Vector3 point1, Vector3 point2, float radius,
        Vector3 direction, out PhysicsHit hit, float maxDistance = DefaultMaxDistance)
    {
        var result = ScriptExecutionContext.Runtime.CapsuleCast(point1, point2, radius,
            direction, maxDistance);
        hit = result.Succeeded ? result.Value : default;
        return result.Succeeded && hit.Valid;
    }

    public static Collider[] OverlapSphere(Vector3 center, float radius)
        => CollidersOf(ScriptExecutionContext.Runtime.OverlapSphere(center, radius));

    public static Collider[] OverlapBox(Vector3 center, Vector3 halfExtents)
        => CollidersOf(ScriptExecutionContext.Runtime.OverlapBox(center, halfExtents,
            Quaternion.Identity));

    public static Collider[] OverlapCapsule(Vector3 point1, Vector3 point2, float radius)
        => CollidersOf(ScriptExecutionContext.Runtime.OverlapCapsule(point1, point2, radius));

    private static Collider[] CollidersOf(RuntimeResult<PhysicsHit[]> result)
    {
        if (!result.Succeeded || result.Value.Length == 0) return Array.Empty<Collider>();
        var found = new List<Collider>(result.Value.Length);
        foreach (var hit in result.Value)
        {
            var gameObject = GameObject.Wrap(hit.Object);
            if (gameObject == null) continue;
            var collider = gameObject.GetComponent<Collider>();
            if (collider != null) found.Add(collider);
        }
        return found.Count == 0 ? Array.Empty<Collider>() : found.ToArray();
    }
}

// Scene 遷移の Public 入口。SceneFlow の既存契約をそのまま使う。
public static class SceneManager
{
    // Asset GUID で読み込む。Scene 名で引く Native API は無いので受け付けない。
    public static void LoadScene(AssetReference<SceneAsset> scene)
        => ScriptExecutionContext.Runtime.LoadScene(scene);

    public static void LoadScene(string sceneAssetGuid)
        => ScriptExecutionContext.Runtime.LoadScene(sceneAssetGuid);

    public static SceneLoadOperation LoadSceneAsync(AssetReference<SceneAsset> scene)
        => ScriptExecutionContext.Runtime.LoadSceneAsync(scene);

    public static void ReloadScene() => ScriptExecutionContext.Runtime.ReloadScene();

    public static void ReturnToPreviousScene()
        => ScriptExecutionContext.Runtime.ReturnToPreviousScene();

    // SceneFlow アセットのイベント名で遷移させる。
    // 遷移先を C# に書かずに済む RePlayEngine 独自の入口。
    public static void TriggerFlow(string eventName)
        => ScriptExecutionContext.Runtime.TriggerSceneFlow(eventName);
}

// アプリケーション全体。
public static class Application
{
    public static void Quit(string reason = "")
        => ScriptExecutionContext.Runtime.QuitApplication(reason);
}
