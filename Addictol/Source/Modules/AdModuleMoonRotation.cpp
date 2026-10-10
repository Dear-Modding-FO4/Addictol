#include <Modules/AdModuleMoonRotation.h>
#include <Core/AdUtils.h>
#include <xbyak/xbyak.h>

#undef MEM_RELEASE
#undef ERROR
#undef MAX_PATH

#include <RE/B/BSStringT.h>
#include <RE/N/NiNode.h>
#include <RE/B/BSTriShape.h>
#include <RE/S/Sun.h>
#include <RE/S/Sky.h>
#include <RE/M/Moon.h>
#include <RE/C/Calendar.h>
#include <RE/S/Setting.h>
#include <RE/T/TESClimate.h>
#include <RE/N/NiDirectionalLight.h>

namespace Addictol
{
	constexpr float NI_PI = static_cast<float>(3.1415926535897932);

	// Function to convert degrees to radians
	constexpr static double Deg2Rad(double a_degrees) noexcept
	{
		return a_degrees * (NI_PI / 180.0);
	}

	namespace Moon
	{
		class LunarRotationCalculator
		{
			// Degrees the moon rotates in exactly 1 hour (~50 min later every day)
			const double DEGREES_PER_HOUR = 14.5;

			// The Moon's orbit is tilted at approximately 5.14° relative to Earth's orbital plane (the ecliptic)
			const float DEGREES_INCLINATION = 5.14f;

			const float cosInclinationTheta{ std::cosf(static_cast<float>(Deg2Rad(DEGREES_INCLINATION))) };
			const float sinInclinationTheta{ std::sinf(static_cast<float>(Deg2Rad(DEGREES_INCLINATION))) };
		public:
			constexpr LunarRotationCalculator() = default;

			double CalculateRotationDeg(double a_hours, bool a_clampTo360 = true) const noexcept
			{
				// 1 day (It was visible, added to 90°)
				double totalDegrees = a_hours * DEGREES_PER_HOUR + 90.0f;

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
				root->local.scale = std::fmaxf(1.5f - std::fabs(root->local.translate.z) / radius, 1.25f);
#if 0
				// FOR TEST
				root->local.translate.x += 1200.f;
				root->local.translate.z += 1200.f;
#else
				// Place it a little lower so that you don't see how the moon turns at the end of the map.
				root->local.translate.x -= 120.f;
				root->local.translate.z -= 120.f;
#endif
				// Shadows follow the moon (see HookSunUpdate), without the offset applied below.
				coordinate = root->local.translate;
			}

			[[nodiscard]] const RE::NiPoint3& GetCoordinate() const noexcept
			{
				return coordinate;
			}
		};

		static Lunar moon;
		using TUpdateThunk = void(RE::Moon* a_moon, RE::Sky* a_sky, float a_unk);
		static std::function<TUpdateThunk> Update_orig;

		static void HookUpdateDirection(RE::Moon* a_moon, RE::Sky* a_sky, float a_unk) noexcept
		{
			Update_orig(a_moon, a_sky, a_unk);

			if (a_sky->mode.none(RE::Sky::Mode::kFull))
				return;

			moon.Update(a_moon, a_sky);
		}
	}

	namespace Sun
	{
		using TSunUpdateThunk = void(RE::Sun* a_sun, RE::Sky* a_sky, float a_unk);
		static std::function<TSunUpdateThunk> SunUpdate_orig;

		static auto fSunAlphaTransTime	= 0.5f;
		static auto fSunShadowMinAngle	= .0f;
		static auto fSunShadowScale		= .0f;

