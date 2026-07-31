#include <Modules/AdModulePhysicsFix.h>
#include <AdUtils.h>
#include <RE/S/Setting.h>

#include <xbyak/xbyak.h>

#include <cstring>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesPhysicsFix{ "Patches"sv, "bPhysicsFix"sv, true };
	static REX::TOML::Bool<> bUntieSpeed{ "HighFPSPhysics"sv, "bUntieSpeedFromFPS"sv, true };
	static REX::TOML::Bool<> bFixStuttering{ "HighFPSPhysics"sv, "bFixStuttering"sv, true };
	static REX::TOML::Bool<> bFixWhiteScreen{ "HighFPSPhysics"sv, "bFixWhiteScreen"sv, true };
	static REX::TOML::Bool<> bFixWindSpeed{ "HighFPSPhysics"sv, "bFixWindSpeed"sv, true };
	static REX::TOML::Bool<> bFixRotationSpeed{ "HighFPSPhysics"sv, "bFixRotationSpeed"sv, true };
	static REX::TOML::Bool<> bFixSittingRotation{ "HighFPSPhysics"sv, "bFixSittingRotationSpeed"sv, true };
	static REX::TOML::Bool<> bFixWorkshopRotation{ "HighFPSPhysics"sv, "bFixWorkshopRotationSpeed"sv, true };
	static REX::TOML::Bool<> bFixStuckAnimation{ "HighFPSPhysics"sv, "bFixStuckAnimation"sv, true };
	static REX::TOML::Bool<> bFixMotionResponsive{ "HighFPSPhysics"sv, "bFixMotionResponsive"sv, true };

	namespace physicsDetail
	{
		static constexpr std::int32_t Magic1 = 0x426b4b44;   // 58.8235
		static constexpr std::int32_t Magic2 = 0xc26b4b44;   // -58.8235

		struct StutterSubsteps : Xbyak::CodeGenerator
		{
			explicit StutterSubsteps(uintptr_t a_retn) noexcept
			{
				Xbyak::Label retn, magic;
				movss(xmm5, dword[rip + magic]);
				cvttss2si(rcx, xmm5);
				jmp(ptr[rip + retn]);
				L(retn); dq(a_retn + 0x5);
				L(magic); dd(0x3f800000);  // 1.0
			}
		};

		struct StutterClamp : Xbyak::CodeGenerator
		{
			explicit StutterClamp(uintptr_t a_retn) noexcept
			{
				Xbyak::Label retn;
				movss(xmm4, xmm6);
				jmp(ptr[rip + retn]);
				L(retn); dq(a_retn + 0x8);
			}
		};

		struct StutterObjects : Xbyak::CodeGenerator
		{
			explicit StutterObjects(uintptr_t a_retn) noexcept
			{
				Xbyak::Label retn, magic;
				movss(xmm1, dword[rip + magic]);
				jmp(ptr[rip + retn]);
				L(retn); dq(a_retn + 0x5);
				L(magic); dd(0x3c88888a);  // 0.016667
			}
		};

		struct WindConst : Xbyak::CodeGenerator
		{
			explicit WindConst(uintptr_t a_retn) noexcept
			{
				Xbyak::Label retn, magic;
				movaps(ptr[rsp + 0x30], xmm6);
				movss(xmm6, dword[rip + magic]);
				jmp(ptr[rip + retn]);
				L(retn); dq(a_retn + 0x8);
				L(magic); dd(0x3c88888a);  // 0.016667
			}
		};

		struct WindTimer9 : Xbyak::CodeGenerator
		{
			explicit WindTimer9(uintptr_t a_retn, uintptr_t a_timer) noexcept
			{
				Xbyak::Label retn, timer;
				mov(rcx, ptr[rip + timer]);
				movss(xmm9, dword[rcx]);
				jmp(ptr[rip + retn]);
				L(retn); dq(a_retn + 0x9);
				L(timer); dq(a_timer);
			}
		};

		struct WindTimer0 : Xbyak::CodeGenerator
		{
			explicit WindTimer0(uintptr_t a_retn, uintptr_t a_timer) noexcept
			{
				Xbyak::Label retn, timer;
				mov(r8, ptr[rip + timer]);
				movss(xmm0, dword[r8]);
				jmp(ptr[rip + retn]);
				L(retn); dq(a_retn + 0x8);
				L(timer); dq(a_timer);
			}
		};

		struct GrabRotation : Xbyak::CodeGenerator
		{
			explicit GrabRotation(uintptr_t a_retn, uintptr_t a_timer) noexcept
			{
				Xbyak::Label retn, timer, jneL, jmpL, fwd, rev;
				jne(jneL);
				movss(xmm2, dword[rip + fwd]);
				mov(rcx, ptr[rip + timer]);
				mulss(xmm2, dword[rcx]);
				jmp(jmpL);
				L(jneL);
				movss(xmm2, dword[rip + rev]);
				mov(rcx, ptr[rip + timer]);
				mulss(xmm2, dword[rcx]);
				L(jmpL);
				mulss(xmm2, ptr[rdi + 0x38]);
				jmp(ptr[rip + retn]);
				L(retn); dq(a_retn + 0x19);
				L(timer); dq(a_timer);
				L(fwd); dd(0x403c3c3c);   // 2.94118
				L(rev); dd(0xc03c3c4b);   // -2.94118
			}
		};

		struct Lockpick : Xbyak::CodeGenerator
		{
			explicit Lockpick(uintptr_t a_retn) noexcept
			{
				Xbyak::Label retn, magic;
				mulss(xmm1, dword[rip + magic]);
				jmp(ptr[rip + retn]);
				L(retn); dq(a_retn + 0x8);
				L(magic); dd(0x3c88888a);  // 0.016667
			}
		};

		struct SittingX : Xbyak::CodeGenerator
		{
			explicit SittingX(uintptr_t a_retn, uintptr_t a_timer) noexcept
			{
				Xbyak::Label retn, magic, timer;
				mulss(xmm0, dword[rip + magic]);
				mov(r9, ptr[rip + timer]);
				mulss(xmm0, dword[r9]);
				mulss(xmm0, ptr[rax + 0x4C]);
				jmp(ptr[rip + retn]);
				L(retn); dq(a_retn + 0x5);
				L(magic); dq(uintptr_t(Magic1));
				L(timer); dq(a_timer);
			}
		};

		struct SittingY : Xbyak::CodeGenerator
		{
			explicit SittingY(uintptr_t a_retn, uintptr_t a_timer) noexcept
			{
				Xbyak::Label retn, magic, timer;
				mulss(xmm1, dword[rip + magic]);
				mov(r9, ptr[rip + timer]);
				mulss(xmm1, dword[r9]);
				movss(xmm0, ptr[rbx + 0x64]);
				jmp(ptr[rip + retn]);
				L(retn); dq(a_retn + 0x5);
				L(magic); dq(uintptr_t(Magic1));
				L(timer); dq(a_timer);
			}
		};

		struct WorkshopRotation : Xbyak::CodeGenerator
		{
			explicit WorkshopRotation(uintptr_t a_retn, uintptr_t a_timer) noexcept
			{
				Xbyak::Label retn, timer;
				mov(rax, ptr[rip + timer]);
				mulss(xmm0, dword[rax]);
				jmp(ptr[rip + retn]);
				L(retn); dq(a_retn + 0x8);
				L(timer); dq(a_timer);
			}
		};

		struct StuckAnim : Xbyak::CodeGenerator
		{
			explicit StuckAnim(uintptr_t a_retn) noexcept
			{
				Xbyak::Label retn, magic;
				movss(xmm3, dword[rip + magic]);
				jmp(ptr[rip + retn]);
				L(retn); dq(a_retn + 0x5);
				L(magic); dd(0x3c8b4396);  // 0.017
			}
		};

		// OG cave variants: different scratch registers and lengths than NG/AE.
		struct StutterSubstepsOG : Xbyak::CodeGenerator
		{
			explicit StutterSubstepsOG(uintptr_t a_retn) noexcept
			{
				Xbyak::Label retn, magic;
				movss(xmm3, dword[rip + magic]);
				cvttss2si(rcx, xmm3);
				jmp(ptr[rip + retn]);
				L(retn); dq(a_retn + 0x5);
				L(magic); dd(0x3f800000);  // 1.0
			}
		};

		struct StutterClampOG : Xbyak::CodeGenerator
		{
			explicit StutterClampOG(uintptr_t a_retn) noexcept
			{
				Xbyak::Label retn;
				movss(xmm2, xmm6);
				jmp(ptr[rip + retn]);
				L(retn); dq(a_retn + 0x6);
			}
		};

		struct GrabRotationOG : Xbyak::CodeGenerator
		{
			explicit GrabRotationOG(uintptr_t a_retn, uintptr_t a_timer) noexcept
			{
				Xbyak::Label retn, timer, jneL, jmpL, fwd, rev;
				jne(jneL);
				movss(xmm2, dword[rip + fwd]);
				mov(rcx, ptr[rip + timer]);
				mulss(xmm2, dword[rcx]);
				jmp(jmpL);
				L(jneL);
				movss(xmm2, dword[rip + rev]);
				mov(rcx, ptr[rip + timer]);
				mulss(xmm2, dword[rcx]);
				L(jmpL);
				mulss(xmm2, ptr[rbx + 0x38]);
				jmp(ptr[rip + retn]);
				L(retn); dq(a_retn + 0x19);
				L(timer); dq(a_timer);
				L(fwd); dq(uintptr_t(Magic1));   // 58.8235
				L(rev); dq(uintptr_t(Magic2));   // -58.8235
			}
		};

		struct WorkshopRotationOG : Xbyak::CodeGenerator
		{
			explicit WorkshopRotationOG(uintptr_t a_retn, uintptr_t a_timer) noexcept
			{
				Xbyak::Label retn, timer;
				mov(rax, ptr[rip + timer]);
				mulss(xmm1, dword[rax]);
				jmp(ptr[rip + retn]);
				L(retn); dq(a_retn + 0x8);
				L(timer); dq(a_timer);
			}
		};
	}

	ModulePhysicsFix::ModulePhysicsFix() :
		Module("High FPS Physics", &bPatchesPhysicsFix)
	{}

	bool ModulePhysicsFix::DoQuery() const noexcept
	{
		return true;
	}

	bool ModulePhysicsFix::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace physicsDetail;

		const bool og = RELEX::IsRuntimeOG();

		// Untie the sim step from FPS and force iFPSClamp to 0.
		if (bUntieSpeed.GetValue())
		{
			const auto untie = REL::Relocation{ REL::ID{ 462873, 2267969 }, REL::Offset{ 0x6B, 0x5F, 0x61 } }.address();
			if (RELEX::Validate(untie, { 0x08, 0x00, 0x00, 0x00 }))
				RELEX::WriteSafe(untie, { 0x00 });

			if (auto* coll = RE::INISettingCollection::GetSingleton())
				if (auto* clamp = coll->GetSetting("iFPSClamp:General"sv); clamp && clamp->GetInt() != 0)
					clamp->SetInt(0);
		}

		if (bFixWhiteScreen.GetValue())
		{
			const auto site = REL::Relocation{ REL::ID{ 703643, 2258401 }, REL::Offset{ 0x13, 0x10 } }.address();
			if (og ? RELEX::Validate(site, { 0x74, 0x23, 0x83, 0xE9, 0x02 }) : RELEX::Validate(site, { 0x74, 0x20, 0x83, 0xE9, 0x02 }))
				RELEX::WriteSafeNop(site, og ? 0x3C : 0x35);
		}

		if (bFixMotionResponsive.GetValue())
		{
			const auto site = REL::Relocation{ REL::ID{ 1201084, 2196089 }, REL::Offset{ 0x9F7, 0x9FE } }.address();
			if (RELEX::Validate(site, { 0x73 }))
				RELEX::WriteSafe(site, { 0xEB });
		}

		auto frameTimer = REL::Relocation<float*>{ REL::ID{ 922988, 2696498, 4803789 }, 0x21C }.address();
		auto frameTimerSlow = REL::Relocation<float*>{ REL::ID{ 922988, 2696498, 4803789 }, 0x218 }.address();

		if (bFixStuttering.GetValue())
		{
			const auto substeps = REL::Relocation{ REL::ID{ 12890, 2277709 }, REL::Offset{ 0x196, 0x169 } }.address();
			if (og)
			{
				if (RELEX::Validate(substeps, { 0xF3, 0x48, 0x0F, 0x2C, 0xCB }))
					RELEX::XbyakJump<StutterSubstepsOG>(substeps, substeps + 0x5);
			}
			else if (RELEX::Validate(substeps, { 0xF3, 0x48, 0x0F, 0x2C, 0xCD }))
				RELEX::XbyakJump<StutterSubsteps>(substeps, substeps + 0x5);

			auto write = REL::Relocation{ REL::ID{ 1395106, 2277710 }, REL::Offset{ 0x1A1, 0x19B } }.address();
			if (RELEX::Validate(write, { 0xF3, 0x0F, 0x11, 0x0D }))
				RELEX::WriteSafeNop(write, 0x8);

			const auto clamp = REL::Relocation{ REL::ID{ 12890, 2277709 }, REL::Offset{ 0x145, 0x122 } }.address();
			if (og)
			{
				if (RELEX::Validate(clamp, { 0xF3, 0x0F, 0x5D, 0x54, 0x24, 0x20 }))
				{
					RELEX::WriteSafeNop(clamp, 0x12);
					RELEX::XbyakJump<StutterClampOG>(clamp, clamp + 0x12);
				}
			}
			else if (RELEX::Validate(clamp, { 0xF3, 0x0F, 0x5D, 0x25 }))
			{
				RELEX::WriteSafeNop(clamp, 0x18);
				RELEX::XbyakJump<StutterClamp>(clamp, clamp + 0x18);
			}

			const auto objects = REL::Relocation{ REL::ID{ 754666, 2255886 } }.address();
			if (RELEX::Validate(objects, { 0xF3, 0x41, 0x0F, 0x10, 0x08 }))
				RELEX::XbyakJump<StutterObjects>(objects, objects + 0x5);
		}

		if (bFixWindSpeed.GetValue())
		{
			const auto a = REL::Relocation{ REL::ID{ 1469635, 2278751 }, REL::Offset{ 0x21, 0x24 } }.address();
			if (RELEX::Validate(a, { 0x0F, 0x29, 0x74, 0x24, 0x30 }))
			{
				RELEX::WriteSafeNop(a, 0x8);
				RELEX::XbyakJump<WindConst>(a, a + 0x8);
			}

			const REL::ID idTarget{ 1164603, 2277711 };

			const auto b = REL::Relocation{ idTarget, REL::Offset{ 0x9E, 0x115 } }.address();
			if (RELEX::Validate(b, { 0xF3, 0x44, 0x0F, 0x10, 0x0D }))
			{
				RELEX::WriteSafeNop(b, 0x9);
				RELEX::XbyakJump<WindTimer9>(b, b + 0x9, frameTimerSlow);
			}

			const auto c = REL::Relocation{ idTarget, REL::Offset{ 0x147, 0x1B7 } }.address();
			if (RELEX::Validate(c, { 0xF3, 0x44, 0x0F, 0x10, 0x0D }))
			{
				RELEX::WriteSafeNop(c, 0x9);
				RELEX::XbyakJump<WindTimer9>(c, c + 0x9, frameTimerSlow);
			}

			const auto d = REL::Relocation{ idTarget, REL::Offset{ 0x32B, 0x3BD } }.address();
			if (RELEX::Validate(d, { 0xF3, 0x0F, 0x10, 0x05 }))
			{
				RELEX::WriteSafeNop(d, 0x8);
				RELEX::XbyakJump<WindTimer0>(d, d + 0x8, frameTimerSlow);
			}
		}

		if (bFixRotationSpeed.GetValue())
		{
			const auto rot = REL::Relocation{ REL::ID{ 457276, 2234879 }, REL::Offset{ 0xE1, 0x6E } }.address();		
			if (RELEX::Validate(rot, { 0x75, 0x0A, 0xF3, 0x0F, 0x10, 0x15 }))
			{
				RELEX::WriteSafeNop(rot, 0x19);
				if (og)
					RELEX::XbyakJump<GrabRotationOG>(rot, rot + 0x19, frameTimer);
				else
					RELEX::XbyakJump<GrabRotation>(rot, rot + 0x19, frameTimer);
			}

			const auto lock = REL::Relocation{ REL::ID{ 676000, 2249260 }, REL::Offset{ 0x42 } }.address();
			if (RELEX::Validate(lock, { 0xF3, 0x0F, 0x59, 0x0D }))
			{
				RELEX::WriteSafeNop(lock, 0x8);
				RELEX::XbyakJump<Lockpick>(lock, lock + 0x8);
			}
		}

		if (bFixSittingRotation.GetValue())
		{
			const REL::ID idTarget{ 1164603, 2277711 };
			const auto x = REL::Relocation{ idTarget, REL::Offset{ 0xC0 } }.address();
			if (RELEX::Validate(x, { 0xF3, 0x0F, 0x59, 0x40, 0x4C }))
				RELEX::XbyakJump<SittingX>(x, x + 0x5, frameTimer);

			const auto y = REL::Relocation{ idTarget, REL::Offset{ 0xD7, 0xDE } }.address();
			if (RELEX::Validate(y, { 0xF3, 0x0F, 0x10, 0x43, 0x64 }))
				RELEX::XbyakJump<SittingY>(y, y + 0x5, frameTimer);
		}

		if (bFixWorkshopRotation.GetValue())
		{
			const auto ws = REL::Relocation{ REL::ID{ 1144472, 2195211 }, REL::Offset{ 0xA2, 0x94 } }.address();

			if (og)
			{
				if (RELEX::Validate(ws, { 0xF3, 0x0F, 0x59, 0x0D }))
				{
					RELEX::WriteSafeNop(ws, 0x8);
					RELEX::XbyakJump<WorkshopRotationOG>(ws, ws + 0x8, frameTimer);
				}
			}
			else if (RELEX::Validate(ws, { 0xF3, 0x0F, 0x59, 0x05 }))
			{
				RELEX::WriteSafeNop(ws, 0x8);
				RELEX::XbyakJump<WorkshopRotation>(ws, ws + 0x8, frameTimer);
			}
		}

		if (bFixStuckAnimation.GetValue())
		{
			const auto stuck = REL::Relocation{ REL::ID{ 463133, 2302542 }, REL::Offset{ 0xA9 } }.address();
			if (RELEX::Validate(stuck, { 0xF3, 0x41, 0x0F, 0x10, 0x1E }))
				RELEX::XbyakJump<StuckAnim>(stuck, stuck + 0x5);
		}

		return true;
	}

	bool ModulePhysicsFix::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModulePhysicsFix::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
