#include "framework.h"
#include "mainInternal.h"

#include "../../../RePlayEngine/Rendering/DX12/D3D12DeviceContext.h"
#include "../../../RePlayEngine/Rendering/DX12/D3D12DescriptorHeapAllocator.h"

#include <cstdio>
#include <cstring>

namespace ReplayEngine::Runtime::Detail
{
    namespace
    {
        constexpr wchar_t kValidationWindowClass[] =
            L"ReplayEngineDX12ValidationWindowClass";

        LRESULT CALLBACK ValidationWindowProcedure(HWND window, UINT message,
            WPARAM wparam, LPARAM lparam)
        {
            return DefWindowProcW(window, message, wparam, lparam);
        }

        HWND CreateValidationWindow(HINSTANCE instance) noexcept
        {
            WNDCLASSEXW window_class{};
            window_class.cbSize = sizeof(window_class);
            window_class.lpfnWndProc = ValidationWindowProcedure;
            window_class.hInstance = instance;
            window_class.lpszClassName = kValidationWindowClass;
            if (RegisterClassExW(&window_class) == 0 &&
                GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                return nullptr;

            return CreateWindowExW(0, kValidationWindowClass, L"DX12 validation",
                WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, instance,
                nullptr);
        }

        bool RunDeviceFrameValidation(
            Rendering::DX12::D3D12DeviceContext& context, const float color[4],
            const char* label, int& checks)
        {
            ++checks;
            if (!context.BeginFrame(color))
            {
                std::fprintf(stderr, "DX12 validation failed: %s BeginFrame\n", label);
                return false;
            }

            Rendering::DX12::D3D12FrameConstants frame_constants;
            ++checks;
            if (!context.SubmitFrameConstants(frame_constants))
            {
                std::fprintf(stderr, "DX12 validation failed: %s frame constants\n", label);
                return false;
            }

            Rendering::RenderItemList render_items;
            Rendering::RenderItem first_item;
            first_item.owner = Core::ObjectID{ 101 };
            first_item.mesh_asset = "builtin:validation";
            first_item.cast_shadow = true;
            render_items.Add(first_item);
            Rendering::RenderItem second_item;
            second_item.owner = Core::ObjectID{ 102 };
            second_item.mesh_asset = "builtin:validation";
            second_item.skinned = true;
            second_item.outline = true;
            render_items.Add(second_item);
            ++checks;
            if (!context.SubmitRenderItems(render_items) ||
                context.RenderItemBatch().Size() != render_items.Size() ||
                context.RenderItemBatch().GpuBuffer() == nullptr ||
                !context.RenderItemBatch().ShaderResourceAllocation().IsValid())
            {
                std::fprintf(stderr, "DX12 validation failed: %s render item upload\n", label);
                return false;
            }

            ++checks;
            if (!context.DrawValidationTriangle())
            {
                std::fprintf(stderr, "DX12 validation failed: %s triangle\n", label);
                return false;
            }

            ++checks;
            if (!context.EndFrame())
            {
                std::fprintf(stderr, "DX12 validation failed: %s EndFrame\n", label);
                return false;
            }
            context.WaitForGpu();
            return true;
        }
    }

    int RunHeadlessDX12Validation(const char* command_line)
    {
        if (command_line == nullptr ||
            std::strstr(command_line, "--validate-dx12-device") == nullptr)
            return -1;

        const HINSTANCE instance = GetModuleHandleW(nullptr);
        HWND window = CreateValidationWindow(instance);
        if (window == nullptr)
        {
            std::fprintf(stderr, "DX12 validation failed: CreateWindowExW error=%lu\n",
                static_cast<unsigned long>(GetLastError()));
            return 80;
        }

        Rendering::DX12::D3D12DeviceContext context;
        int checks = 0;
        bool ok = true;
        ok = context.Initialize(window, 64, 64, false, false);
        ++checks;
        if (!ok)
        {
            std::fprintf(stderr, "DX12 validation failed: Initialize\n");
        }
        else
        {
            ok = context.IsInitialized() && context.Device() != nullptr &&
                context.CommandQueue() != nullptr && context.CommandList() != nullptr;
            ++checks;
            if (!ok)
                std::fprintf(stderr, "DX12 validation failed: device objects\n");
        }

        if (ok)
        {
            Rendering::DX12::D3D12DescriptorAllocation descriptors{};
            ++checks;
            if (!context.ResourceDescriptorAllocator().Allocate(4, descriptors) ||
                context.ResourceDescriptorAllocator().Used() != 4)
            {
                std::fprintf(stderr, "DX12 validation failed: descriptor allocate\n");
                ok = false;
            }
            ++checks;
            if (ok && (!context.ResourceDescriptorAllocator().Free(descriptors) ||
                context.ResourceDescriptorAllocator().Used() != 0))
            {
                std::fprintf(stderr, "DX12 validation failed: descriptor free\n");
                ok = false;
            }
        }

        constexpr float first_color[] = { 0.08f, 0.12f, 0.20f, 1.0f };
        constexpr float second_color[] = { 0.20f, 0.10f, 0.06f, 1.0f };
        if (ok) ok = RunDeviceFrameValidation(context, first_color, "first frame", checks);

        if (ok)
        {
            ++checks;
            if (!context.Resize(96, 64))
            {
                std::fprintf(stderr, "DX12 validation failed: Resize\n");
                ok = false;
            }
        }
        if (ok) ok = RunDeviceFrameValidation(context, second_color, "resized frame", checks);
        if (context.IsInitialized()) context.WaitForGpu();
        context.Shutdown();
        DestroyWindow(window);
        UnregisterClassW(kValidationWindowClass, instance);

        if (!ok)
        {
            std::fprintf(stderr, "DX12 validation failed: %d checks\n", checks);
            return 81;
        }
        std::fprintf(stderr, "DX12 validation passed: %d checks\n", checks);
        return 0;
    }
}
