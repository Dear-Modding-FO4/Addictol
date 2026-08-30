// Gaussian blur after Unrimp by Christian Ofenberg, MIT.

#include <DearModdingUI/BackgroundBlur.h>
#include <DearModdingUI/HostSettings.h>
#include <Core/AdUtils.h>

#include <REX/REX.h>

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Addictol::DearModdingUI::BackgroundBlur
{
	using namespace std::literals;

	namespace
	{
		template <class T>
		using ComPtr = Microsoft::WRL::ComPtr<T>;

		inline constexpr UINT kDownsampleFactor{ 8 };
		inline constexpr int kBlurSampleCount{ 9 };
		inline constexpr float kBlurIntensity{ 0.30f };
		inline constexpr float kScissorPadding{ 2.0f };
		inline constexpr UINT kFullscreenVertexCount{ 3 };
		inline constexpr size_t kClassInstanceCapacity{ 256 };

		struct BlurConstants
		{
			float texelSize[4]{};
			int blurParameters[4]{};
		};

		struct WindowConstants
		{
			float windowRect[4]{};
			float windowParameters[4]{};
		};

		struct Region
		{
			float minX{ 0.0f };
			float minY{ 0.0f };
			float maxX{ 0.0f };
			float maxY{ 0.0f };
			float rounding{ 0.0f };
			bool valid{ false };
		};

		struct Resources
		{
			ID3D11Device* device{ nullptr };
			bool initializationAttempted{ false };
			bool available{ false };
			ComPtr<ID3D11VertexShader> vertexShader;
			ComPtr<ID3D11PixelShader> downsampleShader;
			ComPtr<ID3D11PixelShader> horizontalShader;
			ComPtr<ID3D11PixelShader> verticalShader;
			ComPtr<ID3D11PixelShader> compositeShader;
			ComPtr<ID3D11Buffer> blurConstants;
			ComPtr<ID3D11Buffer> windowConstants;
			ComPtr<ID3D11SamplerState> sampler;
			ComPtr<ID3D11BlendState> blend;
			ComPtr<ID3D11DepthStencilState> depthStencil;
			ComPtr<ID3D11RasterizerState> rasterizer;
			ComPtr<ID3D11Texture2D> sourceCopy;
			ComPtr<ID3D11ShaderResourceView> sourceView;
			ComPtr<ID3D11Texture2D> downsampleTexture;
			ComPtr<ID3D11RenderTargetView> downsampleTarget;
			ComPtr<ID3D11ShaderResourceView> downsampleView;
			std::array<ComPtr<ID3D11Texture2D>, 2> blurTextures;
			std::array<ComPtr<ID3D11RenderTargetView>, 2> blurTargets;
			std::array<ComPtr<ID3D11ShaderResourceView>, 2> blurViews;
			UINT width{ 0 };
			UINT height{ 0 };
			UINT downsampledWidth{ 0 };
			UINT downsampledHeight{ 0 };
			DXGI_FORMAT resourceFormat{ DXGI_FORMAT_UNKNOWN };
			DXGI_FORMAT viewFormat{ DXGI_FORMAT_UNKNOWN };
			bool frameFailureLogged{ false };
		};

		Region g_region;
		Resources g_resources;

		[[nodiscard]] std::filesystem::path ShaderPath(std::wstring_view a_file)
		{
			auto path = std::filesystem::path{ AdGetRuntimeDirectory() };
			path /= L"Data\\F4SE\\Plugins\\DearModdingUI\\Shaders";
			path /= a_file;
			return path;
		}

		[[nodiscard]] DXGI_FORMAT ResolveViewFormat(DXGI_FORMAT a_format) noexcept
		{
			switch (a_format)
			{
			case DXGI_FORMAT_R8G8B8A8_TYPELESS:
				return DXGI_FORMAT_R8G8B8A8_UNORM;
			case DXGI_FORMAT_B8G8R8A8_TYPELESS:
				return DXGI_FORMAT_B8G8R8A8_UNORM;
			case DXGI_FORMAT_B8G8R8X8_TYPELESS:
				return DXGI_FORMAT_B8G8R8X8_UNORM;
			case DXGI_FORMAT_R16G16B16A16_TYPELESS:
				return DXGI_FORMAT_R16G16B16A16_FLOAT;
			default:
				return a_format;
			}
		}

		template <class Shader>
		[[nodiscard]] bool CompileShader(
			ID3D11Device* a_device,
			std::wstring_view a_file,
			const char* a_entry,
			const char* a_target,
			ComPtr<Shader>& a_shader) noexcept
		{
			const auto path = ShaderPath(a_file);
			ComPtr<ID3DBlob> bytecode;
			ComPtr<ID3DBlob> errors;
			const auto result = D3DCompileFromFile(
				path.c_str(),
				nullptr,
				D3D_COMPILE_STANDARD_FILE_INCLUDE,
				a_entry,
				a_target,
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0,
				bytecode.GetAddressOf(),
				errors.GetAddressOf());
			if (FAILED(result) || !bytecode)
			{
				const auto detail = errors && errors->GetBufferPointer() ?
					std::string_view{
						static_cast<const char*>(errors->GetBufferPointer()),
						errors->GetBufferSize()
					} :
					"shader file missing or unreadable"sv;
				REX::WARN("DearModdingUI blur: {} ({})"sv, path.string(), detail);
				return false;
			}

			HRESULT created = E_FAIL;
			if constexpr (std::is_same_v<Shader, ID3D11VertexShader>)
			{
				created = a_device->CreateVertexShader(
					bytecode->GetBufferPointer(),
					bytecode->GetBufferSize(),
					nullptr,
					a_shader.ReleaseAndGetAddressOf());
			}
			else
			{
				created = a_device->CreatePixelShader(
					bytecode->GetBufferPointer(),
					bytecode->GetBufferSize(),
					nullptr,
					a_shader.ReleaseAndGetAddressOf());
			}
			return SUCCEEDED(created) && a_shader;
		}

		[[nodiscard]] bool CreateFixedResources(ID3D11Device* a_device) noexcept
		{
			auto& resources = g_resources;
			if (!CompileShader(
					a_device,
					L"BackgroundBlurDownsample.hlsl",
					"VS_Main",
					"vs_5_0",
					resources.vertexShader) ||
				!CompileShader(
					a_device,
					L"BackgroundBlurDownsample.hlsl",
					"PS_Main",
					"ps_5_0",
					resources.downsampleShader) ||
				!CompileShader(
					a_device,
					L"BackgroundBlurHorizontal.hlsl",
					"PS_Main",
					"ps_5_0",
					resources.horizontalShader) ||
				!CompileShader(
					a_device,
					L"BackgroundBlurVertical.hlsl",
					"PS_Main",
					"ps_5_0",
					resources.verticalShader) ||
				!CompileShader(
					a_device,
					L"BackgroundBlurComposite.hlsl",
					"PS_Main",
					"ps_5_0",
					resources.compositeShader))
				return false;

			D3D11_BUFFER_DESC bufferDescription{};
			bufferDescription.Usage = D3D11_USAGE_DEFAULT;
			bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			bufferDescription.ByteWidth = sizeof(BlurConstants);
			if (FAILED(a_device->CreateBuffer(
					&bufferDescription,
					nullptr,
					resources.blurConstants.GetAddressOf())))
				return false;
			bufferDescription.ByteWidth = sizeof(WindowConstants);
			if (FAILED(a_device->CreateBuffer(
					&bufferDescription,
					nullptr,
					resources.windowConstants.GetAddressOf())))
				return false;

			D3D11_SAMPLER_DESC samplerDescription{};
			samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
			if (FAILED(a_device->CreateSamplerState(
					&samplerDescription,
					resources.sampler.GetAddressOf())))
				return false;

			D3D11_BLEND_DESC blendDescription{};
			auto& renderTarget = blendDescription.RenderTarget[0];
			renderTarget.BlendEnable = TRUE;
			renderTarget.SrcBlend = D3D11_BLEND_SRC_ALPHA;
			renderTarget.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			renderTarget.BlendOp = D3D11_BLEND_OP_ADD;
			renderTarget.SrcBlendAlpha = D3D11_BLEND_ONE;
			renderTarget.DestBlendAlpha = D3D11_BLEND_ZERO;
			renderTarget.BlendOpAlpha = D3D11_BLEND_OP_ADD;
			renderTarget.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			if (FAILED(a_device->CreateBlendState(
					&blendDescription,
					resources.blend.GetAddressOf())))
				return false;

			D3D11_DEPTH_STENCIL_DESC depthDescription{};
			depthDescription.DepthEnable = FALSE;
			depthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			depthDescription.DepthFunc = D3D11_COMPARISON_ALWAYS;
			if (FAILED(a_device->CreateDepthStencilState(
					&depthDescription,
					resources.depthStencil.GetAddressOf())))
				return false;

			D3D11_RASTERIZER_DESC rasterizerDescription{};
			rasterizerDescription.FillMode = D3D11_FILL_SOLID;
			rasterizerDescription.CullMode = D3D11_CULL_NONE;
			rasterizerDescription.DepthClipEnable = TRUE;
			rasterizerDescription.ScissorEnable = TRUE;
			return SUCCEEDED(a_device->CreateRasterizerState(
				&rasterizerDescription,
				resources.rasterizer.GetAddressOf()));
		}

		[[nodiscard]] bool EnsureInitialized(ID3D11Device* a_device) noexcept
		{
			auto& resources = g_resources;
			if (resources.device != a_device)
			{
				ResetDeviceResources();
				resources.device = a_device;
			}
			if (resources.initializationAttempted)
				return resources.available;
			resources.initializationAttempted = true;
			resources.available = CreateFixedResources(a_device);
			if (!resources.available)
				REX::WARN("DearModdingUI: background blur is unavailable; the menu remains usable"sv);
			return resources.available;
		}

		void ReleaseFrameTextures() noexcept
		{
			auto& resources = g_resources;
			resources.sourceCopy.Reset();
			resources.sourceView.Reset();
			resources.downsampleTexture.Reset();
			resources.downsampleTarget.Reset();
			resources.downsampleView.Reset();
			for (auto& value : resources.blurTextures)
				value.Reset();
			for (auto& value : resources.blurTargets)
				value.Reset();
			for (auto& value : resources.blurViews)
				value.Reset();
			resources.width = 0;
			resources.height = 0;
			resources.downsampledWidth = 0;
			resources.downsampledHeight = 0;
			resources.resourceFormat = DXGI_FORMAT_UNKNOWN;
			resources.viewFormat = DXGI_FORMAT_UNKNOWN;
		}

		[[nodiscard]] bool CreateTextureSet(
			ID3D11Device* a_device,
			const D3D11_TEXTURE2D_DESC& a_description,
			ComPtr<ID3D11Texture2D>& a_texture,
			ComPtr<ID3D11RenderTargetView>& a_target,
			ComPtr<ID3D11ShaderResourceView>& a_view) noexcept
		{
			return SUCCEEDED(a_device->CreateTexture2D(
					   &a_description,
					   nullptr,
					   a_texture.ReleaseAndGetAddressOf())) &&
				SUCCEEDED(a_device->CreateRenderTargetView(
					a_texture.Get(),
					nullptr,
					a_target.ReleaseAndGetAddressOf())) &&
				SUCCEEDED(a_device->CreateShaderResourceView(
					a_texture.Get(),
					nullptr,
					a_view.ReleaseAndGetAddressOf()));
		}

		[[nodiscard]] bool CreateFrameTextures(
			ID3D11Device* a_device,
			const D3D11_TEXTURE2D_DESC& a_backBufferDescription) noexcept
		{
			auto& resources = g_resources;
			const auto viewFormat = ResolveViewFormat(a_backBufferDescription.Format);
			if (resources.width == a_backBufferDescription.Width &&
				resources.height == a_backBufferDescription.Height &&
				resources.resourceFormat == a_backBufferDescription.Format &&
				resources.viewFormat == viewFormat &&
				resources.sourceView &&
				resources.blurViews[1])
				return true;

			ReleaseFrameTextures();
			D3D11_TEXTURE2D_DESC copyDescription = a_backBufferDescription;
			copyDescription.MipLevels = 1;
			copyDescription.ArraySize = 1;
			copyDescription.SampleDesc.Count = 1;
			copyDescription.SampleDesc.Quality = 0;
			copyDescription.Usage = D3D11_USAGE_DEFAULT;
			copyDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			copyDescription.CPUAccessFlags = 0;
			copyDescription.MiscFlags = 0;
			if (FAILED(a_device->CreateTexture2D(
					&copyDescription,
					nullptr,
					resources.sourceCopy.GetAddressOf())))
				return false;

			D3D11_SHADER_RESOURCE_VIEW_DESC sourceViewDescription{};
			sourceViewDescription.Format = viewFormat;
			sourceViewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			sourceViewDescription.Texture2D.MipLevels = 1;
			if (FAILED(a_device->CreateShaderResourceView(
					resources.sourceCopy.Get(),
					&sourceViewDescription,
					resources.sourceView.GetAddressOf())))
				return false;

			resources.downsampledWidth =
				(std::max)(1u, a_backBufferDescription.Width / kDownsampleFactor);
			resources.downsampledHeight =
				(std::max)(1u, a_backBufferDescription.Height / kDownsampleFactor);
			D3D11_TEXTURE2D_DESC blurDescription{};
			blurDescription.Width = resources.downsampledWidth;
			blurDescription.Height = resources.downsampledHeight;
			blurDescription.MipLevels = 1;
			blurDescription.ArraySize = 1;
			blurDescription.Format = viewFormat;
			blurDescription.SampleDesc.Count = 1;
			blurDescription.Usage = D3D11_USAGE_DEFAULT;
			blurDescription.BindFlags =
				D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			if (!CreateTextureSet(
					a_device,
					blurDescription,
					resources.downsampleTexture,
					resources.downsampleTarget,
					resources.downsampleView) ||
				!CreateTextureSet(
					a_device,
					blurDescription,
					resources.blurTextures[0],
					resources.blurTargets[0],
					resources.blurViews[0]) ||
				!CreateTextureSet(
					a_device,
					blurDescription,
					resources.blurTextures[1],
					resources.blurTargets[1],
					resources.blurViews[1]))
			{
				ReleaseFrameTextures();
				return false;
			}

			resources.width = a_backBufferDescription.Width;
			resources.height = a_backBufferDescription.Height;
			resources.resourceFormat = a_backBufferDescription.Format;
			resources.viewFormat = viewFormat;
			resources.frameFailureLogged = false;
			return true;
		}

		template <class Shader>
		struct ShaderState
		{
			ComPtr<Shader> shader;
			std::array<ID3D11ClassInstance*, kClassInstanceCapacity> instances{};
			UINT count{ static_cast<UINT>(instances.size()) };

			void ReleaseInstances() noexcept
			{
				for (UINT index = 0; index < count; ++index)
				{
					if (instances[index])
						instances[index]->Release();
				}
			}
		};

		class PipelineStateScope
		{
		public:
			explicit PipelineStateScope(ID3D11DeviceContext* a_context) noexcept :
				m_context(a_context)
			{
				m_context->OMGetRenderTargets(
					static_cast<UINT>(m_renderTargets.size()),
					m_renderTargets.data(),
					m_depthStencilView.GetAddressOf());
				m_context->OMGetBlendState(
					m_blend.GetAddressOf(),
					m_blendFactor.data(),
					&m_sampleMask);
				m_context->OMGetDepthStencilState(
					m_depthStencil.GetAddressOf(),
					&m_stencilReference);
				m_context->RSGetState(m_rasterizer.GetAddressOf());
				m_context->RSGetViewports(&m_viewportCount, m_viewports.data());
				m_context->RSGetScissorRects(&m_scissorCount, m_scissors.data());
				m_context->IAGetInputLayout(m_inputLayout.GetAddressOf());
				m_context->IAGetPrimitiveTopology(&m_topology);
				m_context->VSGetShader(
					m_vertex.shader.GetAddressOf(),
					m_vertex.instances.data(),
					&m_vertex.count);
				m_context->PSGetShader(
					m_pixel.shader.GetAddressOf(),
					m_pixel.instances.data(),
					&m_pixel.count);
				m_context->GSGetShader(
					m_geometry.shader.GetAddressOf(),
					m_geometry.instances.data(),
					&m_geometry.count);
				m_context->HSGetShader(
					m_hull.shader.GetAddressOf(),
					m_hull.instances.data(),
					&m_hull.count);
				m_context->DSGetShader(
					m_domain.shader.GetAddressOf(),
					m_domain.instances.data(),
					&m_domain.count);

				ID3D11SamplerState* sampler{ nullptr };
				m_context->PSGetSamplers(0, 1, &sampler);
				m_sampler.Attach(sampler);
				std::array<ID3D11Buffer*, 2> constants{};
				m_context->PSGetConstantBuffers(
					0,
					static_cast<UINT>(constants.size()),
					constants.data());
				for (size_t index = 0; index < constants.size(); ++index)
					m_constantBuffers[index].Attach(constants[index]);
				ID3D11ShaderResourceView* resource{ nullptr };
				m_context->PSGetShaderResources(0, 1, &resource);
				m_shaderResource.Attach(resource);
			}

			~PipelineStateScope() noexcept
			{
				std::array<ID3D11Buffer*, 2> constants{
					m_constantBuffers[0].Get(),
					m_constantBuffers[1].Get()
				};
				auto* sampler = m_sampler.Get();
				auto* resource = m_shaderResource.Get();
				m_context->OMSetRenderTargets(
					static_cast<UINT>(m_renderTargets.size()),
					m_renderTargets.data(),
					m_depthStencilView.Get());
				m_context->OMSetBlendState(
					m_blend.Get(),
					m_blendFactor.data(),
					m_sampleMask);
				m_context->OMSetDepthStencilState(
					m_depthStencil.Get(),
					m_stencilReference);
				m_context->RSSetState(m_rasterizer.Get());
				m_context->RSSetViewports(
					m_viewportCount,
					m_viewportCount ? m_viewports.data() : nullptr);
				m_context->RSSetScissorRects(
					m_scissorCount,
					m_scissorCount ? m_scissors.data() : nullptr);
				m_context->IASetInputLayout(m_inputLayout.Get());
				m_context->IASetPrimitiveTopology(m_topology);
				m_context->VSSetShader(
					m_vertex.shader.Get(),
					m_vertex.instances.data(),
					m_vertex.count);
				m_context->PSSetShader(
					m_pixel.shader.Get(),
					m_pixel.instances.data(),
					m_pixel.count);
				m_context->GSSetShader(
					m_geometry.shader.Get(),
					m_geometry.instances.data(),
					m_geometry.count);
				m_context->HSSetShader(
					m_hull.shader.Get(),
					m_hull.instances.data(),
					m_hull.count);
				m_context->DSSetShader(
					m_domain.shader.Get(),
					m_domain.instances.data(),
					m_domain.count);
				m_context->PSSetSamplers(0, 1, &sampler);
				m_context->PSSetConstantBuffers(
					0,
					static_cast<UINT>(constants.size()),
					constants.data());
				m_context->PSSetShaderResources(0, 1, &resource);
				for (auto* target : m_renderTargets)
				{
					if (target)
						target->Release();
				}
				m_vertex.ReleaseInstances();
				m_pixel.ReleaseInstances();
				m_geometry.ReleaseInstances();
				m_hull.ReleaseInstances();
				m_domain.ReleaseInstances();
			}

			PipelineStateScope(const PipelineStateScope&) = delete;
			PipelineStateScope& operator=(const PipelineStateScope&) = delete;

		private:
			ID3D11DeviceContext* m_context;
			std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT>
				m_renderTargets{};
			ComPtr<ID3D11DepthStencilView> m_depthStencilView;
			ComPtr<ID3D11BlendState> m_blend;
			std::array<float, 4> m_blendFactor{};
			UINT m_sampleMask{ 0 };
			ComPtr<ID3D11DepthStencilState> m_depthStencil;
			UINT m_stencilReference{ 0 };
			ComPtr<ID3D11RasterizerState> m_rasterizer;
			std::array<
				D3D11_VIEWPORT,
				D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE>
				m_viewports{};
			UINT m_viewportCount{ static_cast<UINT>(m_viewports.size()) };
			std::array<
				D3D11_RECT,
				D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE>
				m_scissors{};
			UINT m_scissorCount{ static_cast<UINT>(m_scissors.size()) };
			ComPtr<ID3D11InputLayout> m_inputLayout;
			D3D11_PRIMITIVE_TOPOLOGY m_topology{
				D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED
			};
			ShaderState<ID3D11VertexShader> m_vertex;
			ShaderState<ID3D11PixelShader> m_pixel;
			ShaderState<ID3D11GeometryShader> m_geometry;
			ShaderState<ID3D11HullShader> m_hull;
			ShaderState<ID3D11DomainShader> m_domain;
			ComPtr<ID3D11SamplerState> m_sampler;
			std::array<ComPtr<ID3D11Buffer>, 2> m_constantBuffers;
			ComPtr<ID3D11ShaderResourceView> m_shaderResource;
		};

		void DrawFullscreen(
			ID3D11DeviceContext* a_context,
			ID3D11RenderTargetView* a_target,
			ID3D11PixelShader* a_shader,
			ID3D11ShaderResourceView* a_source) noexcept
		{
			ID3D11ShaderResourceView* nullResource{ nullptr };
			a_context->OMSetRenderTargets(1, &a_target, nullptr);
			a_context->PSSetShader(a_shader, nullptr, 0);
			a_context->PSSetShaderResources(0, 1, &a_source);
			a_context->Draw(kFullscreenVertexCount, 0);
			a_context->PSSetShaderResources(0, 1, &nullResource);
		}

		void PerformBlur(
			ID3D11DeviceContext* a_context,
			const D3D11_TEXTURE2D_DESC& a_description,
			ID3D11RenderTargetView* a_target) noexcept
		{
			auto& resources = g_resources;
			const PipelineStateScope previousState{ a_context };
			const std::array<float, 4> blendFactor{};
			auto* sampler = resources.sampler.Get();
			auto* blurBuffer = resources.blurConstants.Get();
			a_context->IASetInputLayout(nullptr);
			a_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			a_context->VSSetShader(resources.vertexShader.Get(), nullptr, 0);
			a_context->GSSetShader(nullptr, nullptr, 0);
			a_context->HSSetShader(nullptr, nullptr, 0);
			a_context->DSSetShader(nullptr, nullptr, 0);
			a_context->OMSetBlendState(nullptr, blendFactor.data(), 0xFFFFFFFF);
			a_context->OMSetDepthStencilState(resources.depthStencil.Get(), 0);
			a_context->RSSetState(resources.rasterizer.Get());
			a_context->PSSetSamplers(0, 1, &sampler);
			a_context->PSSetConstantBuffers(0, 1, &blurBuffer);

			const D3D11_VIEWPORT blurViewport{
				0.0f,
				0.0f,
				static_cast<float>(resources.downsampledWidth),
				static_cast<float>(resources.downsampledHeight),
				0.0f,
				1.0f
			};
			a_context->RSSetViewports(1, &blurViewport);
			const D3D11_RECT blurScissor{
				0,
				0,
				static_cast<LONG>(resources.downsampledWidth),
				static_cast<LONG>(resources.downsampledHeight)
			};
			a_context->RSSetScissorRects(1, &blurScissor);

			BlurConstants downsampleConstants{};
			downsampleConstants.texelSize[0] =
				1.0f / static_cast<float>(a_description.Width);
			downsampleConstants.texelSize[1] =
				1.0f / static_cast<float>(a_description.Height);
			downsampleConstants.texelSize[3] =
				static_cast<float>(kDownsampleFactor);
			a_context->UpdateSubresource(
				resources.blurConstants.Get(),
				0,
				nullptr,
				&downsampleConstants,
				0,
				0);
			DrawFullscreen(
				a_context,
				resources.downsampleTarget.Get(),
				resources.downsampleShader.Get(),
				resources.sourceView.Get());

			BlurConstants blurConstants{};
			blurConstants.texelSize[0] =
				kBlurIntensity / static_cast<float>(resources.downsampledWidth);
			blurConstants.texelSize[1] =
				kBlurIntensity / static_cast<float>(resources.downsampledHeight);
			blurConstants.texelSize[3] = static_cast<float>(kDownsampleFactor);
			blurConstants.blurParameters[0] = kBlurSampleCount;
			a_context->UpdateSubresource(
				resources.blurConstants.Get(),
				0,
				nullptr,
				&blurConstants,
				0,
				0);
			DrawFullscreen(
				a_context,
				resources.blurTargets[0].Get(),
				resources.horizontalShader.Get(),
				resources.downsampleView.Get());
			DrawFullscreen(
				a_context,
				resources.blurTargets[1].Get(),
				resources.verticalShader.Get(),
				resources.blurViews[0].Get());

			const D3D11_VIEWPORT targetViewport{
				0.0f,
				0.0f,
				static_cast<float>(a_description.Width),
				static_cast<float>(a_description.Height),
				0.0f,
				1.0f
			};
			a_context->RSSetViewports(1, &targetViewport);
			const D3D11_RECT targetScissor{
				static_cast<LONG>((std::max)(0.0f, g_region.minX - kScissorPadding)),
				static_cast<LONG>((std::max)(0.0f, g_region.minY - kScissorPadding)),
				static_cast<LONG>((std::min)(
					static_cast<float>(a_description.Width),
					g_region.maxX + kScissorPadding)),
				static_cast<LONG>((std::min)(
					static_cast<float>(a_description.Height),
					g_region.maxY + kScissorPadding))
			};
			a_context->RSSetScissorRects(1, &targetScissor);

			WindowConstants windowConstants{};
			windowConstants.windowRect[0] = g_region.minX;
			windowConstants.windowRect[1] = g_region.minY;
			windowConstants.windowRect[2] = g_region.maxX;
			windowConstants.windowRect[3] = g_region.maxY;
			windowConstants.windowParameters[0] = g_region.rounding;
			windowConstants.windowParameters[1] =
				static_cast<float>(a_description.Width);
			windowConstants.windowParameters[2] =
				static_cast<float>(a_description.Height);
			a_context->UpdateSubresource(
				resources.windowConstants.Get(),
				0,
				nullptr,
				&windowConstants,
				0,
				0);
			auto* windowBuffer = resources.windowConstants.Get();
			a_context->PSSetConstantBuffers(1, 1, &windowBuffer);
			a_context->OMSetBlendState(resources.blend.Get(), nullptr, 0xFFFFFFFF);
			DrawFullscreen(
				a_context,
				a_target,
				resources.compositeShader.Get(),
				resources.blurViews[1].Get());
		}
	}

	void BeginFrame() noexcept
	{
		g_region = {};
	}

	void SetHostWindow(
		float a_minX,
		float a_minY,
		float a_maxX,
		float a_maxY,
		float a_rounding) noexcept
	{
		g_region = {
			a_minX,
			a_minY,
			a_maxX,
			a_maxY,
			(std::max)(0.0f, a_rounding),
			a_maxX > a_minX && a_maxY > a_minY
		};
	}

	void InvalidateBackBuffer() noexcept
	{
		ReleaseFrameTextures();
	}

	void ResetDeviceResources() noexcept
	{
		g_resources = {};
		g_region = {};
	}

	void Render(
		ID3D11Device* a_device,
		ID3D11DeviceContext* a_context,
		ID3D11Texture2D* a_backBuffer,
		ID3D11RenderTargetView* a_backBufferView) noexcept
	{
		if (!HostSettings::Current().backgroundBlur ||
			!g_region.valid ||
			!a_device ||
			!a_context ||
			!a_backBuffer ||
			!a_backBufferView ||
			!EnsureInitialized(a_device))
			return;

		D3D11_TEXTURE2D_DESC description{};
		a_backBuffer->GetDesc(&description);
		if (!description.Width ||
			!description.Height ||
			!CreateFrameTextures(a_device, description))
		{
			if (!std::exchange(g_resources.frameFailureLogged, true))
				REX::WARN("DearModdingUI: background blur frame resources are unavailable"sv);
			ReleaseFrameTextures();
			return;
		}

		g_region.minX = std::clamp(
			g_region.minX, 0.0f, static_cast<float>(description.Width));
		g_region.minY = std::clamp(
			g_region.minY, 0.0f, static_cast<float>(description.Height));
		g_region.maxX = std::clamp(
			g_region.maxX, 0.0f, static_cast<float>(description.Width));
		g_region.maxY = std::clamp(
			g_region.maxY, 0.0f, static_cast<float>(description.Height));
		if (g_region.maxX <= g_region.minX || g_region.maxY <= g_region.minY)
			return;

		if (description.SampleDesc.Count > 1)
		{
			a_context->ResolveSubresource(
				g_resources.sourceCopy.Get(),
				0,
				a_backBuffer,
				0,
				g_resources.viewFormat);
		}
		else
		{
			a_context->CopyResource(g_resources.sourceCopy.Get(), a_backBuffer);
		}
		PerformBlur(a_context, description, a_backBufferView);
	}
}
