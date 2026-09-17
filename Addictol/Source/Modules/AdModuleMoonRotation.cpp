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
	namespace Moon
	{
		constexpr float NI_PI = static_cast<float>(3.1415926535897932);

		// Function to convert degrees to radians
		constexpr static double Deg2Rad(double a_degrees) noexcept
		{
			return a_degrees * (NI_PI / 180.0);
		}

		class LunarRotationCalculator
		{
			// The Moon rotates exactly 360 degrees in one Sidereal Month (27.321661 days)
#if 0
			// FOR TEST
			const double DAYS_PER_SIDEREAL_MONTH = .00321661;
#else
			const double DAYS_PER_SIDEREAL_MONTH = 27.321661;
#endif
			const double HOURS_PER_DAY = 24.0;

			// Derived total hours for a single full 360-degree rotation
			const double HOURS_PER_ROTATION = DAYS_PER_SIDEREAL_MONTH * HOURS_PER_DAY;

			// Degrees the moon rotates in exactly 1 hour (~0.549°/hr)
			const double DEGREES_PER_HOUR = 360.0 / HOURS_PER_ROTATION;

			// The Moon's orbit is tilted at approximately 5.14° relative to Earth's orbital plane (the ecliptic)
			const float DEGREES_INCLINATION = 5.14f;

			const float cosInclinationTheta{ std::cosf(static_cast<float>(Deg2Rad(DEGREES_INCLINATION))) };
			const float sinInclinationTheta{ std::sinf(static_cast<float>(Deg2Rad(DEGREES_INCLINATION))) };
		public:
			constexpr LunarRotationCalculator() = default;

			double CalculateRotationDeg(double a_hours, bool a_clampTo360 = true) const noexcept
			{
				double totalDegrees = a_hours * DEGREES_PER_HOUR;

				if (a_clampTo360)
				{
					totalDegrees = std::fmod(totalDegrees, 360.0);
					if (totalDegrees < 0.0)
						totalDegrees += 360.0; // Handle negative elapsed hours gracefully
				}

				return totalDegrees;
			}

			double CalculateRotationRad(double a_hours, bool a_clampTo360 = true) const noexcept
			{
				return Deg2Rad(CalculateRotationDeg(a_hours, a_clampTo360));
			}

			void ApplyInclination(RE::NiPoint3& a_translate) const noexcept
			{
				a_translate.y = a_translate.y * cosInclinationTheta - a_translate.x * sinInclinationTheta;
				a_translate.x = a_translate.y * sinInclinationTheta + a_translate.x * cosInclinationTheta;
			}
		};

		class Lunar
		{
			float angle{ 0.f };
			float radius{ 600.f };
			RE::NiPoint3 coordinate{};
			LunarRotationCalculator calc{};
		public:
			Lunar() = default;

			void Update(RE::Moon* a_moon, RE::Sky* a_sky) noexcept
			{
				// We get the number of hours passed since the beginning of the game.
				auto hoursPassed = 0.f;
				auto calendar = RE::Calendar::GetSingleton();
				if (calendar) hoursPassed = calendar->GetHoursPassed();
				else hoursPassed = a_sky->currentGameHour;

				// Getting the actual angle of the moon by the hour, clipped by degrees, in radians.
				angle = static_cast<float>(calc.CalculateRotationRad(hoursPassed));
				auto& root = a_moon->root;

				// Calculate coordinate
				root->local.translate.x = std::cosf(angle) * radius;
				root->local.translate.z = std::sinf(angle) * radius;
				// Let's move it to the south, since Boston is north of the equator.
				root->local.translate.y = -325.f;

				// Apply inclination tilted at approximately 5.14° relative to Earth's orbital plane.
				calc.ApplyInclination(root->local.translate);

				// The moon will be bigger at moonrise and moonset.
				root->local.scale = std::fmaxf(1.0f - std::fabs(root->local.translate.z) / radius, 0.5f);
#if 0
				// FOR TEST
				root->local.translate.x += 1200.f;
				root->local.translate.z += 1200.f;
#else
				// Place it a little lower so that you don't see how the moon turns at the end of the map.
				root->local.translate.z -= 120.f;
#endif
			}
		};

		static Lunar moon;
		using TUpdateThunk = void(RE::Moon* a_moon, RE::Sky* a_sky, float a_unk);
		static std::function<TUpdateThunk> Update_orig;

		static void HookUpdateDirection(RE::Moon* a_moon, RE::Sky* a_sky, float a_unk) noexcept
		{
			Update_orig(a_moon, a_sky, a_unk);
			moon.Update(a_moon, a_sky);
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
		const auto targetInit = REL::ID{ 114988, 2208804 };
		const auto targetUpdate = REL::ID{ 4410, 2208806 };

		// Fixed rotation and direction

		Moon::Update_orig = (Moon::TUpdateThunk*)RELEX::DetourJump(targetUpdate.address(),
			reinterpret_cast<uintptr_t>(&Moon::HookUpdateDirection));

		const auto target3 = REL::Relocation{ targetInit, REL::Offset{ 0x1E2, 0x1F7 } }.address();
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