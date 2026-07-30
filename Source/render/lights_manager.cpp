#include "lights_manager.h"
#include "misc.h"

bool lights_manager::initialize(ID3D11Device* device)
{
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(lights_cb);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    return SUCCEEDED(device->CreateBuffer(&bd, nullptr, cb.GetAddressOf()));
}

void lights_manager::update_constants(ID3D11DeviceContext* ctx)
{
    ctx->UpdateSubresource(cb.Get(), 0, nullptr, &data, 0, 0);
    ctx->PSSetConstantBuffers(10, 1, cb.GetAddressOf());
}
