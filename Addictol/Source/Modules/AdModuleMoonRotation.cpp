#include <Modules/AdModuleMoonRotation.h>
#include <Core/AdUtils.h>

#include <RE/B/BSStringT.h>
#include <RE/N/NiNode.h>
#include <RE/B/BSTriShape.h>
#include <RE/S/Sky.h>
#include <RE/M/Moon.h>

namespace Addictol
{
	constexpr float NI_PI = static_cast<float>(3.1415926535897932);
	constexpr float NI_HALF_PI = 0.5F * NI_PI;

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
			if (a_root) 
				Math::SetEulerAnglesXYZ(std::addressof(a_root->local.rotate), .0f, .0f, NI_HALF_PI);

			Init_orig(a_moon, a_root);
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
		Moon::Init_orig = reinterpret_cast<Moon::TInitThunk*>(RELEX::TryDetourJump(REL::ID{ 114988, 2208804 }.address(), 
			reinterpret_cast<uintptr_t>(&Moon::Init), { 0x48, 0x89, 0x5C, 0x24, 0x10 }));
		if (!Moon::Init_orig)
		{
			REX::WARN("Moon Rotation: unexpected bytes at target - skipping to avoid corruption."sv);
			return false;
		}

		return true;
	}
}
