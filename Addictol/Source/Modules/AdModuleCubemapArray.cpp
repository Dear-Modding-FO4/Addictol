#include <Modules/AdModuleCubemapArray.h>
#include <Core/AdUtils.h>

#include <RE/B/BSGraphics.h>
#include <RE/T/TLS.h>
#include <REX/W32/D3D11.h>
#include <REX/W32/D3DCOMPILER.h>

#include <wrl/client.h>
#include <Core/AdXbyak.h>

namespace Addictol
{
	// One odd first cube fixes the shared envmap array's layout for the session.
	namespace cubemapArrayDetail
	{
		namespace W32 = REX::W32;
		template <class T>
		using ComPtr = Microsoft::WRL::ComPtr<T>;

		// The Fix Cubemaps script's output layout; envmaps are created sRGB.
		static constexpr uint32_t s_size{ 128 };
		static constexpr uint32_t s_mipCount{ 8 };
		static constexpr auto s_format{ W32::DXGI_FORMAT_B8G8R8A8_UNORM_SRGB };
		static constexpr uint32_t s_faceCount{ 6 };
		static constexpr uint8_t s_cubeFlag{ 0x1 };
		static constexpr uint32_t s_maxTaps{ 8 };

		static REL::Relocation<RE::BSGraphics::Texture*(W32::D3D11_TEXTURE2D_DESC&, RE::BSGraphics::TextureHeader&)> CreateTextureArrayHelper{ RE::ID::BSGraphics::CreateTextureArrayHelper };

		static LogLimiter s_conversions{ 16, "Cubemap Array: further cube conversions will not be logged"sv };

		static constexpr char s_shaderSource[] = R"(
cbuffer Params : register(b0)
{
	uint  g_face;
	uint  g_mode;   // 0 samples the source cube, 1 halves the previous destination mip
	uint  g_taps;
	float g_lod;
	float g_texel;
	float3 g_pad;
};

TextureCube<float4>    g_source   : register(t0);
Texture2DArray<float4> g_previous : register(t1);
SamplerState           g_linear   : register(s0);

struct Interpolants
{
	float4 position : SV_Position;
	float2 uv       : TEXCOORD0;
};

Interpolants VSMain(uint a_id : SV_VertexID)
{
	Interpolants o;
	o.uv = float2((a_id << 1) & 2, a_id & 2);
	o.position = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
	return o;
}

// D3D face order: +X -X +Y -Y +Z -Z.
float3 FaceDirection(uint a_face, float2 a_uv)
{
	const float2 p = a_uv * 2.0 - 1.0;
	switch (a_face)
	{
	case 0:  return float3(1.0, -p.y, -p.x);
	case 1:  return float3(-1.0, -p.y, p.x);
	case 2:  return float3(p.x, 1.0, p.y);
	case 3:  return float3(p.x, -1.0, -p.y);
	case 4:  return float3(p.x, -p.y, 1.0);
	default: return float3(-p.x, -p.y, -1.0);
	}
}

float4 PSMain(Interpolants a_in) : SV_Target
{
	if (g_mode != 0)
		return g_previous.SampleLevel(g_linear, float3(a_in.uv, g_face), 0.0);

	float4 sum = 0.0;
	const float step = 1.0 / g_taps;
	for (uint y = 0; y < g_taps; ++y)
	{
		for (uint x = 0; x < g_taps; ++x)
		{
			const float2 offset = ((float2(x, y) + 0.5) * step - 0.5) * g_texel;
			sum += g_source.SampleLevel(g_linear, FaceDirection(g_face, a_in.uv + offset), g_lod);
		}
	}
	return sum / (g_taps * g_taps);
}
)";

		struct alignas(16) DrawParams
		{
			uint32_t face{ 0 };
			uint32_t mode{ 0 };
			uint32_t taps{ 1 };
			float    lod{ 0.0f };
			float    texel{ 0.0f };
			float    pad[3]{};
		};
		static_assert(sizeof(DrawParams) == 32);

