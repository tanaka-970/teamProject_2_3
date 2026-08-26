#pragma once

#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>

#include <cstdint>
#include <vector>

namespace ReplayEngine::Rendering
{
    // タイルドDeferredライティング。
    //
    // 画面を16x16のタイルへ分割し、タイルごとにライトを絞ってから
    // コンピュートシェーダーでシェーディングする。ライトを定数バッファ配列では
    // なくStructuredBufferへ置くため、シェーダーを触らずにライト数を増やせる。
    //
    // 従来のフルスクリーンPS版と同じG-Buffer/SSAO/SSR/CSMを入力に取り、
    // 出力先だけがRTVからUAVに変わる。
    class TiledDeferredPass final
    {
    public:
        // Shader\tiled_light_common.hlsli の TiledLight と並びを一致させる。
        struct Light
        {
            DirectX::XMFLOAT4 position_radius{ 0.0f, 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT4 color_intensity{ 1.0f, 1.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT4 direction_cone{ 0.0f, -1.0f, 0.0f, 1.0f };
            // x=外コーンcos, y=種類, z=影マップ先頭スライス(負なら影なし), w=影の濃さ
            DirectX::XMFLOAT4 params{ 0.0f, 0.0f, -1.0f, 1.0f };
        };

        enum class LightType : int { Point = 0, Spot = 1 };

        // Shader\tiled_light_common.hlsli の TILED_LIGHT_CONSTANT_BUFFER と一致させる。
        struct Constants
        {
            DirectX::XMINT4 counts{ 0, 0, 0, 0 };      // x=ライト数, yz=タイル数, w=デバッグ
            DirectX::XMFLOAT4 params{ 32.0f, 0.0f, 0.0f, 0.0f }; // x=ヒートマップ正規化
        };

        static constexpr uint32_t kTileSize = 16;
        // StructuredBufferの容量。実行時に増やす必要が出たら再生成する。
        static constexpr uint32_t kMaxLightCapacity = 1024;
        static constexpr uint32_t kConstantSlot = 12;
        static constexpr uint32_t kLightBufferSlot = 20;

        bool Initialize(ID3D11Device* device, uint32_t width, uint32_t height);

        // 1フレーム分のライトを積む。Dispatch前に呼ぶ。
        void ClearLights() noexcept { lights_.clear(); }
        // shadow_slice が負なら影マップ無し。PS 版と同じ値をそのまま運ぶ。
        void AddPointLight(const DirectX::XMFLOAT3& position, float radius,
            const DirectX::XMFLOAT3& color, float intensity,
            int shadow_slice = -1, float shadow_strength = 1.0f);
        void AddSpotLight(const DirectX::XMFLOAT3& position, float radius,
            const DirectX::XMFLOAT3& direction, float inner_cosine, float outer_cosine,
            const DirectX::XMFLOAT3& color, float intensity,
            int shadow_slice = -1, float shadow_strength = 1.0f);
        size_t LightCount() const noexcept { return lights_.size(); }

        // G-BufferとSSAO/SSRを読み、output_uav へライティング結果を書く。
        // pbr / csm 側の CS 用リソースは呼び出し前にバインドしておくこと。
        bool Dispatch(ID3D11DeviceContext* context,
            ID3D11UnorderedAccessView* output_uav,
            ID3D11ShaderResourceView* const gbuffer[4],
            ID3D11ShaderResourceView* depth,
            ID3D11ShaderResourceView* ambient_occlusion,
            ID3D11ShaderResourceView* screen_reflection);

        bool Initialized() const noexcept { return initialized_; }
        uint32_t TileCountX() const noexcept { return tile_count_x_; }
        uint32_t TileCountY() const noexcept { return tile_count_y_; }

        // エディタから触る値。
        bool enabled = false;          // 既定はOFF。UIでONにしてPS版と見比べる
        bool debug_heatmap = false;    // タイルあたりのライト数を色で可視化
        float heatmap_scale = 32.0f;   // 赤になるライト数

    private:
        bool CreateLightBuffer(ID3D11Device* device, uint32_t capacity);

        Microsoft::WRL::ComPtr<ID3D11ComputeShader> lighting_shader_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> light_buffer_;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> light_srv_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        std::vector<Light> lights_;
        uint32_t width_ = 0;
        uint32_t height_ = 0;
        uint32_t tile_count_x_ = 0;
        uint32_t tile_count_y_ = 0;
        uint32_t light_capacity_ = 0;
        bool initialized_ = false;
    };
}