		// Same window as Sun::Update: 1.0 while the sun is up, ramping over fSunAlphaTransTime around the middle
		// of the climate's sunrise and sunset transitions.
		[[nodiscard]] static float GetSunAlpha(const RE::Sky * a_sky) noexcept
		{
			const auto * climate = a_sky->currentClimate;
			if (!climate)
				return 1.f;
			
			constexpr float kHour = 0.16666667f;
			const auto time = [&climate](std::size_t a_index) noexcept {
				return static_cast<float>(static_cast<std::uint8_t>(climate->data[a_index])) * kHour;
			};
			
			const float half		= fSunAlphaTransTime * 0.5f;
			const float sunrise		= (time(1) + time(0)) * 0.5f;
			const float sunset		= (time(3) + time(2)) * 0.5f;
			const float riseBegin	= sunrise - half;
			const float riseEnd		= sunrise + half;
			const float setBegin	= sunset - half;
			const float setEnd		= sunset + half;
			const float hour		= a_sky->currentGameHour;
			
			if (riseBegin > hour || hour > setEnd)
				return 0.f;
			if (riseBegin >= hour || hour >= riseEnd)
			{
				if (setBegin < hour && hour < setEnd)
					return 1.f - (hour - setBegin) / (setEnd - setBegin);
				return 1.f;
			}

			return (hour - riseBegin) / (riseEnd - riseBegin);
		}

		static void HookSunUpdate(RE::Sun* a_sun, RE::Sky* a_sky, float a_unk) noexcept
		{
			SunUpdate_orig(a_sun, a_sky, a_unk);

			// Interior and sky-dome-only modes use the cell's own directional light.
			if (a_sky->mode.none(RE::Sky::Mode::kFull))
				return;

			const float night = 1.f - GetSunAlpha(a_sky);
			if (night <= .0f)
				return;

			auto light = a_sun->light.get();
			auto cloudLight = a_sun->cloudLight.get();
			if (!light)
				return;

			RE::NiPoint3 moonDir = Moon::moon.GetCoordinate();
			moonDir.Normalize();

			const float elevation = std::fmaxf(std::fabsf(moonDir.z) + fSunShadowScale, fSunShadowMinAngle);

			moonDir.x = -moonDir.x;
			moonDir.y = -moonDir.y;
			moonDir.z = -elevation;
			moonDir.Normalize();

			auto& row = light->local.rotate.entry[0];
			
			RE::NiPoint3 direction
			{
				row.x + (moonDir.x - row.x) * night,
				row.y + (moonDir.y - row.y) * night,
				row.z + (moonDir.z - row.z) * night
			};
			direction.Normalize();

			if (direction == RE::NiPoint3::ZERO)
				direction = moonDir;
	
			row.x = direction.x;
			row.y = direction.y;
			row.z = direction.z;

			if (cloudLight)
			{
				auto& cloudRow = cloudLight->local.rotate.entry[0];
				cloudRow.x = direction.x;
				cloudRow.y = direction.y;
				cloudRow.z = direction.z;
			}
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
		if (!a_msg)
		{
			const auto targetInit = REL::ID{ 114988, 2208804 };
			const auto targetUpdate = REL::ID{ 4410, 2208806 };

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

			// Fixed shadows
			// Sun::Update is virtual slot 2 of the Sun vtable on every runtime.
			const REL::Relocation sunVTable{ RE::Sun::VTABLE[0] };
			Sun::SunUpdate_orig = (Sun::TSunUpdateThunk*)RELEX::DetourVTable(sunVTable.address(),
				reinterpret_cast<uintptr_t>(&Sun::HookSunUpdate), 2);

			return true;
		}
		else if (a_msg->type == F4SE::MessagingInterface::kGameLoaded)
		{
			auto GetGameSettingFloat = [](std::string_view a_name, float a_default) noexcept
				{
					auto settings = RE::GameSettingCollection::GetSingleton();
					auto setting = settings ? settings->GetSetting(a_name) : nullptr;
					return setting ? setting->GetFloat() : a_default;
				};

			Sun::fSunAlphaTransTime	= GetGameSettingFloat("fSunAlphaTransTime"sv, .5f);
			Sun::fSunShadowMinAngle	= static_cast<float>(Deg2Rad(GetGameSettingFloat("fSunShadowMinAngle"sv, 30.f)));
			Sun::fSunShadowScale	= static_cast<float>(Deg2Rad(GetGameSettingFloat("fSunShadowScale"sv, .0f)));

			return true;
		}

		return false;
	}
}