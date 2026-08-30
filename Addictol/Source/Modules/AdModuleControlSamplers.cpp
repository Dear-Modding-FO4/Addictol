#include <Modules/AdModuleControlSamplers.h>
#include <Core/AdUtils.h>

#include <comdef.h>
#include <wrl/client.h>
#include <ShlObj_core.h>

#undef ERROR
#undef MAX_PATH
#undef MEM_RELEASE

#include <RE/B/BSScriptUtil.h>
#include <RE/B/BSGraphics.h>
#include <RE/S/Setting.h>

namespace Addictol
{
	// Does not exist in Pref list
	static constexpr char MIPBIAS_OPTION_NAME[]				= "fMipBias:Display";
	static constexpr char MAXANISTROPY_OPTION_NAME[]		= "iMaxAnisotropy:Display";
	static constexpr wchar_t MIPBIAS_OPTION_NAMEW[]			= L"fMipBias:Display";
	static constexpr wchar_t MAXANISTROPY_OPTION_NAMEW[]	= L"iMaxAnisotropy:Display";
	static constexpr auto OBJECT_PAPYRUS_NAME				= "Addictol"sv;
	static constexpr auto OBJECT_PAPYRUS_XCELL_NAME			= "XCELL"sv;

	RE::Setting g_MipBiasSetting{ MIPBIAS_OPTION_NAME, -1.f };
	RE::Setting g_MaxAnisotropySetting{ MAXANISTROPY_OPTION_NAME, 16 };
	std::wstring g_PrefIniFileName;


	using namespace Microsoft::WRL;

	static std::unordered_set<REX::W32::ID3D11SamplerState*> g_PassThroughSamplers;
	static std::unordered_map<REX::W32::ID3D11SamplerState*, ComPtr<REX::W32::ID3D11SamplerState>> g_MappedSamplers;
	static RE::BSGraphics::RendererData* g_RendererDataForCS{ nullptr };
	static REX::W32::ID3D11DeviceContext* g_HookedContext{ nullptr };

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
	static void PreXSSetSamplers(REX::W32::ID3D11SamplerState** OutSamplers, [[maybe_unused]] uint32_t StartSlot, uint32_t NumSamplers,
		REX::W32::ID3D11SamplerState* const* Samplers) noexcept
	{
		memcpy(OutSamplers, Samplers, NumSamplers * sizeof(REX::W32::ID3D11SamplerState*));
		for (uint32_t i = 0; i < NumSamplers; ++i)
		{
			auto orig = OutSamplers[i];
			if ((orig == nullptr) || g_PassThroughSamplers.contains(orig) || !CS_CheckAddrState(orig))
				continue;

			if (!g_MappedSamplers.contains(orig))
			{
				REX::W32::D3D11_SAMPLER_DESC sd{};
				orig->GetDesc(&sd);

				if (sd.mipLODBias && !bAdditionalIgnorePreInstallBias.GetValue())
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

	namespace VirtualMachine
	{
		static float GetMipLODBias([[maybe_unused]] std::monostate a_base) noexcept
		{
			return g_MipBiasSetting.GetFloat();
		}

		static void SetMipLODBias([[maybe_unused]] std::monostate a_base, float a_value) noexcept
		{
			a_value = std::min(5.f, std::max(-5.f, a_value));

			REX::INFO("MIP LOD Bias changed from {} to {}, recreating samplers"sv,
				static_cast<float>(g_MipBiasSetting.GetFloat()), a_value);

			g_PassThroughSamplers.clear();
			g_MappedSamplers.clear();
			g_MipBiasSetting.SetFloat(a_value);

			WriteINISettingFloat(g_PrefIniFileName.c_str(), MIPBIAS_OPTION_NAMEW, a_value);
		}

		static void SetDefaultMipLODBias(std::monostate a_base) noexcept
		{
			SetMipLODBias(a_base, -1.f);
		}

		static long GetMaxAnisotropy([[maybe_unused]] std::monostate a_base) noexcept
		{
			return (long)g_MaxAnisotropySetting.GetInt();
		}

		static void SetMaxAnisotropy([[maybe_unused]] std::monostate a_base, long a_value) noexcept
		{
			a_value = std::min(16l, std::max(0l, a_value));

			REX::INFO("MAX Anisotropy changed from {} to {}, recreating samplers"sv,
				static_cast<int>(g_MaxAnisotropySetting.GetInt()), a_value);

			g_PassThroughSamplers.clear();
			g_MappedSamplers.clear();
			g_MaxAnisotropySetting.SetInt((int32_t)a_value);

			WriteINISettingInt(g_PrefIniFileName.c_str(), MAXANISTROPY_OPTION_NAMEW, a_value);
		}

		static void SetDefaultMaxAnisotropy(std::monostate a_base) noexcept
		{
			SetMaxAnisotropy(a_base, 16);
		}
	}

	///////////////////////////////////////////////////////////////////////////////

	ModuleControlSamplers::ModuleControlSamplers() :
		Module("Control Samplers", nullptr, { F4SE::MessagingInterface::kPostLoadGame }, true)
	{}

	bool ModuleControlSamplers::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kGameDataReady)
		{
			auto Pref = RE::INIPrefSettingCollection::GetSingleton();
			Pref->settings.push_front(&g_MipBiasSetting);
			Pref->settings.push_front(&g_MaxAnisotropySetting);

			wchar_t* knownBuffer{ nullptr };
			const auto knownResult = SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, std::addressof(knownBuffer));
			std::unique_ptr<wchar_t[], decltype(&CoTaskMemFree)> knownPath(knownBuffer, &CoTaskMemFree);
			if (!knownPath || knownResult != 0) {
				REX::ERROR("failed to get known folder path"sv);
				return false;
			}

			std::filesystem::path path = knownPath.get();
			path /= std::format("My Games/{}/Fallout4Prefs.ini"sv, GetSaveFolderName());
			g_PrefIniFileName = path;

			g_RendererDataForCS = (RE::BSGraphics::RendererData*)REL::ID{ 235166, 2704527 }.address();

			// Some upscalers swap renderer context with a proxy object.
			// Hook the real immediate context to keep COM vtable indices stable.
			g_HookedContext = g_RendererDataForCS->context;
			REX::W32::ID3D11DeviceContext* realContext = nullptr;
			g_RendererDataForCS->device->GetImmediateContext(&realContext);

			if (realContext)
			{
				if (realContext != g_RendererDataForCS->context)
				{
					REX::WARN("D3D11 device context proxy detected, hooking real immediate context"sv);
					g_HookedContext = realContext;
				}
				realContext->Release();
			}

			auto vtable = *reinterpret_cast<uintptr_t**>(g_HookedContext);
			g_origSetSamplers[0] = reinterpret_cast<XXSetSamplers>(vtable[10]);
			g_origSetSamplers[1] = reinterpret_cast<XXSetSamplers>(vtable[26]);
			g_origSetSamplers[2] = reinterpret_cast<XXSetSamplers>(vtable[32]);
			g_origSetSamplers[3] = reinterpret_cast<XXSetSamplers>(vtable[61]);
			g_origSetSamplers[4] = reinterpret_cast<XXSetSamplers>(vtable[65]);
			g_origSetSamplers[5] = reinterpret_cast<XXSetSamplers>(vtable[70]);

			RELEX::DetourVTable((uintptr_t)vtable, (uintptr_t)&Hook_PSSetSamplers, 10);
			RELEX::DetourVTable((uintptr_t)vtable, (uintptr_t)&Hook_VSSetSamplers, 26);
			RELEX::DetourVTable((uintptr_t)vtable, (uintptr_t)&Hook_GSSetSamplers, 32);
			RELEX::DetourVTable((uintptr_t)vtable, (uintptr_t)&Hook_HSSetSamplers, 61);
			RELEX::DetourVTable((uintptr_t)vtable, (uintptr_t)&Hook_DSSetSamplers, 65);
			RELEX::DetourVTable((uintptr_t)vtable, (uintptr_t)&Hook_CSSetSamplers, 70);
		}

		return true;
	}

