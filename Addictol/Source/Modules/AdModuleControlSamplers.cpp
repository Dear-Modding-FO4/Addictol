#include <Modules/AdModuleControlSamplers.h>
#include <Modules/AdControlSamplerCache.h>
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

	static ControlSamplerCache g_SamplerCache;
	static RE::BSGraphics::RendererData* g_RendererDataForCS{ nullptr };
	static ComPtr<REX::W32::ID3D11DeviceContext> g_HookedContext;
	static ComPtr<REX::W32::ID3D11Device> g_HookedDevice;

	using XXSetSamplers = void (*)(REX::W32::ID3D11DeviceContext*, uint32_t, uint32_t, REX::W32::ID3D11SamplerState* const*) noexcept;
	static XXSetSamplers g_origSetSamplers[6];

	///////////////////////////////////////////////////////////////////////////////

	[[nodiscard]] static uint32_t ClampMaxAnisotropy(long a_value) noexcept
	{
		return static_cast<uint32_t>(std::clamp(a_value, 1l, 16l));
	}

	// Mostly from vrperfkit, thanks to fholger for showing how to do mip lod bias
	// https://github.com/fholger/vrperfkit/blob/037c09f3168ac045b5775e8d1a0c8ac982b5854f/src/d3d11/d3d11_post_processor.cpp#L76
	template <size_t Index>
	static void __stdcall HookSetSamplers(REX::W32::ID3D11DeviceContext* a_this, uint32_t a_startSlot, uint32_t a_numSamplers,
		REX::W32::ID3D11SamplerState* const* a_samplers) noexcept
	{
		if (a_this != g_HookedContext.Get() || !a_samplers ||
			a_startSlot > REX::W32::D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT ||
			a_numSamplers > REX::W32::D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - a_startSlot)
		{
			g_origSetSamplers[Index](a_this, a_startSlot, a_numSamplers, a_samplers);
			return;
		}

		std::array<ControlSamplerCache::Sampler, REX::W32::D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> ownedSamplers;
		const ControlSamplerSettings settings{
			g_MipBiasSetting.GetFloat(),
			ClampMaxAnisotropy(g_MaxAnisotropySetting.GetInt()),
			bAdditionalIgnorePreInstallBias.GetValue()
		};
		g_SamplerCache.Translate(
			g_HookedDevice.Get(),
			settings,
			std::span{ a_samplers, a_numSamplers },
			std::span{ ownedSamplers.data(), a_numSamplers },
			[](const REX::W32::D3D11_SAMPLER_DESC& a_desc, REX::W32::ID3D11SamplerState** a_sampler) {
				return g_HookedDevice->CreateSamplerState(&a_desc, a_sampler);
			});

		std::array<REX::W32::ID3D11SamplerState*, REX::W32::D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> samplers{};
		for (uint32_t i = 0; i < a_numSamplers; ++i)
			samplers[i] = ownedSamplers[i].Get();
		g_origSetSamplers[Index](a_this, a_startSlot, a_numSamplers, samplers.data());
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

			g_MipBiasSetting.SetFloat(a_value);
			g_SamplerCache.Clear();

			WriteINISettingFloat(g_PrefIniFileName.c_str(), MIPBIAS_OPTION_NAMEW, a_value);
		}

		static void SetDefaultMipLODBias(std::monostate a_base) noexcept
		{
			SetMipLODBias(a_base, -1.f);
		}

		static long GetMaxAnisotropy([[maybe_unused]] std::monostate a_base) noexcept
		{
			return static_cast<long>(ClampMaxAnisotropy(g_MaxAnisotropySetting.GetInt()));
		}

		static void SetMaxAnisotropy([[maybe_unused]] std::monostate a_base, long a_value) noexcept
		{
			a_value = static_cast<long>(ClampMaxAnisotropy(a_value));

			REX::INFO("MAX Anisotropy changed from {} to {}, recreating samplers"sv,
				static_cast<int>(g_MaxAnisotropySetting.GetInt()), a_value);

			g_MaxAnisotropySetting.SetInt((int32_t)a_value);
			g_SamplerCache.Clear();

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
			ComPtr<REX::W32::ID3D11DeviceContext> realContext;
			g_RendererDataForCS->device->GetImmediateContext(realContext.GetAddressOf());

			if (realContext && realContext.Get() != g_RendererDataForCS->context)
			{
				REX::WARN("D3D11 device context proxy detected, hooking real immediate context"sv);
				g_HookedContext = realContext;
			}

			if (!g_HookedContext)
			{
				REX::WARN("Control Samplers: D3D11 immediate context is unavailable."sv);
				return false;
			}
			g_HookedContext->GetDevice(g_HookedDevice.GetAddressOf());
			if (!g_HookedDevice)
			{
				REX::WARN("Control Samplers: hooked D3D11 context has no device."sv);
				return false;
			}

			auto vtable = *reinterpret_cast<uintptr_t**>(g_HookedContext.Get());
			g_origSetSamplers[0] = reinterpret_cast<XXSetSamplers>(vtable[10]);
			g_origSetSamplers[1] = reinterpret_cast<XXSetSamplers>(vtable[26]);
			g_origSetSamplers[2] = reinterpret_cast<XXSetSamplers>(vtable[32]);
			g_origSetSamplers[3] = reinterpret_cast<XXSetSamplers>(vtable[61]);
			g_origSetSamplers[4] = reinterpret_cast<XXSetSamplers>(vtable[65]);
			g_origSetSamplers[5] = reinterpret_cast<XXSetSamplers>(vtable[70]);

			RELEX::DetourVTable((uintptr_t)vtable, (uintptr_t)&HookSetSamplers<0>, 10);
			RELEX::DetourVTable((uintptr_t)vtable, (uintptr_t)&HookSetSamplers<1>, 26);
			RELEX::DetourVTable((uintptr_t)vtable, (uintptr_t)&HookSetSamplers<2>, 32);
			RELEX::DetourVTable((uintptr_t)vtable, (uintptr_t)&HookSetSamplers<3>, 61);
			RELEX::DetourVTable((uintptr_t)vtable, (uintptr_t)&HookSetSamplers<4>, 65);
			RELEX::DetourVTable((uintptr_t)vtable, (uintptr_t)&HookSetSamplers<5>, 70);
		}

		return true;
	}

	bool ModuleControlSamplers::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && (a_msg->type == F4SE::MessagingInterface::kPostLoadGame))
		{
			g_SamplerCache.Clear();
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
