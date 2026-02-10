#include <Modules\AdModuleControlSamplers.h>
#include <AdUtils.h>

#include <comdef.h>
#include <wrl/client.h>

#undef MEM_RELEASE

#include <RE\B\BSGraphics.h>
#include <RE\S\Setting.h>

namespace Addictol
{
	// Does not exist in Pref list
	static constexpr char MIPBIAS_OPTION_NAME[] = "fMipBias:Display";
	static constexpr char MAXANISTROPY_OPTION_NAME[] = "iMaxAnisotropy:Display";

	RE::Setting g_MipBiasSetting{ MIPBIAS_OPTION_NAME, 0.0f };
	RE::Setting g_MaxAnisotropySetting{ MAXANISTROPY_OPTION_NAME, 0 };

	using namespace Microsoft::WRL;

	static std::unordered_set<REX::W32::ID3D11SamplerState*> g_PassThroughSamplers;
	static std::unordered_map<REX::W32::ID3D11SamplerState*, ComPtr<REX::W32::ID3D11SamplerState>> g_MappedSamplers;
	static RE::BSGraphics::RendererData* g_RendererDataForCS{ nullptr };

	using XXSetSamplers = void (*)(REX::W32::ID3D11DeviceContext*, uint32_t, uint32_t, REX::W32::ID3D11SamplerState* const*) noexcept;
	static XXSetSamplers g_origSetSamplers[6];

	///////////////////////////////////////////////////////////////////////////////

	static bool CS_CheckAddrState(REX::W32::ID3D11SamplerState* Ptr)
	{
		__try
		{
			REX::W32::D3D11_SAMPLER_DESC sd;
			Ptr->GetDesc(&sd);
			return true;
		}
		__except (1)
		{
			return false;
		}
	}

	// Mostly from vrperfkit, thanks to fholger for showing how to do mip lod bias
	// https://github.com/fholger/vrperfkit/blob/037c09f3168ac045b5775e8d1a0c8ac982b5854f/src/d3d11/d3d11_post_processor.cpp#L76
	static void PreXSSetSamplers(REX::W32::ID3D11SamplerState** OutSamplers, uint32_t StartSlot, uint32_t NumSamplers,
		REX::W32::ID3D11SamplerState* const* Samplers) noexcept
	{
		memcpy(OutSamplers, Samplers, NumSamplers * sizeof(REX::W32::ID3D11SamplerState*));
		for (uint32_t i = 0; i < NumSamplers; ++i)
		{
			auto orig = OutSamplers[i];
			if ((orig == nullptr) || (g_PassThroughSamplers.find(orig) != g_PassThroughSamplers.end()) || !CS_CheckAddrState(orig))
				continue;

			if (g_MappedSamplers.find(orig) == g_MappedSamplers.end())
			{
				REX::W32::D3D11_SAMPLER_DESC sd;
				orig->GetDesc(&sd);

				if (sd.mipLODBias)
				{
					// do not mess with samplers that already have a bias.
					// should hopefully reduce the chance of causing rendering errors.
					g_PassThroughSamplers.insert(orig);
					continue;
				}

				//REX::INFO("[DBG] Filter: {}, MipLODBias: {}, MaxAnisotropy: {}", (uint32_t)sd.filter, sd.mipLODBias, sd.maxAnisotropy);

				sd.mipLODBias = g_MipBiasSetting.GetFloat();
				sd.maxAnisotropy = (sd.filter == REX::W32::D3D11_FILTER_ANISOTROPIC) ? (uint32_t)g_MaxAnisotropySetting.GetInt() : 0;

				// Fix getting this weird line with pipboy light. Maybe artifact flashlight texture.
				//sd.MinLOD = 0;
				//sd.MaxLOD = REX::W32::D3D11_FLOAT32_MAX;

				g_RendererDataForCS->device->CreateSamplerState(&sd, g_MappedSamplers[orig].GetAddressOf());
				g_PassThroughSamplers.insert(g_MappedSamplers[orig].Get());
			}

			OutSamplers[i] = g_MappedSamplers[orig].Get();
		}
	}

