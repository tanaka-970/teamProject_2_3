#pragma once

#include <DirectXMath.h>

#include <cstdint>
#include <vector>

namespace ReplayEngine::Rendering
{
    // QEM (Quadric Error Metrics) によるメッシュ簡略化。
    //
    // Garland & Heckbert の手法。各頂点に「その頂点が本来乗っていた平面群からの
    // 二乗距離」を表す4x4対称行列(quadric)を持たせ、エッジを潰したときの誤差が
    // 最小になる順に縮約していく。
    //
    // 頂点クラスタリング(格子で量子化する方式)と違い、平面が保たれるので
    // 建築物のような直線・平面主体のモデルでも形が崩れない。Sponzaのような
    // ステージには必須。
    //
    // 実装上の要点:
    //   - 境界エッジ(隣接三角形が1つしかないエッジ)は潰さない。穴が開くため。
    //   - 縮約後に三角形が裏返る場合は棄却する(法線フリップ判定)。
    //   - UV/法線は縮約位置に応じて線形補間する。
    //   - 優先度キューは遅延削除方式。潰した頂点に紐づく古いエントリは
    //     取り出した時点でバージョン番号を見て捨てる。
    class MeshSimplifier final
    {
    public:
        // 簡略化の入出力に使う最小限の頂点。位置以外は補間対象。
        struct Vertex
        {
            DirectX::XMFLOAT3 position{};
            DirectX::XMFLOAT3 normal{ 0.0f, 1.0f, 0.0f };
            DirectX::XMFLOAT2 texcoord{};
        };

        struct Options
        {
            // 目標三角形数の比率 (0.5 = 半分まで減らす)。
            float target_ratio = 0.5f;
            // これ以上の誤差になったら、目標に届かなくても打ち切る。
            // 0以下なら無制限(比率だけで決める)。
            double error_limit = 0.0;
            // 縮約で三角形が裏返るのを許容しない。通常はtrue。
            bool prevent_normal_flip = true;
            // 境界(開いた縁)を保護する。falseにすると縁が縮んで穴が開く。
            bool preserve_boundary = true;
        };

        struct Result
        {
            std::vector<Vertex> vertices;
            std::vector<std::uint32_t> indices;
            std::uint32_t source_triangles = 0;
            std::uint32_t result_triangles = 0;
            double max_error = 0.0;
        };

        // 三角形リスト(indices.size() % 3 == 0)を簡略化する。
        // 失敗した場合や減らせなかった場合は入力をそのまま返す。
        static Result Simplify(const std::vector<Vertex>& vertices,
            const std::vector<std::uint32_t>& indices, const Options& options);
    };
}
