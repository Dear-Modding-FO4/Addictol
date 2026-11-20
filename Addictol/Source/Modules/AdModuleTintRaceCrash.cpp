#include <Modules/AdModuleTintRaceCrash.h>
#include <Core/AdUtils.h>

#include <Core/AdXbyak.h>

namespace Addictol
{
	namespace tintRaceCrashDetail
	{
		struct LoadGamePatch : RELEX::Stub // TESNPC::LoadGame
		{
			LoadGamePatch(std::uintptr_t target)
			{
				mov(rbx, ptr[rsi + 0x1B8]);

				test(rbx, rbx);
				jz("nullptr");
				JumpTo(target + 0x7);
				
				L("nullptr");
				JumpTo(target + 0x24);
			}
		};
	}

	ModuleTintRaceCrash::ModuleTintRaceCrash() :
		Module("Tint Race Crash", &bFixesTintRaceCrash)
	{}

	bool ModuleTintRaceCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		auto target = REL::Relocation{ REL::ID{ 1411152, 2207485 }, REL::Offset{ 0xB50, 0xA37 } }.address();
		RELEX::XbyakJump<tintRaceCrashDetail::LoadGamePatch>(target, target);

		return true;
	}
}
