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

		[[nodiscard]] static bool VerifyBytes(std::uintptr_t a_addr, std::initializer_list<std::uint8_t> a_sig) noexcept
		{
			return std::memcmp(reinterpret_cast<const void*>(a_addr), std::data(a_sig), a_sig.size()) == 0;
		}

		// Overwrites the first 5 bytes of a_site with a near jmp to a_cave, then NOPs out to a_replaceLen.
		static void Branch(std::uintptr_t a_site, void* a_cave, std::size_t a_replaceLen) noexcept
		{
			const std::int32_t rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(a_cave) - (a_site + 5));
			const auto* const r = reinterpret_cast<const std::uint8_t*>(&rel);
			RELEX::WriteSafe(a_site, { 0xE9, r[0], r[1], r[2], r[3] });
			if (a_replaceLen > 5)
				RELEX::WriteSafeNop(a_site + 5, a_replaceLen - 5);
		}

		struct StutterSubsteps : Xbyak::CodeGenerator
		{
			StutterSubsteps(std::uintptr_t a_retn)
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
			StutterClamp(std::uintptr_t a_retn)
			{
				Xbyak::Label retn;
				movss(xmm4, xmm6);
				jmp(ptr[rip + retn]);
				L(retn); dq(a_retn + 0x8);
			}
		};

		struct StutterObjects : Xbyak::CodeGenerator
		{
			StutterObjects(std::uintptr_t a_retn)
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
			WindConst(std::uintptr_t a_retn)
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
			WindTimer9(std::uintptr_t a_retn, std::uintptr_t a_timer)
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
			WindTimer0(std::uintptr_t a_retn, std::uintptr_t a_timer)
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
			GrabRotation(std::uintptr_t a_retn, std::uintptr_t a_timer)
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
			Lockpick(std::uintptr_t a_retn)
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
			SittingX(std::uintptr_t a_retn, std::uintptr_t a_timer)
			{
				Xbyak::Label retn, magic, timer;
				mulss(xmm0, dword[rip + magic]);
				mov(r9, ptr[rip + timer]);
				mulss(xmm0, dword[r9]);
				mulss(xmm0, ptr[rax + 0x4C]);
				jmp(ptr[rip + retn]);
				L(retn); dq(a_retn + 0x5);
				L(magic); dq(std::uintptr_t(Magic1));
				L(timer); dq(a_timer);
			}
		};

		struct SittingY : Xbyak::CodeGenerator
		{
			SittingY(std::uintptr_t a_retn, std::uintptr_t a_timer)
			{
				Xbyak::Label retn, magic, timer;
				mulss(xmm1, dword[rip + magic]);
				mov(r9, ptr[rip + timer]);
				mulss(xmm1, dword[r9]);
				movss(xmm0, ptr[rbx + 0x64]);
				jmp(ptr[rip + retn]);
				L(retn); dq(a_retn + 0x5);
				L(magic); dq(std::uintptr_t(Magic1));
				L(timer); dq(a_timer);
			}
		};

		struct WorkshopRotation : Xbyak::CodeGenerator
		{
			WorkshopRotation(std::uintptr_t a_retn, std::uintptr_t a_timer)
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
			StuckAnim(std::uintptr_t a_retn)
			{
				Xbyak::Label retn, magic;
				movss(xmm3, dword[rip + magic]);
				jmp(ptr[rip + retn]);
				L(retn); dq(a_retn + 0x5);
				L(magic); dd(0x3c8b4396);  // 0.017
			}
		};

		// OG 1.10.163 cave variants: the engine uses different scratch registers, constants and instruction
		// lengths than NG/AE at these four sites, so OG needs its own caves (offsets come from the triples).
		struct StutterSubstepsOG : Xbyak::CodeGenerator
		{
			StutterSubstepsOG(std::uintptr_t a_retn)
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
			StutterClampOG(std::uintptr_t a_retn)
			{
				Xbyak::Label retn;
				movss(xmm2, xmm6);
				jmp(ptr[rip + retn]);
				L(retn); dq(a_retn + 0x6);
			}
		};

		struct GrabRotationOG : Xbyak::CodeGenerator
		{
			GrabRotationOG(std::uintptr_t a_retn, std::uintptr_t a_timer)
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
				L(fwd); dq(std::uintptr_t(Magic1));   // 58.8235
				L(rev); dq(std::uintptr_t(Magic2));   // -58.8235
			}
		};

		struct WorkshopRotationOG : Xbyak::CodeGenerator
		{
			WorkshopRotationOG(std::uintptr_t a_retn, std::uintptr_t a_timer)
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

		// Untie advances the simulation by the real frame delta instead of a floored fixed step, and force
		// iFPSClamp to 0 so it can't re-tie speed to a fixed FPS.
		if (bUntieSpeed.GetValue())
		{
			const auto untie = REL::Relocation<std::uintptr_t>{ REL::ID{ 462873, 2267969, 2267969 }, REL::Offset{ 0x6B, 0x5F, 0x61 } }.address();
			if (VerifyBytes(untie, { 0x08, 0x00, 0x00, 0x00 }))
				RELEX::WriteSafe(untie, { 0x00 });

			if (auto* coll = RE::INISettingCollection::GetSingleton())
				if (auto* clamp = coll->GetSetting("iFPSClamp:General"sv); clamp && clamp->GetInt() != 0)
					clamp->SetInt(0);
		}

		if (bFixWhiteScreen.GetValue())
		{
			const auto site = REL::Relocation<std::uintptr_t>{ REL::ID{ 703643, 2258401, 2258401 }, REL::Offset{ 0x13, 0x10, 0x10 } }.address();
			if (og ? VerifyBytes(site, { 0x74, 0x23, 0x83, 0xE9, 0x02 }) : VerifyBytes(site, { 0x74, 0x20, 0x83, 0xE9, 0x02 }))
				RELEX::WriteSafeNop(site, og ? 0x3C : 0x35);
		}

		if (bFixMotionResponsive.GetValue())
		{
			const auto site = REL::Relocation<std::uintptr_t>{ REL::ID{ 1201084, 2196089, 2196089 }, REL::Offset{ 0x9F7, 0x9FE, 0x9FE } }.address();
			if (VerifyBytes(site, { 0x73 }))
				RELEX::WriteSafe(site, { 0xEB });
		}

		const auto frameTimer = REL::Relocation<float*>{ REL::ID{ 922988, 2696498, 4803789 }, 0x21C }.address();
		const auto frameTimerSlow = REL::Relocation<float*>{ REL::ID{ 922988, 2696498, 4803789 }, 0x218 }.address();
		auto& tramp = REL::GetTrampoline();

		if (bFixStuttering.GetValue())
		{
			const auto substeps = REL::Relocation<std::uintptr_t>{ REL::ID{ 12890, 2277709, 2277709 }, REL::Offset{ 0x196, 0x169, 0x169 } }.address();
			if (og)
			{
				if (VerifyBytes(substeps, { 0xF3, 0x48, 0x0F, 0x2C, 0xCB }))
				{
					StutterSubstepsOG code(substeps); code.ready();
					Branch(substeps, tramp.allocate(code), 0x5);
				}
			}
			else if (VerifyBytes(substeps, { 0xF3, 0x48, 0x0F, 0x2C, 0xCD }))
			{
				StutterSubsteps code(substeps); code.ready();
				Branch(substeps, tramp.allocate(code), 0x5);
			}

			const auto write = REL::Relocation<std::uintptr_t>{ REL::ID{ 1395106, 2277710, 2277710 }, REL::Offset{ 0x1A1, 0x19B, 0x19B } }.address();
			if (VerifyBytes(write, { 0xF3, 0x0F, 0x11, 0x0D }))
				RELEX::WriteSafeNop(write, 0x8);

			const auto clamp = REL::Relocation<std::uintptr_t>{ REL::ID{ 12890, 2277709, 2277709 }, REL::Offset{ 0x145, 0x122, 0x122 } }.address();
			if (og)
			{
				if (VerifyBytes(clamp, { 0xF3, 0x0F, 0x5D, 0x54, 0x24, 0x20 }))
				{
					StutterClampOG code(clamp); code.ready();
					Branch(clamp, tramp.allocate(code), 0x6);
					RELEX::WriteSafeNop(clamp + 0xE, 0x4);
				}
			}
			else if (VerifyBytes(clamp, { 0xF3, 0x0F, 0x5D, 0x25 }))
			{
				StutterClamp code(clamp); code.ready();
				Branch(clamp, tramp.allocate(code), 0x8);
				RELEX::WriteSafeNop(clamp + 0x10, 0x8);
			}

			const auto objects = REL::Relocation<std::uintptr_t>{ REL::ID{ 754666, 2255886, 2255886 } }.address();
			if (VerifyBytes(objects, { 0xF3, 0x41, 0x0F, 0x10, 0x08 }))
			{
				StutterObjects code(objects); code.ready();
				Branch(objects, tramp.allocate(code), 0x5);
			}
		}

		if (bFixWindSpeed.GetValue())
		{
			const auto a = REL::Relocation<std::uintptr_t>{ REL::ID{ 1469635, 2278751, 2278751 }, REL::Offset{ 0x21, 0x24, 0x24 } }.address();
			if (VerifyBytes(a, { 0x0F, 0x29, 0x74, 0x24, 0x30 }))
			{
				WindConst code(a); code.ready();
				Branch(a, tramp.allocate(code), 0x8);
			}

			const auto b = REL::Relocation<std::uintptr_t>{ REL::ID{ 1164603, 2277711, 2277711 }, REL::Offset{ 0x9E, 0x115, 0x115 } }.address();
			if (VerifyBytes(b, { 0xF3, 0x44, 0x0F, 0x10, 0x0D }))
			{
				WindTimer9 code(b, frameTimerSlow); code.ready();
				Branch(b, tramp.allocate(code), 0x9);
			}

			const auto c = REL::Relocation<std::uintptr_t>{ REL::ID{ 1164603, 2277711, 2277711 }, REL::Offset{ 0x147, 0x1B7, 0x1B7 } }.address();
			if (VerifyBytes(c, { 0xF3, 0x44, 0x0F, 0x10, 0x0D }))
			{
				WindTimer9 code(c, frameTimerSlow); code.ready();
				Branch(c, tramp.allocate(code), 0x9);
			}

			const auto d = REL::Relocation<std::uintptr_t>{ REL::ID{ 1164603, 2277711, 2277711 }, REL::Offset{ 0x32B, 0x3BD, 0x3BD } }.address();
			if (VerifyBytes(d, { 0xF3, 0x0F, 0x10, 0x05 }))
			{
				WindTimer0 code(d, frameTimerSlow); code.ready();
				Branch(d, tramp.allocate(code), 0x8);
			}
		}

		if (bFixRotationSpeed.GetValue())
		{
			const auto rot = REL::Relocation<std::uintptr_t>{ REL::ID{ 457276, 2234879, 2234879 }, REL::Offset{ 0xE1, 0x6E, 0x6E } }.address();
			if (VerifyBytes(rot, { 0x75, 0x0A, 0xF3, 0x0F, 0x10, 0x15 }))
			{
				if (og)
				{
					GrabRotationOG code(rot, frameTimer); code.ready();
					Branch(rot, tramp.allocate(code), 0x19);
				}
				else
				{
					GrabRotation code(rot, frameTimer); code.ready();
					Branch(rot, tramp.allocate(code), 0x19);
				}
			}

			const auto lock = REL::Relocation<std::uintptr_t>{ REL::ID{ 676000, 2249260, 2249260 }, REL::Offset{ 0x42, 0x42, 0x42 } }.address();
			if (VerifyBytes(lock, { 0xF3, 0x0F, 0x59, 0x0D }))
			{
				Lockpick code(lock); code.ready();
				Branch(lock, tramp.allocate(code), 0x8);
			}
		}

		if (bFixSittingRotation.GetValue())
		{
			const auto x = REL::Relocation<std::uintptr_t>{ REL::ID{ 533372, 2248271, 2248271 }, REL::Offset{ 0xC0, 0xC0, 0xC0 } }.address();
			if (VerifyBytes(x, { 0xF3, 0x0F, 0x59, 0x40, 0x4C }))
			{
				SittingX code(x, frameTimer); code.ready();
				Branch(x, tramp.allocate(code), 0x5);
			}

			const auto y = REL::Relocation<std::uintptr_t>{ REL::ID{ 533372, 2248271, 2248271 }, REL::Offset{ 0xD7, 0xDE, 0xDE } }.address();
			if (VerifyBytes(y, { 0xF3, 0x0F, 0x10, 0x43, 0x64 }))
			{
				SittingY code(y, frameTimer); code.ready();
				Branch(y, tramp.allocate(code), 0x5);
			}
		}

		if (bFixWorkshopRotation.GetValue())
		{
			const auto ws = REL::Relocation<std::uintptr_t>{ REL::ID{ 1144472, 2195211, 2195211 }, REL::Offset{ 0xA2, 0x94, 0x94 } }.address();
			if (og)
			{
				if (VerifyBytes(ws, { 0xF3, 0x0F, 0x59, 0x0D }))
				{
					WorkshopRotationOG code(ws, frameTimer); code.ready();
					Branch(ws, tramp.allocate(code), 0x8);
				}
			}
			else if (VerifyBytes(ws, { 0xF3, 0x0F, 0x59, 0x05 }))
			{
				WorkshopRotation code(ws, frameTimer); code.ready();
				Branch(ws, tramp.allocate(code), 0x8);
			}
		}

		if (bFixStuckAnimation.GetValue())
		{
			const auto stuck = REL::Relocation<std::uintptr_t>{ REL::ID{ 463133, 2302542, 2302542 }, REL::Offset{ 0xA9, 0xA9, 0xA9 } }.address();
			if (VerifyBytes(stuck, { 0xF3, 0x41, 0x0F, 0x10, 0x1E }))
			{
				StuckAnim code(stuck); code.ready();
				Branch(stuck, tramp.allocate(code), 0x5);
			}
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
