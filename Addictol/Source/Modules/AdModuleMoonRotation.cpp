#include <Modules/AdModuleMoonRotation.h>
#include <Core/AdUtils.h>
#include <xbyak/xbyak.h>

#undef MEM_RELEASE
#undef ERROR
#undef MAX_PATH

#include <RE/B/BSStringT.h>
#include <RE/N/NiNode.h>
#include <RE/B/BSTriShape.h>
#include <RE/S/Sky.h>
#include <RE/M/Moon.h>
#include <RE/C/Calendar.h>

namespace Addictol
{
	constexpr float NI_PI = static_cast<float>(3.1415926535897932);
	constexpr float NI_HALF_PI = 0.5f * NI_PI;

	namespace Math
	{
		static void SetEulerAnglesXYZ(RE::NiMatrix3* a_matrix, float a_x, float a_y, float a_z) noexcept
		{
			const float sinX = std::sinf(a_x);
			const float sinY = std::sinf(a_y);
			const float sinZ = std::sinf(a_z);

			const float cosX = std::cosf(a_x);
			const float cosY = std::cosf(a_y);
			const float cosZ = std::cosf(a_z);

			a_matrix->entry[0][0] = cosY * cosZ;
			a_matrix->entry[0][1] = cosY * sinZ;
			a_matrix->entry[0][2] = -sinY;
			a_matrix->entry[1][0] = sinX * sinY * cosZ - cosX * sinZ;
			a_matrix->entry[1][1] = sinX * sinY * sinZ + cosX * cosZ;
			a_matrix->entry[1][2] = sinX * cosY;
			a_matrix->entry[2][0] = cosX * sinY * cosZ + sinX * sinZ;
			a_matrix->entry[2][1] = cosX * sinY * sinZ - sinX * cosZ;
			a_matrix->entry[2][2] = cosX * cosY;
		}
	}

	namespace Moon
	{
		using TInitThunk = void(RE::Moon* a_moon, RE::NiNode* a_root);
		static std::function<TInitThunk> Init_orig;

		static void Init(RE::Moon* a_moon, RE::NiNode* a_root) noexcept
		{
			// appearance from the east
			if (a_root) 
				Math::SetEulerAnglesXYZ(std::addressof(a_root->local.rotate), .0f, .0f, NI_HALF_PI);
			
			//a_moon->size = 1000;

			Init_orig(a_moon, a_root);

			//a_moon->size = 1000;
		}

		static void HookUpdatePosition(RE::NiNode* a_root) noexcept
		{
			// movement from east to west (original: 19.75 to 7.0)
			auto hour = reinterpret_cast<float*>((uintptr_t)a_root + 0x358);
			*hour = 24.0f - *hour;
		}
	}

	ModuleMoonRotation::ModuleMoonRotation() :
		Module("Moon Rotation", &bFixesMoonRotation)
	{}

	bool ModuleMoonRotation::DoQuery() const noexcept
	{
		if (IsModDLLPresent("MoonRotationFix.dll"))
		{
			Skip("standalone 'MoonRotationFix.dll' is installed"sv);
			return false;
		}

		if (IsModDLLPresent("MoonDirectionFix.dll"))
		{
			Skip("standalone 'MoonDirectionFix.dll' is installed"sv);
			return false;
		}

		if (IsModDLLPresent("MoonMotionFix.dll"))
		{
			Skip("standalone 'MoonMotionFix.dll' is installed"sv);
			return false;
		}

		return true;
	}

	bool ModuleMoonRotation::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// Fixed starts

		const auto target1 = REL::ID{ 114988, 2208804 };
		Moon::Init_orig = reinterpret_cast<Moon::TInitThunk*>(RELEX::TryDetourJump(target1.address(),
			reinterpret_cast<uintptr_t>(&Moon::Init), { 0x48, 0x89, 0x5C, 0x24, 0x10 }));
		if (!Moon::Init_orig)
		{
			REX::WARN("Moon Rotation: unexpected bytes at target - skipping to avoid corruption."sv);
			return false;
		}

		// Fixed direction

		struct HookUpdatePositionPatch : Xbyak::CodeGenerator
		{
			HookUpdatePositionPatch(uintptr_t targetAddr, uintptr_t funcAddr)
			{
				// move node ptr
				mov(rcx, rdi);

				// call our function
				sub(rsp, 0x20);	
				mov(rax, funcAddr);
				call(rax);
				add(rsp, 0x20);

				// orig code
				movss(xmm6, dword[rdi + 0x358]);

				// return back (ret)
				jmp(ptr[rip]);
				dq(targetAddr + 5);
			}
		};

		struct HookUpdatePositionPatch_OG : Xbyak::CodeGenerator
		{
			HookUpdatePositionPatch_OG(uintptr_t targetAddr, uintptr_t funcAddr)
			{
				// move node ptr
				mov(rcx, rdi);

				// call our function
				sub(rsp, 0x20);
				mov(rax, funcAddr);
				call(rax);
				add(rsp, 0x20);

				// orig code
				movss(xmm3, dword[rdi + 0x358]);

				// return back (ret)
				jmp(ptr[rip]);
				dq(targetAddr + 5);
			}
		};

		if (RELEX::IsRuntimeOG())
		{
			const auto target2 = REL::ID(4410).address() + 0x7E;
			if (RELEX::Validate(target2, { 0xF3, 0x0F, 0x10, 0x9F, 0x58, 0x03, 0x00, 0x00 }))
				RELEX::XbyakJump<HookUpdatePositionPatch_OG>(target2, target2,
					reinterpret_cast<uintptr_t>(&Moon::HookUpdatePosition));
			else
			{
				REX::WARN("Moon Rotation: unexpected bytes at target - skipping to avoid corruption."sv);
				return false;
			}
		}
		else
		{
			const auto target2 = REL::ID(2208806).address() + 0x8A;
			if (RELEX::Validate(target2, { 0xF3, 0x0F, 0x10, 0xB7, 0x58, 0x03, 0x00, 0x00 }))
				RELEX::XbyakJump<HookUpdatePositionPatch>(target2, target2,
					reinterpret_cast<uintptr_t>(&Moon::HookUpdatePosition));
			else
			{
				REX::WARN("Moon Rotation: unexpected bytes at target - skipping to avoid corruption."sv);
				return false;
			}
		}

		const auto target3 = REL::Relocation{ target1, REL::Offset{ 0x1E2, 0x1F7 } }.address();
		if (!RELEX::Validate(target3, { 0x04, 0x48, 0x8B, 0x4E, 0x08 }))
		{
			REX::WARN("Moon Rotation: unexpected bytes at target - skipping to avoid corruption."sv);
			return false;
		}

		// Fixed camera
		// Flip the imm8 0x04 -> 0x03 in Moon::Init's or word ptr [node+0x140], 4.
		RELEX::WriteSafe(target3, { 0x03 });

		return true;
	}
}