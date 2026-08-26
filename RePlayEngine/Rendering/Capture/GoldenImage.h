#pragma once


#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ReplayEngine::Rendering::Capture
{
    // 画面を撮って基準画像と比べる。
    //
    // 【なぜ要るか】
    //   シェーダ基盤の各フェーズの完了条件は
    //   「スクリーンショットが 1 ピクセルも変わらないこと」だが、
    //   それを確かめる手段が無いまま進めていた。
    //   つまり完了条件は誰も検証していなかった。
    //
    //   目視では気付けない。色が 1 段階ずれても人間は分からないし、
    //   画面の隅で起きた変化はまず見落とす。数で止めるしかない。
    struct Image final
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        // RGBA 8bit。1 ピクセル 4 バイト、行の詰めなし。
        std::vector<std::uint8_t> rgba;

        bool Valid() const noexcept
        {
            return width != 0 && height != 0 &&
                rgba.size() == static_cast<std::size_t>(width) * height * 4u;
        }

        std::size_t PixelCount() const noexcept
        {
            return static_cast<std::size_t>(width) * height;
        }
    };

    struct CompareResult final
    {
        // 比較まで到達したか。読み込みに失敗した場合は false。
        //
        // 【false と「差分 0」を混同しないこと】
        //   基準画像が無い、読めない、大きさが違う——どれも
        //   「差分 0」ではない。混ぜると
        //   「壊れているのに通った」が起きる。
        bool compared = false;

        // 大きさが違う。ウィンドウサイズが変わっているだけのことが多い。
        bool size_mismatch = false;

        std::uint32_t width = 0;
        std::uint32_t height = 0;

        std::size_t total_pixels = 0;
        std::size_t differing_pixels = 0;

        // 全チャンネルを通しての最大差分（0..255）。
        int max_channel_delta = 0;

        // 最初に食い違ったピクセルの位置。目で確かめるときの手掛かり。
        std::uint32_t first_difference_x = 0;
        std::uint32_t first_difference_y = 0;

        bool Identical() const noexcept
        {
            return compared && !size_mismatch && differing_pixels == 0;
        }

        std::string Summary() const;
    };

    class GoldenImage final
    {
    public:
        // バックバッファを CPU 側へ吸い上げる。
        //
        // 画面取得は D3D12DeviceContext の Readback 経路だけを使用する。
        static bool SavePng(const std::filesystem::path& path,
            const Image& image, std::string& error);

        static bool LoadPng(const std::filesystem::path& path,
            Image& out, std::string& error);

        // 比べる。
        //
        // tolerance はチャンネルごとの許容差（0 なら完全一致）。
        //
        // 【alpha は見ない】
        //   バックバッファの alpha は描画パスによって中身が定まらない。
        //   見ると毎回差分が出て、誰も結果を見なくなる。
        //   見た目の回帰を見るのが目的なので RGB だけで足りる。
        //
        // diff_out が非 nullptr なら差分画像を作る。
        // 食い違ったピクセルを赤、一致を暗い灰にする。
        static bool Compare(const Image& golden, const Image& current,
            int tolerance, CompareResult& out, Image* diff_out);

        // 基準画像の置き場。Saved/Golden/<name>.png
        static std::filesystem::path GoldenPath(const std::string& name);
        static std::filesystem::path LatestPath(const std::string& name);
        static std::filesystem::path DiffPath(const std::string& name);
    };
}