	bool ModuleControlSamplers::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && (a_msg->type == F4SE::MessagingInterface::kPostLoadGame))
		{
			g_PassThroughSamplers.clear();
			g_MappedSamplers.clear();
		}

		return true;
	}

	bool ModuleControlSamplers::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		a_vm->BindNativeMethod(OBJECT_PAPYRUS_NAME, "GetMipLODBias"sv,				VirtualMachine::GetMipLODBias);
		a_vm->BindNativeMethod(OBJECT_PAPYRUS_NAME, "SetMipLODBias"sv,				VirtualMachine::SetMipLODBias);
		a_vm->BindNativeMethod(OBJECT_PAPYRUS_NAME, "SetDefaultMipLODBias"sv,		VirtualMachine::SetDefaultMipLODBias);
		a_vm->BindNativeMethod(OBJECT_PAPYRUS_NAME, "GetMaxAnisotropy"sv,			VirtualMachine::GetMaxAnisotropy);
		a_vm->BindNativeMethod(OBJECT_PAPYRUS_NAME, "SetMaxAnisotropy"sv,			VirtualMachine::SetMaxAnisotropy);
		a_vm->BindNativeMethod(OBJECT_PAPYRUS_NAME, "SetDefaultMaxAnisotropy"sv,	VirtualMachine::SetDefaultMaxAnisotropy);

		// For support XCELL mods
		a_vm->BindNativeMethod(OBJECT_PAPYRUS_XCELL_NAME, "GetMipLODBias"sv,			VirtualMachine::GetMipLODBias);
		a_vm->BindNativeMethod(OBJECT_PAPYRUS_XCELL_NAME, "SetMipLODBias"sv,			VirtualMachine::SetMipLODBias);
		a_vm->BindNativeMethod(OBJECT_PAPYRUS_XCELL_NAME, "SetDefaultMipLODBias"sv,		VirtualMachine::SetDefaultMipLODBias);
		a_vm->BindNativeMethod(OBJECT_PAPYRUS_XCELL_NAME, "GetMaxAnisotropy"sv,			VirtualMachine::GetMaxAnisotropy);
		a_vm->BindNativeMethod(OBJECT_PAPYRUS_XCELL_NAME, "SetMaxAnisotropy"sv,			VirtualMachine::SetMaxAnisotropy);
		a_vm->BindNativeMethod(OBJECT_PAPYRUS_XCELL_NAME, "SetDefaultMaxAnisotropy"sv,	VirtualMachine::SetDefaultMaxAnisotropy);

		REX::INFO("Register papyrus functions succeed"sv);

		return true;
	}
}
