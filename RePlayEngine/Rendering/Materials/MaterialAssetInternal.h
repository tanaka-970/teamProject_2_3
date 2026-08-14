#pragma once

// MaterialAsset.cpp の分割で保存と読み込みが共有する内部事情であり、
// 兄弟ファイル以外から使うものではない。

namespace ReplayEngine::Rendering
{
    struct MaterialAsset;

    namespace Detail
    {
        bool Finite(const MaterialAsset& value) noexcept;
    }
}
