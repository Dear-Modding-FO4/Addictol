#include <Modules/AdModuleCreateD3DAndSwapchain.h>
#include <Core/AdUtils.h>

#include <dxgi.h>
#include <xbyak/xbyak.h>

#undef ERROR

namespace Addictol
{
	namespace createD3DAndSwapchainDetail
	{
		static ::HRESULT GetDisplayModeList(::IDXGIOutput& a_this, ::DXGI_FORMAT a_enumFormat, 
			::UINT a_flags, ::UINT* a_numModes, ::DXGI_MODE_DESC* a_desc)
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

		// OG/NG skip the windowed refresh query on the no-mode fallback, leaving 0/0.
		struct WindowedRefresh : Xbyak::CodeGenerator
		{
			WindowedRefresh(std::uintptr_t a_freeModes, std::uintptr_t a_rendererData, std::uintptr_t a_refreshQuery, std::uintptr_t a_resume)
			{
				Xbyak::Label freeLab;
				Xbyak::Label refreshLab;
				Xbyak::Label resumeLab;

				call(ptr[rip + freeLab]);
				xor_(esi, esi);
				mov(rcx, a_rendererData);
				mov(rcx, qword[rcx]);

				// A matched mode keeps fullscreen and its own refresh rate, as AE checks.
				cmp(byte[rcx + 0x30], sil);
				jne("resume");
				jmp(ptr[rip + refreshLab]);

				L("resume");
				jmp(ptr[rip + resumeLab]);

				L(freeLab);
				dq(a_freeModes);

				L(refreshLab);
				dq(a_refreshQuery);

				L(resumeLab);
				dq(a_resume);
			}
		};
	}

	ModuleCreateD3DAndSwapchain::ModuleCreateD3DAndSwapchain() :
		Module("CreateD3D and Swapchain", &bFixesCreateD3DAndSwapchain)
	{}

	bool ModuleCreateD3DAndSwapchain::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace createD3DAndSwapchainDetail;

		const auto function = REL::Relocation<uintptr_t>{ RE::ID::BSGraphics::CreateD3DAndSwapChain }.address();
		const auto target = function + REL::Offset{ 0x114, 0x114, 0x10B }.offset();
		const std::size_t size = 0x7;

		if (!RELEX::Validate(target, { 0x41, 0xFF, 0x52, 0x40, 0x45, 0x32, 0xF6 }))
			return false;

		// call free; xor esi, esi; jmp +0x33; mov rcx, [rcx+58h]
		const auto fallback = function + 0x2CB;
		const bool patchFallback = !RELEX::IsRuntimeAE();
		const auto freeModes = patchFallback ? REL::Relocation<uintptr_t>{ RE::ID::ScrapHeap::Deallocate }.address() : 0;
		if (patchFallback && (!RELEX::IsCallTo(fallback, freeModes) || !RELEX::Validate(fallback + 5, { 0x33, 0xF6, 0xEB, 0x33, 0x48, 0x8B, 0x49, 0x58 })))
		{
			Skip("the windowed fallback does not match; unsupported runtime or another plugin patched it"sv);
			return false;
		}

		REL::WriteSafeFill(target, REL::INT3, size);
		if (RELEX::XbyakJump<Patch>(target, target + size, reinterpret_cast<uintptr_t>(&GetDisplayModeList)) == 0)
			return false;

		if (!patchFallback)
			return true;

		const auto rendererData = REL::Relocation<uintptr_t>{ RE::ID::BSGraphics::GetRendererData }.address();
		if (RELEX::XbyakJump<WindowedRefresh>(fallback, freeModes, rendererData, fallback + 9, fallback + 9 + 0x33) == 0)
		{
			REX::ERROR("CreateD3D and Swapchain: failed to patch the windowed fallback at 0x{:X}"sv, fallback);
			return false;
		}

		return true;
	}
}
