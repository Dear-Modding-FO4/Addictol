#include <Modules\AdModuleGreyMovie.h>
#include <AdUtils.h>
#include <Scaleform\G\GFx_ASMovieRootBase.h>
#include <Scaleform\G\GFx_Value.h>
#include <Scaleform\G\GFx_Movie.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesGreyMovie{ "Fixes"sv, "bGreyMovie"sv, true };

	static void HKGfxSetBGAlpha(Scaleform::GFx::Movie* self, float) noexcept
	{
		Scaleform::GFx::Value alpha;

		if (!self->asMovieRoot->GetVariable(std::addressof(alpha), "BackgroundAlpha"))
			alpha = 0.0f;

		// Set alpha
		self->SetBackgroundAlpha((float)alpha.GetNumber());
	}

	ModuleGreyMovie::ModuleGreyMovie() :
		Module("Module Grey Movie", &bFixesGreyMovie)
	{}

	bool ModuleGreyMovie::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleGreyMovie::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (!RELEX::IsRuntimeOG())
		{
			// NG/AE

			// mov rcx, rbx
			// test al, al
			// je fallout4.7FF7DD38FEAF
			// movsd xmm1, qword ptr ss:[rsp+0x40]
			// cvtpd2ps xmm1, xmm1
			// minss xmm1, dword ptr ds:[0x7FF7DDC03D00]
			// maxss xmm1, xmm7
			// jmp fallout4.7FF7DD38FEB7
			// movss xmm1, dword ptr ss:[rbp+0x150]

			auto Target = REL::ID(2287422).address() + 0x279;
			//REX::INFO("[DBG]\tTarget 0x{:X}", Target - REL::Module::GetSingleton()->base());
			RELEX::WriteSafe(Target, { 0x48, 0x8B, 0xCB, 0x84, 0xC0, 0x74, 0x18, 0xF2, 0x0F, 0x10, 0x4C, 0x24, 0x40, 0x66, 0x0F, 0x5A, 0xC9, 0xF3,
				0x0F, 0x5D, 0x0D, 0x5E, 0x3E, 0x87, 0x00, 0xF3, 0x0F, 0x5F, 0xCF, 0xEB, 0x08, 0xF3, 0x0F, 0x10, 0x8D, 0x50, 0x01, 0x00, 0x00,
				0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 });
			RELEX::DetourCall(Target + 0x27, (uintptr_t)&HKGfxSetBGAlpha);
		}
		else
		{
			// OG
			const REL::Relocation<std::uintptr_t> target{ REL::ID(1526234), REL::Offset(0x216) };
			auto& trampoline = REL::GetTrampoline();
			trampoline.write_call<6>(target.address(), HKGfxSetBGAlpha);
		}

		return true;
	}

	bool ModuleGreyMovie::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleGreyMovie::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}