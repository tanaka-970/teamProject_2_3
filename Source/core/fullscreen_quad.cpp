#include"fullscreen_quad.h"
#include"shader.h"
#include"misc.h"

#include "../../RePlayEngine/Rendering/RenderStats.h"

fullscreen_quad::fullscreen_quad(ID3D11Device* device)
{
	// 頂点シェーダーとピクセルシェーダーの生成
	create_vs_from_cso(device, "fullscreen_quad_vs.cso", embedded_vertex_shader.ReleaseAndGetAddressOf(),
		nullptr, nullptr, 0);
	create_ps_from_cso(device, "fullscreen_quad_ps.cso", embedded_pixel_shader.ReleaseAndGetAddressOf());
}
void fullscreen_quad::blit(ID3D11DeviceContext* immediate_context, 
	ID3D11ShaderResourceView** shader_resource_view,
	uint32_t start_slot, uint32_t num_views,
	ID3D11PixelShader* replaced_pixel_shader)
{
	// 4頂点のフルスクリーンクワッドを描画する//
	// 頂点バッファは不要なので nullptr を指定
	immediate_context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	immediate_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	immediate_context->IASetInputLayout(nullptr);
	immediate_context->VSSetShader(embedded_vertex_shader.Get(), 0, 0);
	replaced_pixel_shader ? immediate_context->PSSetShader(replaced_pixel_shader, 0, 0)
	:	immediate_context->PSSetShader(embedded_pixel_shader.Get(), 0, 0);


	immediate_context->PSSetShaderResources(start_slot, num_views, shader_resource_view);
	ReplayEngine::Rendering::Stats().CountDraw(4);
	immediate_context->Draw(4, 0);
}
