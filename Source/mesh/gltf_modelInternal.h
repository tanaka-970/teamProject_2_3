#pragma once

#include "gltf_model.h"

namespace gltf_model_detail
{
    // これは gltf_model.cpp の分割に伴う内部事情であり、外部から使うものではない。
    bool CreateTextureWithMipChain(ID3D11Device* device,
        const std::vector<uint8_t>& top_level_rgba, UINT width, UINT height,
        ID3D11ShaderResourceView** out_view);
}