		[[nodiscard]] static constexpr uint32_t Subresource(uint32_t a_mip, uint32_t a_slice, uint32_t a_mipLevels) noexcept
		{
			return a_mip + a_slice * a_mipLevels;
		}

		class CubeNormalizer
		{
		public:
			[[nodiscard]] bool Initialize(W32::ID3D11Device* a_device) noexcept
			{
				ComPtr<W32::ID3DBlob> vertexCode;
				ComPtr<W32::ID3DBlob> pixelCode;
				if (!Compile("VSMain", "vs_5_0", vertexCode) || !Compile("PSMain", "ps_5_0", pixelCode))
					return false;

				if (a_device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, &m_vertexShader) < 0 ||
					a_device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, &m_pixelShader) < 0)
				{
					REX::ERROR("Cubemap Array: failed to create the conversion shaders"sv);
					return false;
				}

				// Replayed with a full state restore so the engine's bindings survive.
				if (a_device->CreateDeferredContext(0, &m_recorder) < 0)
				{
					REX::ERROR("Cubemap Array: failed to create the conversion context"sv);
					return false;
				}

				// Drawing into the live array would make D3D unbind it under the engine.
				W32::D3D11_TEXTURE2D_DESC scratchDesc{};
				scratchDesc.width = s_size;
				scratchDesc.height = s_size;
				scratchDesc.mipLevels = s_mipCount;
				scratchDesc.arraySize = s_faceCount;
				scratchDesc.format = s_format;
				scratchDesc.sampleDesc.count = 1;
				scratchDesc.usage = W32::D3D11_USAGE_DEFAULT;
				scratchDesc.bindFlags = W32::D3D11_BIND_SHADER_RESOURCE | W32::D3D11_BIND_RENDER_TARGET;
				if (a_device->CreateTexture2D(&scratchDesc, nullptr, &m_scratch) < 0)
				{
					REX::ERROR("Cubemap Array: failed to create the conversion target"sv);
					return false;
				}

				for (uint32_t mip = 0; mip < s_mipCount; ++mip)
				{
					W32::D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
					viewDesc.format = s_format;
					viewDesc.viewDimension = W32::D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
					viewDesc.texture2DArray = { mip, 1, 0, s_faceCount };
					if (a_device->CreateShaderResourceView(m_scratch.Get(), &viewDesc, &m_mipViews[mip]) < 0)
					{
						REX::ERROR("Cubemap Array: failed to create a conversion mip view"sv);
						return false;
					}

					for (uint32_t face = 0; face < s_faceCount; ++face)
					{
						W32::D3D11_RENDER_TARGET_VIEW_DESC targetDesc{};
						targetDesc.format = s_format;
						targetDesc.viewDimension = W32::D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
						targetDesc.texture2DArray = { mip, face, 1 };
						if (a_device->CreateRenderTargetView(m_scratch.Get(), &targetDesc, &m_targets[face][mip]) < 0)
						{
							REX::ERROR("Cubemap Array: failed to create a conversion render target"sv);
							return false;
						}
					}
				}

				W32::D3D11_SAMPLER_DESC samplerDesc{};
				samplerDesc.filter = W32::D3D11_FILTER_MIN_MAG_MIP_LINEAR;
				samplerDesc.addressU = W32::D3D11_TEXTURE_ADDRESS_CLAMP;
				samplerDesc.addressV = W32::D3D11_TEXTURE_ADDRESS_CLAMP;
				samplerDesc.addressW = W32::D3D11_TEXTURE_ADDRESS_CLAMP;
				samplerDesc.maxAnisotropy = 1;
				samplerDesc.comparisonFunc = W32::D3D11_COMPARISON_NEVER;
				samplerDesc.maxLOD = W32::D3D11_FLOAT32_MAX;

				W32::D3D11_RASTERIZER_DESC rasterizerDesc{};
				rasterizerDesc.fillMode = W32::D3D11_FILL_SOLID;
				rasterizerDesc.cullMode = W32::D3D11_CULL_NONE;
				rasterizerDesc.depthClipEnable = true;

