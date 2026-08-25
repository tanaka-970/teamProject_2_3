#pragma once

#include <d3d12.h>

#include <cstdint>
#include <cwchar>
#include <string>

namespace ReplayEngine::Rendering::DX12
{
    inline HRESULT SetD3D12ObjectName(ID3D12Object* object,
        const wchar_t* purpose, const wchar_t* key, std::uint64_t slot) noexcept
    {
        if (object == nullptr || purpose == nullptr || key == nullptr) return E_INVALIDARG;
        wchar_t name[512]{};
        _snwprintf_s(name, _countof(name), _TRUNCATE, L"%ls:%ls:slot%llu",
            purpose, key, static_cast<unsigned long long>(slot));
        return object->SetName(name);
    }

    inline HRESULT SetD3D12ObjectName(ID3D12Object* object,
        const wchar_t* purpose, const wchar_t* key) noexcept
    {
        if (object == nullptr || purpose == nullptr || key == nullptr) return E_INVALIDARG;
        wchar_t name[512]{};
        _snwprintf_s(name, _countof(name), _TRUNCATE, L"%ls:%ls", purpose, key);
        return object->SetName(name);
    }
}
