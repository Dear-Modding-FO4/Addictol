#pragma once

#include <REX/W32/D3D11.h>

#include <wrl/client.h>

namespace Addictol
{
	// Restores the pipeline state a small full-screen pass changes.
	class D3D11StateGuard
	{
	public:
		explicit D3D11StateGuard(REX::W32::ID3D11DeviceContext* a_context) noexcept;
		~D3D11StateGuard() noexcept;

		D3D11StateGuard(const D3D11StateGuard&) = delete;
		D3D11StateGuard(D3D11StateGuard&&) = delete;
		D3D11StateGuard& operator=(const D3D11StateGuard&) = delete;
		D3D11StateGuard& operator=(D3D11StateGuard&&) = delete;

	private:
		static constexpr uint32_t s_pixelShaderResources{ 8 };
		static constexpr uint32_t s_pixelSamplers{ 4 };
		static constexpr uint32_t s_pixelConstantBuffers{ 4 };

		template <class T>
		using ComPtr = Microsoft::WRL::ComPtr<T>;

		REX::W32::ID3D11DeviceContext* m_context;

		REX::W32::D3D11_PRIMITIVE_TOPOLOGY      m_topology{};
		ComPtr<REX::W32::ID3D11InputLayout>     m_inputLayout;
		ComPtr<REX::W32::ID3D11VertexShader>    m_vertexShader;
		ComPtr<REX::W32::ID3D11HullShader>      m_hullShader;
		ComPtr<REX::W32::ID3D11DomainShader>    m_domainShader;
		ComPtr<REX::W32::ID3D11GeometryShader>  m_geometryShader;
		ComPtr<REX::W32::ID3D11PixelShader>     m_pixelShader;

		ComPtr<REX::W32::ID3D11ShaderResourceView> m_pixelResources[s_pixelShaderResources];
		ComPtr<REX::W32::ID3D11SamplerState>       m_pixelSamplers[s_pixelSamplers];
		ComPtr<REX::W32::ID3D11Buffer>             m_pixelConstantBuffers[s_pixelConstantBuffers];

		ComPtr<REX::W32::ID3D11RasterizerState> m_rasterizer;
		REX::W32::D3D11_VIEWPORT                m_viewports[REX::W32::D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
		uint32_t                                m_viewportCount{ REX::W32::D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE };

		ComPtr<REX::W32::ID3D11RenderTargetView> m_renderTargets[REX::W32::D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
		ComPtr<REX::W32::ID3D11DepthStencilView> m_depthStencilView;
		ComPtr<REX::W32::ID3D11BlendState>       m_blend;
		float                                    m_blendFactor[4]{};
		uint32_t                                 m_sampleMask{};
		ComPtr<REX::W32::ID3D11DepthStencilState> m_depthStencil;
		uint32_t                                  m_stencilRef{};
	};
}
