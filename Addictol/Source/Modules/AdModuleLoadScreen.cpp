#include <Modules/AdModuleLoadScreen.h>
#include <AdUtils.h>
#include <RE/B/BSGraphics.h>

#include <xbyak/xbyak.h>

#include <bit>
#include <cstring>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesLoadScreen{ "Patches"sv, "bLoadScreen"sv, true };
	static REX::TOML::Bool<> bDisableBlackLoadingScreens{ "Additional"sv, "bDisableBlackLoadingScreens"sv, false };
	static REX::TOML::Bool<> bDisableAnimationOnLoadingScreens{ "Additional"sv, "bDisableAnimationOnLoadingScreens"sv, false };
	static REX::TOML::F32<> fPostloadingMenuSpeed{ "Additional"sv, "fPostloadingMenuSpeed"sv, 1.0f };

	static RE::BSGraphics::RendererData* g_RendererData{ nullptr };
	static void DrawUILoadScreen(uint32_t a_unk) noexcept;
	decltype(&DrawUILoadScreen) origDrawUI{ nullptr };

	static void DrawUILoadScreen(uint32_t a_unk) noexcept
	{
		REX::W32::ID3D11Texture2D* backBuffer{ nullptr };
		g_RendererData->renderWindow[0].swapChain->GetBuffer(0, REX::W32::IID_ID3D11Texture2D, (void**)&backBuffer);
		if (!backBuffer)
			return;

		REX::W32::ID3D11RenderTargetView* backTarget{ nullptr };
		g_RendererData->device->CreateRenderTargetView(backBuffer, NULL, &backTarget);
		backBuffer->Release();
		if (!backTarget)
			return;

		// fix black screen for ultra wide monitors
		static const float color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		g_RendererData->context->ClearRenderTargetView(backTarget, color);

		backTarget->Release();
		origDrawUI(a_unk);
	}

	static void GetRandomLoadScreen() noexcept
	{
		return;
	}

	namespace loadScreenDetail
	{
		// Scales the post-loading menu fade by the real frame delta instead of a fixed step.
		struct PostLoadSpeed : Xbyak::CodeGenerator
		{
			PostLoadSpeed(std::uintptr_t a_retn, float a_value, std::uintptr_t a_timer)
			{
				Xbyak::Label retn, timer, value;
				movss(xmm0, dword[rip + value]);
				mov(rcx, ptr[rip + timer]);
				mulss(xmm0, dword[rcx]);
				jmp(ptr[rip + retn]);
				L(retn); dq(a_retn + 0x8);
				L(timer); dq(a_timer);
				L(value); dd(std::bit_cast<std::uint32_t>(a_value));
			}
		};
	}

	ModuleLoadScreen::ModuleLoadScreen() :
		Module("Load Screen", &bPatchesLoadScreen)
	{}

	bool ModuleLoadScreen::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleLoadScreen::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (!a_msg)
			return true;

		const bool og = RELEX::IsRuntimeOG();

		if (bDisableBlackLoadingScreens.GetValue())
		{
			const auto site = REL::Relocation<std::uintptr_t>{ REL::ID{ 991513, 2249217, 2249217 }, 0x116 }.address();
			if (*reinterpret_cast<const std::uint8_t*>(site) == 0x75)
				RELEX::WriteSafe(site, { 0xEB });
		}

		if (bDisableAnimationOnLoadingScreens.GetValue())
		{
			const auto site = REL::Relocation<std::uintptr_t>{ REL::ID{ 132841, 2227631, 2227631 }, REL::Offset{ 0x19D, 0x223, 0x223 } }.address();
			if (og)
			{
				static constexpr std::uint8_t cmp[] = { 0x83, 0x7F, 0x68, 0x02 };
				if (std::memcmp(reinterpret_cast<const void*>(site), cmp, sizeof(cmp)) == 0)
					RELEX::WriteSafeNop(site, 0x4);
			}
			else
			{
				static constexpr std::uint8_t cmp[] = { 0x41, 0x83, 0x7E, 0x68, 0x02 };
				if (std::memcmp(reinterpret_cast<const void*>(site), cmp, sizeof(cmp)) == 0)
					RELEX::WriteSafeNop(site, 0x5);
			}
		}

		if (fPostloadingMenuSpeed.GetValue() != 1.0f)
		{
			const auto site = REL::Relocation<std::uintptr_t>{ REL::ID{ 1289136, 2248711, 2248711 }, REL::Offset{ 0x2B, 0x38, 0x2E } }.address();
			static constexpr std::uint8_t mov[] = { 0xF3, 0x0F, 0x10, 0x05 };
			if (std::memcmp(reinterpret_cast<const void*>(site), mov, sizeof(mov)) == 0)
			{
				const auto timer = REL::Relocation<float*>{ REL::ID{ 922988, 2696498, 4803789 }, 0x21C }.address();
				auto& tramp = REL::GetTrampoline();
				loadScreenDetail::PostLoadSpeed code(site, fPostloadingMenuSpeed.GetValue(), timer);
				code.ready();
				const std::int32_t rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(tramp.allocate(code)) - (site + 5));
				const auto* const r = reinterpret_cast<const std::uint8_t*>(&rel);
				RELEX::WriteSafe(site, { 0xE9, r[0], r[1], r[2], r[3] });
				RELEX::WriteSafeNop(site + 5, 0x3);
			}
		}

		// The ultra-wide clear applies while the black screen is shown; the no-op random-loadscreen detour pairs with disabling the animation.
		const bool installAnimNoOp = bDisableAnimationOnLoadingScreens.GetValue();
		const bool installDrawUIClear = !bDisableBlackLoadingScreens.GetValue();

		if (!og)
		{
			if (installAnimNoOp)
				RELEX::DetourJump(REL::ID(2249232).address(), (uintptr_t)&GetRandomLoadScreen);
			if (installDrawUIClear)
			{
				g_RendererData = (RE::BSGraphics::RendererData*)REL::ID(2704527).address();
				origDrawUI = (decltype(&DrawUILoadScreen))(REL::ID(2222551).address());
				RELEX::DetourCall(REL::ID(2249225).address() + 0x3CC, (uintptr_t)&DrawUILoadScreen);
			}
		}
		else if (installDrawUIClear)
		{
			g_RendererData = (RE::BSGraphics::RendererData*)REL::ID(235166).address();
			RELEX::WriteSafe(REL::Relocation{ REL::ID(316170), REL::Offset(0x1B) }.get(), { 0xE9, 0x34, 0x02, 0x00, 0x00, 0x90 });
			origDrawUI = (decltype(&DrawUILoadScreen))(REL::ID(386550).address());
			RELEX::DetourCall(REL::ID(135719).address() + 0x414, (uintptr_t)&DrawUILoadScreen);
		}

		return true;
	}

	bool ModuleLoadScreen::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleLoadScreen::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
