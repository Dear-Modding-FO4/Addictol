#include <Modules/AdModuleCreateD3DAndSwapchain.h>
#include <AdUtils.h>

#include <dxgi.h>
#include <xbyak/xbyak.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesCreateD3DAndSwapchain{ "Fixes"sv, "bCreateD3DAndSwapchain"sv, true };

	namespace createD3DAndSwapchainDetail
	{
		::HRESULT GetDisplayModeList(::IDXGIOutput& a_this, ::DXGI_FORMAT a_enumFormat, ::UINT a_flags, ::UINT* a_numModes, ::DXGI_MODE_DESC* a_desc)
		{
			const auto result = a_this.GetDisplayModeList(a_enumFormat, a_flags, a_numModes, a_desc);

			const auto modes = std::span(a_desc, *a_numModes);
			const auto end = std::stable_partition(
				modes.begin(),
				modes.end(),
				[](const ::DXGI_MODE_DESC& a_desc) {
					return a_desc.RefreshRate.Denominator != 0;
				});
			*a_numModes = static_cast<::UINT>(end - modes.begin());

			return result;
		}

		struct Patch : Xbyak::CodeGenerator
		{
			Patch(std::uintptr_t resumeAddr, std::uintptr_t funcAddr)
			{
				mov(r10, funcAddr);
				call(r10);

				xor_(r14b, r14b);

				jmp(ptr[rip]);
				dq(resumeAddr);
			}
		};
	}

	ModuleCreateD3DAndSwapchain::ModuleCreateD3DAndSwapchain() :
		Module("CreateD3D and Swapchain", &bFixesCreateD3DAndSwapchain)
	{}

	bool ModuleCreateD3DAndSwapchain::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleCreateD3DAndSwapchain::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		const auto target = REL::Relocation<std::uintptr_t>(REL::ID{ 224250, 2277018, 4492363 }, REL::Offset{ 0x114, 0x114, 0x10B }).address();
		const std::size_t size = 0x7;

		REL::WriteSafeFill(target, REL::INT3, size);
		RELEX::XbyakJump<createD3DAndSwapchainDetail::Patch>(target, target + size, (std::uintptr_t)&createD3DAndSwapchainDetail::GetDisplayModeList);

		return true;
	}

	bool ModuleCreateD3DAndSwapchain::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleCreateD3DAndSwapchain::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
