#include<d3d11.h>
#include<wrl.h>
#include<cstdint>

class framebuffer
{
public:
    framebuffer(ID3D11Device* device, uint32_t width, uint32_t height);
    virtual ~framebuffer() = default;

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target_view;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_stencil_view;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shader_resource_views[2];
    D3D11_VIEWPORT viewport;

    void clear(ID3D11DeviceContext* immediate_context,
        float r = 0, float g = 0, float b = 0, float a = 1, float depth = 1);
    void activate(ID3D11DeviceContext* immediate_context);
    void deactivate(ID3D11DeviceContext* immediate_context);

private:
    UINT viewport_count{ D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE };
    D3D11_VIEWPORT cached_viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> cached_render_target_view;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> cached_depth_stencil_view;

};
