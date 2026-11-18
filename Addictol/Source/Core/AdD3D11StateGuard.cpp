#include <Core/AdD3D11StateGuard.h>

namespace Addictol
{
	// The engine binds no class instances, so none are saved.
	D3D11StateGuard::D3D11StateGuard(REX::W32::ID3D11DeviceContext* a_context) noexcept :
		m_context(a_context)
	{
		m_context->IAGetPrimitiveTopology(&m_topology);
		m_context->IAGetInputLayout(&m_inputLayout);
		m_context->VSGetShader(&m_vertexShader, nullptr, nullptr);
		m_context->HSGetShader(&m_hullShader, nullptr, nullptr);
		m_context->DSGetShader(&m_domainShader, nullptr, nullptr);
		m_context->GSGetShader(&m_geometryShader, nullptr, nullptr);
		m_context->PSGetShader(&m_pixelShader, nullptr, nullptr);

		m_context->PSGetShaderResources(0, s_pixelShaderResources, m_pixelResources[0].GetAddressOf());
		m_context->PSGetSamplers(0, s_pixelSamplers, m_pixelSamplers[0].GetAddressOf());
		m_context->PSGetConstantBuffers(0, s_pixelConstantBuffers, m_pixelConstantBuffers[0].GetAddressOf());

		m_context->RSGetState(&m_rasterizer);
		m_context->RSGetViewports(&m_viewportCount, m_viewports);

		m_context->OMGetRenderTargets(REX::W32::D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, m_renderTargets[0].GetAddressOf(), &m_depthStencilView);
		m_context->OMGetBlendState(&m_blend, m_blendFactor, &m_sampleMask);
		m_context->OMGetDepthStencilState(&m_depthStencil, &m_stencilRef);
	}

	D3D11StateGuard::~D3D11StateGuard() noexcept
	{
		m_context->OMSetRenderTargets(REX::W32::D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, m_renderTargets[0].GetAddressOf(), m_depthStencilView.Get());
		m_context->OMSetBlendState(m_blend.Get(), m_blendFactor, m_sampleMask);
		m_context->OMSetDepthStencilState(m_depthStencil.Get(), m_stencilRef);

		m_context->RSSetState(m_rasterizer.Get());
		m_context->RSSetViewports(m_viewportCount, m_viewports);

		m_context->PSSetShaderResources(0, s_pixelShaderResources, m_pixelResources[0].GetAddressOf());
		m_context->PSSetSamplers(0, s_pixelSamplers, m_pixelSamplers[0].GetAddressOf());
		m_context->PSSetConstantBuffers(0, s_pixelConstantBuffers, m_pixelConstantBuffers[0].GetAddressOf());

		m_context->IASetPrimitiveTopology(m_topology);
		m_context->IASetInputLayout(m_inputLayout.Get());
		m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
		m_context->HSSetShader(m_hullShader.Get(), nullptr, 0);
		m_context->DSSetShader(m_domainShader.Get(), nullptr, 0);
		m_context->GSSetShader(m_geometryShader.Get(), nullptr, 0);
		m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
	}
}
