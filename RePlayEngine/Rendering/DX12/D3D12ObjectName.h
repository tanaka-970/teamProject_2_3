#pragma once

#include <d3d12.h>

#include <cstdint>
#include <cwchar>
#include <string>
#include <string_view>

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
    inline std::wstring D3D12ObjectNameWide(std::string_view text)
    {
        if (text.empty()) return {};
        const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(),
            static_cast<int>(text.size()), nullptr, 0);
        if (count <= 0) return {};
        std::wstring result(static_cast<std::size_t>(count), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
            result.data(), count);
        return result;
    }

    inline HRESULT SetD3D12ObjectName(ID3D12Object* object,
        const wchar_t* purpose) noexcept
    {
        if (object == nullptr || purpose == nullptr) return E_INVALIDARG;
        return object->SetName(purpose);
    }

    inline HRESULT SetD3D12ObjectNameUtf8(ID3D12Object* object,
        const wchar_t* purpose, std::string_view key) noexcept
    {
        if (object == nullptr || purpose == nullptr) return E_INVALIDARG;
        try
        {
            const std::wstring wide = D3D12ObjectNameWide(key);
            return SetD3D12ObjectName(object, purpose,
                wide.empty() ? L"unnamed" : wide.c_str());
        }
        catch (...)
        {
            return E_OUTOFMEMORY;
        }
    }

}
