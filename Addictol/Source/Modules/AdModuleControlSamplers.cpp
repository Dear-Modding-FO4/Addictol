#include <Modules/AdModuleControlSamplers.h>
#include <Core/AdUtils.h>

#include <atomic>
#include <comdef.h>
#include <mutex>
#include <vector>
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


	namespace controlSamplersDetail
	{
		using Sampler = Microsoft::WRL::ComPtr<REX::W32::ID3D11SamplerState>;
		using SamplerTable = REX::W32::ID3D11SamplerState*[4][5][16];

		static std::mutex s_mutex;
		static SamplerTable* s_table{ nullptr };
		static Microsoft::WRL::ComPtr<REX::W32::ID3D11Device> s_device;
		// CS scales the bias to its upscaler's render size per pass
		static bool s_mipBiasOwnedExternally{ false };
		// Render-thread readers may retain any retired pointer until process exit.
		static std::vector<Sampler> s_retiredSamplers;
	}

	///////////////////////////////////////////////////////////////////////////////

	[[nodiscard]] static uint32_t ClampMaxAnisotropy(long a_value) noexcept
	{
		return static_cast<uint32_t>(std::clamp(a_value, 1l, 16l));
	}

	namespace controlSamplersDetail
	{
		// Callers hold s_mutex across settings changes, rebuilds, and retirement.
		[[nodiscard]] static bool RebuildSamplersLocked() noexcept
		{
			if (!s_table || !s_device)
			{
				REX::WARN("Control Samplers: sampler table or D3D11 device is unavailable; skipping rebuild."sv);
				return false;
			}

			const auto mipBias = g_MipBiasSetting.GetFloat();
			const auto maxAnisotropy = ClampMaxAnisotropy(g_MaxAnisotropySetting.GetInt());
			static constexpr uint32_t filters[]{ 0x00, 0x14, 0x15, 0x55, 0x94 };
			Sampler replacements[4][5][16];

			for (size_t address = 0; address < 4; ++address)
			{
				for (size_t filter = 0; filter < 5; ++filter)
				{
					for (size_t minLOD = 0; minLOD < 16; ++minLOD)
					{
						const auto sampler = (*s_table)[address][filter][minLOD];
						if (!sampler)
						{
							REX::WARN("Control Samplers: sampler table entry [{},{},{}] is null; skipping rebuild."sv, address, filter, minLOD);
							return false;
						}

						REX::W32::D3D11_SAMPLER_DESC desc{};
						sampler->GetDesc(&desc);
						const auto addressU = (address & 2) ? REX::W32::D3D11_TEXTURE_ADDRESS_WRAP : REX::W32::D3D11_TEXTURE_ADDRESS_CLAMP;
						const auto addressV = (address & 1) ? REX::W32::D3D11_TEXTURE_ADDRESS_WRAP : REX::W32::D3D11_TEXTURE_ADDRESS_CLAMP;
						const auto addressW = address == 3 ? REX::W32::D3D11_TEXTURE_ADDRESS_WRAP : REX::W32::D3D11_TEXTURE_ADDRESS_CLAMP;
						if (static_cast<uint32_t>(desc.filter) != filters[filter] ||
							desc.addressU != addressU || desc.addressV != addressV || desc.addressW != addressW)
						{
							REX::WARN("Control Samplers: unexpected sampler table layout at [{},{},{}]; skipping rebuild."sv, address, filter, minLOD);
							return false;
						}

						if (filter != 3 && !(address == 3 && filter == 2))
							continue;

						// CLAMP samplers serve explicit-LOD reflections, not materials.
						if (address == 3)
							desc.mipLODBias = s_mipBiasOwnedExternally ? 0.f : mipBias;
						if (filter == 3)
							desc.maxAnisotropy = maxAnisotropy;

						auto& replacement = replacements[address][filter][minLOD];
						const auto result = s_device->CreateSamplerState(&desc, replacement.GetAddressOf());
						if (FAILED(result) || !replacement)
						{
							REX::WARN("Control Samplers: CreateSamplerState failed at [{},{},{}] ({}); skipping rebuild."sv, address, filter, minLOD, result);
							return false;
						}
					}
				}
			}

			// Stage the entire rebuild before publishing any render-thread pointers.
			s_retiredSamplers.reserve(s_retiredSamplers.size() + 80);
			for (size_t address = 0; address < 4; ++address)
			{
				for (size_t filter = 0; filter < 5; ++filter)
				{
					for (size_t minLOD = 0; minLOD < 16; ++minLOD)
					{
						auto& replacement = replacements[address][filter][minLOD];
						if (!replacement)
							continue;

						std::atomic_ref entry{ (*s_table)[address][filter][minLOD] };
						s_retiredSamplers.emplace_back().Attach(entry.exchange(replacement.Detach(), std::memory_order_release));
					}
				}
			}
			return true;
		}
	}

	///////////////////////////////////////////////////////////////////////////////

	namespace VirtualMachine
	{
		static float GetMipLODBias([[maybe_unused]] std::monostate a_base) noexcept
		{
			const std::scoped_lock lock{ controlSamplersDetail::s_mutex };
			return g_MipBiasSetting.GetFloat();
		}

		static void SetMipLODBias([[maybe_unused]] std::monostate a_base, float a_value) noexcept
		{
			const std::scoped_lock lock{ controlSamplersDetail::s_mutex };
			a_value = std::min(5.f, std::max(-5.f, a_value));

			REX::INFO("MIP LOD Bias changed from {} to {}, recreating samplers"sv,
				static_cast<float>(g_MipBiasSetting.GetFloat()), a_value);

			g_MipBiasSetting.SetFloat(a_value);
			(void)controlSamplersDetail::RebuildSamplersLocked();

			WriteINISettingFloat(g_PrefIniFileName.c_str(), MIPBIAS_OPTION_NAMEW, a_value);
		}

		static void SetDefaultMipLODBias(std::monostate a_base) noexcept
		{
			SetMipLODBias(a_base, -1.f);
		}

		static long GetMaxAnisotropy([[maybe_unused]] std::monostate a_base) noexcept
		{
			const std::scoped_lock lock{ controlSamplersDetail::s_mutex };
			return static_cast<long>(ClampMaxAnisotropy(g_MaxAnisotropySetting.GetInt()));
		}

		static void SetMaxAnisotropy([[maybe_unused]] std::monostate a_base, long a_value) noexcept
		{
			const std::scoped_lock lock{ controlSamplersDetail::s_mutex };
			a_value = static_cast<long>(ClampMaxAnisotropy(a_value));

			REX::INFO("MAX Anisotropy changed from {} to {}, recreating samplers"sv,
				static_cast<int>(g_MaxAnisotropySetting.GetInt()), a_value);

			g_MaxAnisotropySetting.SetInt((int32_t)a_value);
			(void)controlSamplersDetail::RebuildSamplersLocked();

			WriteINISettingInt(g_PrefIniFileName.c_str(), MAXANISTROPY_OPTION_NAMEW, a_value);
		}

		static void SetDefaultMaxAnisotropy(std::monostate a_base) noexcept
		{
			SetMaxAnisotropy(a_base, 16);
		}
	}

	///////////////////////////////////////////////////////////////////////////////

	ModuleControlSamplers::ModuleControlSamplers() :
		Module("Control Samplers", nullptr, {}, true)
	{}

	bool ModuleControlSamplers::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kGameDataReady)
		{
			const std::scoped_lock lock{ controlSamplersDetail::s_mutex };
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

			const auto rendererData = RE::BSGraphics::GetRendererData();
			if (!rendererData || !rendererData->device)
			{
				REX::WARN("Control Samplers: renderer D3D11 device is unavailable; skipping rebuild."sv);
				return false;
			}

			controlSamplersDetail::s_device = rendererData->device;
			controlSamplersDetail::s_mipBiasOwnedExternally = F4SE::GetPluginInfo("FO4CommunityShaders"sv) != nullptr;
			if (controlSamplersDetail::s_mipBiasOwnedExternally)
				REX::INFO("Control Samplers: Community Shaders is loaded; leaving mip bias to it."sv);
			controlSamplersDetail::s_table = reinterpret_cast<controlSamplersDetail::SamplerTable*>(REL::ID{ 44312, 2704455, 2704455 }.address());
			return controlSamplersDetail::RebuildSamplersLocked();
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