	static void __stdcall Hook_PSSetSamplers(REX::W32::ID3D11DeviceContext* a_this, uint32_t a_startSlot, uint32_t a_numSamplers,
		REX::W32::ID3D11SamplerState* const* a_samplers) noexcept
	{
		REX::W32::ID3D11SamplerState* samplers[REX::W32::D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
		PreXSSetSamplers(samplers, a_startSlot, a_numSamplers, a_samplers);
		g_origSetSamplers[0](a_this, a_startSlot, a_numSamplers, samplers);
	}

	static void __stdcall Hook_VSSetSamplers(REX::W32::ID3D11DeviceContext* a_this, uint32_t a_startSlot, uint32_t a_numSamplers,
		REX::W32::ID3D11SamplerState* const* a_samplers) noexcept
	{
		REX::W32::ID3D11SamplerState* samplers[REX::W32::D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
		PreXSSetSamplers(samplers, a_startSlot, a_numSamplers, a_samplers);
		g_origSetSamplers[1](a_this, a_startSlot, a_numSamplers, samplers);
	}

	static void __stdcall Hook_GSSetSamplers(REX::W32::ID3D11DeviceContext* a_this, uint32_t a_startSlot, uint32_t a_numSamplers,
		REX::W32::ID3D11SamplerState* const* a_samplers) noexcept
	{
		REX::W32::ID3D11SamplerState* samplers[REX::W32::D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
		PreXSSetSamplers(samplers, a_startSlot, a_numSamplers, a_samplers);
		g_origSetSamplers[2](a_this, a_startSlot, a_numSamplers, samplers);
	}

	static void __stdcall Hook_HSSetSamplers(REX::W32::ID3D11DeviceContext* a_this, uint32_t a_startSlot, uint32_t a_numSamplers,
		REX::W32::ID3D11SamplerState* const* a_samplers) noexcept
	{
		REX::W32::ID3D11SamplerState* samplers[REX::W32::D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
		PreXSSetSamplers(samplers, a_startSlot, a_numSamplers, a_samplers);
		g_origSetSamplers[3](a_this, a_startSlot, a_numSamplers, samplers);
	}

	static void __stdcall Hook_DSSetSamplers(REX::W32::ID3D11DeviceContext* a_this, uint32_t a_startSlot, uint32_t a_numSamplers,
		REX::W32::ID3D11SamplerState* const* a_samplers) noexcept
	{
		REX::W32::ID3D11SamplerState* samplers[REX::W32::D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
		PreXSSetSamplers(samplers, a_startSlot, a_numSamplers, a_samplers);
		g_origSetSamplers[4](a_this, a_startSlot, a_numSamplers, samplers);
	}

	static void __stdcall Hook_CSSetSamplers(REX::W32::ID3D11DeviceContext* a_this, uint32_t a_startSlot, uint32_t a_numSamplers,
		REX::W32::ID3D11SamplerState* const* a_samplers) noexcept
	{
		REX::W32::ID3D11SamplerState* samplers[REX::W32::D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
		PreXSSetSamplers(samplers, a_startSlot, a_numSamplers, a_samplers);
		g_origSetSamplers[5](a_this, a_startSlot, a_numSamplers, samplers);
	}

	///////////////////////////////////////////////////////////////////////////////

	ModuleControlSamplers::ModuleControlSamplers() :
		Module("Module Control Samplers", nullptr, {}, true)
	{}

	bool ModuleControlSamplers::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleControlSamplers::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (!a_msg)
		{
			auto Pref = RE::INIPrefSettingCollection::GetSingleton();
			Pref->settings.push_front(&g_MipBiasSetting);
			Pref->settings.push_front(&g_MaxAnisotropySetting);
		}
		else if (a_msg->type == F4SE::MessagingInterface::kGameDataReady)
		{
			g_RendererDataForCS = (RE::BSGraphics::RendererData*)REL::ID{ 235166, 2704527 }.address();

			*(uintptr_t*)&g_origSetSamplers[0] = RELEX::DetourVTable(*((uintptr_t*)g_RendererDataForCS->context),
				(uintptr_t)&Hook_PSSetSamplers, 10);
			*(uintptr_t*)&g_origSetSamplers[1] = RELEX::DetourVTable(*((uintptr_t*)g_RendererDataForCS->context),
				(uintptr_t)&Hook_VSSetSamplers, 26);
			*(uintptr_t*)&g_origSetSamplers[2] = RELEX::DetourVTable(*((uintptr_t*)g_RendererDataForCS->context),
				(uintptr_t)&Hook_GSSetSamplers, 32);
			*(uintptr_t*)&g_origSetSamplers[3] = RELEX::DetourVTable(*((uintptr_t*)g_RendererDataForCS->context),
				(uintptr_t)&Hook_HSSetSamplers, 61);
			*(uintptr_t*)&g_origSetSamplers[4] = RELEX::DetourVTable(*((uintptr_t*)g_RendererDataForCS->context),
				(uintptr_t)&Hook_DSSetSamplers, 65);
			*(uintptr_t*)&g_origSetSamplers[5] = RELEX::DetourVTable(*((uintptr_t*)g_RendererDataForCS->context),
				(uintptr_t)&Hook_CSSetSamplers, 70);
		}
		else return false;

		return true;
	}

	bool ModuleControlSamplers::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleControlSamplers::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		REX::INFO("[DBG] DoPapyrusListener");

		// TODO: Need register functions for papyrus

		return true;
	}
}