				W32::D3D11_BUFFER_DESC paramsDesc{};
				paramsDesc.byteWidth = sizeof(DrawParams);
				paramsDesc.usage = W32::D3D11_USAGE_DEFAULT;
				paramsDesc.bindFlags = W32::D3D11_BIND_CONSTANT_BUFFER;

				if (a_device->CreateSamplerState(&samplerDesc, &m_sampler) < 0 ||
					a_device->CreateRasterizerState(&rasterizerDesc, &m_rasterizer) < 0 ||
					a_device->CreateBuffer(&paramsDesc, nullptr, &m_params) < 0)
				{
					REX::ERROR("Cubemap Array: failed to create the conversion pipeline state"sv);
					return false;
				}

				return true;
			}

			void Copy(W32::ID3D11DeviceContext* a_context, const RE::BSGraphics::Texture& a_source, W32::ID3D11Texture2D* a_array, uint32_t a_firstFace) noexcept
			{
				W32::D3D11_TEXTURE2D_DESC desc{};
				a_source.texture->GetDesc(&desc);

				W32::D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
				a_source.srView->GetDesc(&viewDesc);
				if (viewDesc.viewDimension != W32::D3D11_SRV_DIMENSION_TEXTURECUBE || desc.arraySize != s_faceCount || desc.mipLevels == 0)
				{
					if (s_conversions.Take())
						REX::WARN("Cubemap Array: skipped a {}x{} source that is not a cube view"sv, desc.width, desc.height);
					return;
				}

				// Already the array's layout: copy with the source's own mip stride.
				if (desc.format == s_format && desc.width == s_size && desc.height == s_size && desc.mipLevels >= s_mipCount)
				{
					CopyFaces(a_context, a_array, a_firstFace, a_source.texture, desc.mipLevels);
					return;
				}

				if (s_conversions.Take())
					REX::INFO("Cubemap Array: converting a {}x{} cube with {} mips in DXGI format {}"sv, desc.width, desc.height, desc.mipLevels, std::to_underlying(desc.format));

				Render(a_source.srView, desc.width, desc.mipLevels);
				CopyFaces(m_recorder.Get(), a_array, a_firstFace, m_scratch.Get(), s_mipCount);

				ComPtr<W32::ID3D11CommandList> commands;
				if (m_recorder->FinishCommandList(false, &commands) < 0)
				{
					REX::ERROR("Cubemap Array: failed to record a cube conversion"sv);
					return;
				}
				a_context->ExecuteCommandList(commands.Get(), true);
			}

		private:
			[[nodiscard]] static bool Compile(const char* a_entry, const char* a_target, ComPtr<W32::ID3DBlob>& a_code) noexcept
			{
				ComPtr<W32::ID3DBlob> errors;
				const auto flags = static_cast<uint32_t>(W32::D3DCOMPILE_OPTIMIZATION_LEVEL3 | W32::D3DCOMPILE_ENABLE_STRICTNESS);
				const auto result = W32::D3DCompile(s_shaderSource, sizeof(s_shaderSource) - 1, "AddictolCubemapArray", nullptr, nullptr, a_entry, a_target,
					flags, 0, &a_code, &errors);
				if (result >= 0)
					return true;

				const auto* message = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no compiler output";
				REX::ERROR("Cubemap Array: failed to compile {} (0x{:08X}): {}"sv, a_entry, static_cast<uint32_t>(result), message);
				return false;
			}

			static void CopyFaces(W32::ID3D11DeviceContext* a_context, W32::ID3D11Texture2D* a_array, uint32_t a_firstFace, W32::ID3D11Texture2D* a_source, uint32_t a_sourceMips) noexcept
			{
				for (uint32_t face = 0; face < s_faceCount; ++face)
				{
					for (uint32_t mip = 0; mip < s_mipCount; ++mip)
					{
						a_context->CopySubresourceRegion(a_array, Subresource(mip, a_firstFace + face, s_mipCount), 0, 0, 0,
							a_source, Subresource(mip, face, a_sourceMips), nullptr);
					}
				}
			}

			// Same-size source mips are kept; others halve the mip above, like texconv.
			void Render(W32::ID3D11ShaderResourceView* a_source, uint32_t a_width, uint32_t a_sourceMips) noexcept
			{
				auto* const context = m_recorder.Get();
				context->IASetPrimitiveTopology(W32::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
				context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
				context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
				context->PSSetConstantBuffers(0, 1, m_params.GetAddressOf());
				context->RSSetState(m_rasterizer.Get());

				for (uint32_t mip = 0; mip < s_mipCount; ++mip)
				{
					const uint32_t size = s_size >> mip;
					const W32::D3D11_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(size), static_cast<float>(size), 0.0f, 1.0f };
					context->RSSetViewports(1, &viewport);

					DrawParams params{};
					params.texel = 1.0f / static_cast<float>(size);

					// Smallest source level at least this size; the top level when upsampling.
					uint32_t level = 0;
					while (level + 1 < a_sourceMips && std::max(a_width >> (level + 1), 1u) >= size)
						++level;

					const uint32_t levelWidth = std::max(a_width >> level, 1u);
					if (levelWidth == size || mip == 0)
					{
						params.lod = static_cast<float>(level);
						params.taps = std::clamp((levelWidth + size - 1) / size, 1u, s_maxTaps);
					}
					else
						params.mode = 1;

					W32::ID3D11ShaderResourceView* const views[2]{ a_source, params.mode ? m_mipViews[mip - 1].Get() : nullptr };
					for (uint32_t face = 0; face < s_faceCount; ++face)
					{
						params.face = face;
						context->UpdateSubresource(m_params.Get(), 0, nullptr, &params, 0, 0);
						// Target first: the previous-mip view never overlaps it, so both stay bound.
						context->OMSetRenderTargets(1, m_targets[face][mip].GetAddressOf(), nullptr);
						context->PSSetShaderResources(0, 2, views);
						context->Draw(3, 0);
					}
				}
			}

			ComPtr<W32::ID3D11DeviceContext>      m_recorder;
			ComPtr<W32::ID3D11VertexShader>       m_vertexShader;
			ComPtr<W32::ID3D11PixelShader>        m_pixelShader;
			ComPtr<W32::ID3D11Texture2D>          m_scratch;
			ComPtr<W32::ID3D11ShaderResourceView> m_mipViews[s_mipCount];
			ComPtr<W32::ID3D11RenderTargetView>   m_targets[s_faceCount][s_mipCount];
			ComPtr<W32::ID3D11SamplerState>       m_sampler;
			ComPtr<W32::ID3D11RasterizerState>    m_rasterizer;
			ComPtr<W32::ID3D11Buffer>             m_params;
		};

		// Leaked on purpose: static destruction runs after the D3D device is gone.
		static CubeNormalizer* s_normalizer{ nullptr };

		// 1x1 cubes stay rejected so an earlier AssignUsedTexture hook can claim them.
		[[nodiscard]] static bool IsAdmissible(const RE::BSGraphics::Texture* a_source) noexcept
		{
			const auto& header = a_source->header;
			return (header.flags & s_cubeFlag) && !(header.width == 1 && header.height == 1) &&
				a_source->texture && a_source->srView;
		}

		static RE::BSGraphics::Texture* HKCreateTextureArrayEmpty([[maybe_unused]] void* a_renderer, [[maybe_unused]] RE::BSGraphics::Texture* a_first, uint32_t a_count) noexcept
		{
			W32::D3D11_TEXTURE2D_DESC desc{};
			desc.width = s_size;
			desc.height = s_size;
			desc.mipLevels = s_mipCount;
			desc.arraySize = s_faceCount * a_count;
			desc.format = s_format;
			desc.sampleDesc.count = 1;
			desc.usage = W32::D3D11_USAGE_DEFAULT;
			desc.bindFlags = W32::D3D11_BIND_SHADER_RESOURCE;
			desc.miscFlags = W32::D3D11_RESOURCE_MISC_TEXTURECUBE;

			RE::BSGraphics::TextureHeader header{};
			header.height = static_cast<uint16_t>(s_size);
			header.width = static_cast<uint16_t>(s_size);
			header.mipCount = static_cast<uint8_t>(s_mipCount);
			header.format = static_cast<uint8_t>(s_format);
			header.flags = s_cubeFlag;

			auto* const array = CreateTextureArrayHelper(desc, header);
			if (array)
				REX::INFO("Cubemap Array: created the envmap array for {} cubes at {}x{}, {} mips, BGRA8 sRGB"sv, a_count, s_size, s_size, s_mipCount);
			else  // The engine dereferences the array right after, as it does natively.
				REX::ERROR("Cubemap Array: D3D11 refused the envmap array for {} cubes; the engine will fault on it"sv, a_count);
			return array;
		}

		// Matches the native copy: renderer lock held, the calling thread's context.
		static void HKAddTextureToArray(RE::BSGraphics::Renderer* a_renderer, RE::BSGraphics::Texture* a_source, RE::BSGraphics::Texture* a_array, uint32_t a_firstFace) noexcept
		{
			if (!a_source || !a_source->texture || !a_source->srView || !a_array || !a_array->texture)
				return;

			auto* const context = RE::TLS::GetSingleton()->d3dContext;
			if (!context)
				return;

			a_renderer->Lock();
			s_normalizer->Copy(context, *a_source, a_array->texture, a_firstFace);
			a_renderer->Unlock();
		}

		struct AdmissionPatch :
			RELEX::Stub
		{
			AdmissionPatch(bool a_og, uintptr_t a_join)
			{
				if (a_og)
					mov(rcx, rbx);
				else
				{
					// The native compare paths leave esi = 1 for the join and the slot claim.
					mov(esi, 1);
					mov(rcx, rdi);
				}

				// rsp is 16-aligned; the function's own 0x20 home space at rsp is free.
				mov(rax, reinterpret_cast<uintptr_t>(&IsAdmissible));
				call(rax);
				JumpTo(a_join);
			}
		};
	}

	ModuleCubemapArray::ModuleCubemapArray() :
		Module("Cubemap Array", &bFixesCubemapArray)
	{}

	bool ModuleCubemapArray::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace cubemapArrayDetail;

		const bool og = RELEX::IsRuntimeOG();
		const auto assign = REL::Relocation<uintptr_t>{ RE::ID::BSDFCompositeShaderEnvmapAccumulator::AssignUsedTexture }.address();
		const auto copy = REL::Relocation<uintptr_t>{ RE::ID::BSDFCompositeShaderEnvmapAccumulator::CopyPendingTexturesToArray }.address();

		const auto admission = assign + (og ? 0x23 : 0x1E);
		const auto join = assign + (og ? 0xA3 : 0xC1);
		const auto arrayTest = copy + 0x1F;
		const auto createCall = copy + (og ? 0x4A : 0x51);
		const auto addCall = copy + 0xB4;

		// test rcx, rcx; jz pre-array; then the header compares against the array
		const bool admissionValid = og ?
			RELEX::Validate(assign + 0x1E, { 0xBD, 0x01, 0x00, 0x00, 0x00 }) &&  // mov ebp, 1
				RELEX::Validate(admission, { 0x48, 0x85, 0xC9, 0x74, 0x39, 0x0F, 0xB7, 0x41, 0x28, 0x66, 0x39, 0x43, 0x28, 0x75, 0x2B,
					0x0F, 0xB7, 0x41, 0x2A, 0x66, 0x39, 0x43, 0x2A, 0x75, 0x21, 0x0F, 0xB6, 0x41, 0x2D, 0x38, 0x43, 0x2D, 0x75, 0x18,
					0x0F, 0xB6, 0x41, 0x2E, 0x38, 0x43, 0x2E, 0x75, 0x0F, 0x0F, 0xB6, 0x41, 0x2C, 0x38, 0x43, 0x2C, 0x72, 0x06 }) &&
				RELEX::Validate(join, { 0x89, 0x2D }) && RELEX::Validate(join + 6, { 0x84, 0xC0, 0x0F, 0x84 }) :  // mov [flag], ebp; test al, al; jz
			RELEX::Validate(admission, { 0x48, 0x85, 0xC9, 0x74, 0x57, 0x0F, 0xB7, 0x41, 0x28, 0x66, 0x39, 0x47, 0x28, 0x75, 0x32,
				0x0F, 0xB7, 0x41, 0x2A, 0x66, 0x39, 0x47, 0x2A, 0x75, 0x28, 0x0F, 0xB6, 0x41, 0x2D, 0x38, 0x47, 0x2D, 0x75, 0x1F,
				0x0F, 0xB6, 0x41, 0x2E, 0x38, 0x47, 0x2E, 0x75, 0x16, 0x0F, 0xB6, 0x41, 0x2C, 0x38, 0x47, 0x2C, 0x72, 0x0D }) &&
				RELEX::Validate(join, { 0x89, 0x35 }) && RELEX::Validate(join + 6, { 0x84, 0xC0, 0x74, 0x9F });  // mov [flag], esi; test al, al; jz

		if (!admissionValid)
		{
			Skip("AssignUsedTexture bytes do not match; unsupported runtime or another plugin owns the site"sv);
			return false;
		}

		const auto createTarget = REL::Relocation<uintptr_t>{ RE::ID::BSGraphics::Renderer::CreateTextureArrayEmpty }.address();
		const auto addTarget = REL::Relocation<uintptr_t>{ RE::ID::BSGraphics::Renderer::AddTextureToArray }.address();
		if (!RELEX::IsCallTo(createCall, createTarget) || !RELEX::IsCallTo(addCall, addTarget) ||
			!RELEX::Validate(arrayTest, { 0x48, 0x83, 0x3D }) || !RELEX::Validate(arrayTest + 7, { 0x00 }))  // cmp [array], 0
		{
			Skip("CopyPendingTexturesToArray bytes do not match; unsupported runtime or another plugin owns the site"sv);
			return false;
		}

		// Too late once a cube has already chosen the array's format for this session.
		const auto arraySlot = arrayTest + 8 + static_cast<intptr_t>(*reinterpret_cast<const int32_t*>(arrayTest + 3));
		if (*reinterpret_cast<const RE::BSGraphics::Texture* const*>(arraySlot))
		{
			Skip("the envmap array already exists"sv);
			return false;
		}

		const auto* const rendererData = RE::BSGraphics::GetRendererData();
		if (!rendererData || !rendererData->device)
		{
			Skip("the D3D11 device is unavailable"sv);
			return false;
		}

		AdmissionPatch patch{ og, join };
		patch.ready();

		// The cave plus the 14-byte branch stub write_jmp places in the trampoline.
		auto& trampoline = REL::GetTrampoline();
		if (trampoline.free_size() < patch.getSize() + 14)
		{
			Skip("the trampoline has no room for the admission patch"sv);
			return false;
		}

		auto normalizer = std::make_unique<CubeNormalizer>();
		if (!normalizer->Initialize(rendererData->device))
		{
			Skip("the cube conversion pipeline could not be created"sv);
			return false;
		}
		s_normalizer = normalizer.release();

		// Copy paths first: alone, admission still passes only exact-layout cubes.
		if (!RELEX::DetourCall(createCall, reinterpret_cast<uintptr_t>(&HKCreateTextureArrayEmpty)) ||
			!RELEX::DetourCall(addCall, reinterpret_cast<uintptr_t>(&HKAddTextureToArray)))
		{
			REX::ERROR("Cubemap Array: failed to patch CopyPendingTexturesToArray"sv);
			return false;
		}

		trampoline.write_jmp<5>(admission, trampoline.allocate(patch));

		REX::INFO("Cubemap Array: material envmaps use a fixed 128x128, 8-mip BGRA8 sRGB array"sv);
		return true;
	}
}
