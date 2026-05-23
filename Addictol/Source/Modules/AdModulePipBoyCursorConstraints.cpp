#include <Modules/AdModulePipBoyCursorConstraints.h>
#include <AdUtils.h>

#include <RE/S/Setting.h>

#define AD_NOMESSAGE_PIPBOY_CURSOR_CONSTRAINTS 1

namespace Addictol
{
	static REX::TOML::Bool<> bFixesPipBoyCursorConstraints{ "Fixes"sv, "bPipBoyCursorConstraints"sv, true };

	namespace pipBoyCursorConstraintsDetail
	{
		// Default PipBoy Resolution
		constexpr std::uint32_t defaultPipboyTargetWidth = 876;
		constexpr std::uint32_t defaultPipboyTargetHeight = 700;

		// Default PipBoy Constraints
		constexpr std::uint32_t defaultPipboyConstraintTLX = 75;
		constexpr std::uint32_t defaultPipboyConstraintTLY = 95;
		constexpr std::uint32_t defaultPipboyConstraintWidth = 725;
		constexpr std::uint32_t defaultPipboyConstraintHeight = 510;

		// Default Power Armor PipBoy Constraints
		constexpr std::uint32_t defaultPipboyConstraintTLX_PowerArmor = 160;
		constexpr std::uint32_t defaultPipboyConstraintTLY_PowerArmor = 160;
		constexpr std::uint32_t defaultPipboyConstraintWidth_PowerArmor = 555;
		constexpr std::uint32_t defaultPipboyConstraintHeight_PowerArmor = 380;

		// PipBoy Resolution Settings
		RE::Setting* uPipboyTargetWidth = nullptr;
		RE::Setting* uPipboyTargetHeight = nullptr;

		// PipBoy Constraints
		static REL::Relocation<std::uint32_t*> PipboyConstraintTLX{ RE::ID::MenuCursor::PipboyConstraintTLX };
		static REL::Relocation<std::uint32_t*> PipboyConstraintTLY{ RE::ID::MenuCursor::PipboyConstraintTLY };
		static REL::Relocation<std::uint32_t*> PipboyConstraintWidth{ RE::ID::MenuCursor::PipboyConstraintWidth };
		static REL::Relocation<std::uint32_t*> PipboyConstraintHeight{ RE::ID::MenuCursor::PipboyConstraintHeight };

		// Power Armor PipBoy Constraints
		static REL::Relocation<std::uint32_t*> PipboyConstraintTLX_PowerArmor{ RE::ID::MenuCursor::PipboyConstraintTLX_PowerArmor };
		static REL::Relocation<std::uint32_t*> PipboyConstraintTLY_PowerArmor{ RE::ID::MenuCursor::PipboyConstraintTLY_PowerArmor };
		static REL::Relocation<std::uint32_t*> PipboyConstraintWidth_PowerArmor{ RE::ID::MenuCursor::PipboyConstraintWidth_PowerArmor };
		static REL::Relocation<std::uint32_t*> PipboyConstraintHeight_PowerArmor{ RE::ID::MenuCursor::PipboyConstraintHeight_PowerArmor };

		inline void LogPipBoyConstraints(std::string a_title, std::uint32_t a_tlx, std::uint32_t a_tly, std::uint32_t a_width, std::uint32_t a_height) noexcept
		{
#if !AD_NOMESSAGE_PIPBOY_CURSOR_CONSTRAINTS
			REX::INFO("{}", a_title);
			REX::INFO("TLX: {}", a_tlx);
			REX::INFO("TLY: {}", a_tly);
			REX::INFO("Width: {}", a_width);
			REX::INFO("Height: {}", a_height);
#endif
		}

		inline bool GetPipBoyResolution() noexcept
		{
			// Get PipBoy Resolution
			auto iniPrefs = RE::INIPrefSettingCollection::GetSingleton();
			if (iniPrefs)
			{
				uPipboyTargetWidth = iniPrefs->GetSetting("uPipboyTargetWidth:Display"sv);
				uPipboyTargetHeight = iniPrefs->GetSetting("uPipboyTargetHeight:Display"sv);
			}

			// Fail if we couldn't get the Settings
			if (!uPipboyTargetWidth || !uPipboyTargetHeight)
			{
				REX::WARN("Could not get one of the uPipboyTargetWidth/Height settings!");
				return false;
			}

#if !AD_NOMESSAGE_PIPBOY_CURSOR_CONSTRAINTS
			// Log PipBoy Resolution
			REX::INFO("PipBoy Resolution: {}x{}", uPipboyTargetWidth->GetUInt(), uPipboyTargetHeight->GetUInt());
#endif

			return true;
		}

		inline bool UpdatePipBoyCursorConstraints() noexcept
		{
			// Get PipBoy Resolution
			if (!GetPipBoyResolution())
				return false;

			// Resolution Factors
			const float widthFactor = (float)uPipboyTargetWidth->GetUInt() / defaultPipboyTargetWidth;
			const float heightFactor = (float)uPipboyTargetHeight->GetUInt() / defaultPipboyTargetHeight;

			// Log before updating PipBoy Constraints
			LogPipBoyConstraints("PipBoy Constraints Pre-Update:", *PipboyConstraintTLX, *PipboyConstraintTLY, *PipboyConstraintWidth, *PipboyConstraintHeight);
			LogPipBoyConstraints("PA PipBoy Constraints Pre-Update:", *PipboyConstraintTLX_PowerArmor, *PipboyConstraintTLY_PowerArmor, *PipboyConstraintWidth_PowerArmor, *PipboyConstraintHeight_PowerArmor);

			// Update PipBoy Constraints
			*PipboyConstraintTLX 	= static_cast<std::uint32_t>(std::ceil(defaultPipboyConstraintTLX * widthFactor));
			*PipboyConstraintTLY 	= static_cast<std::uint32_t>(std::ceil(defaultPipboyConstraintTLY * heightFactor));
			*PipboyConstraintWidth 	= static_cast<std::uint32_t>(std::ceil(defaultPipboyConstraintWidth * widthFactor));
			*PipboyConstraintHeight = static_cast<std::uint32_t>(std::ceil(defaultPipboyConstraintHeight * heightFactor));

			// Update Power Armor PipBoy Constraints
			*PipboyConstraintTLX_PowerArmor 	= static_cast<std::uint32_t>(std::ceil(defaultPipboyConstraintTLX_PowerArmor * widthFactor));
			*PipboyConstraintTLY_PowerArmor 	= static_cast<std::uint32_t>(std::ceil(defaultPipboyConstraintTLY_PowerArmor * heightFactor));
			*PipboyConstraintWidth_PowerArmor 	= static_cast<std::uint32_t>(std::ceil(defaultPipboyConstraintWidth_PowerArmor * widthFactor));
			*PipboyConstraintHeight_PowerArmor 	= static_cast<std::uint32_t>(std::ceil(defaultPipboyConstraintHeight_PowerArmor * heightFactor));

			// Log after updating PipBoy Constraints
			LogPipBoyConstraints("PipBoy Constraints Post-Update:", *PipboyConstraintTLX, *PipboyConstraintTLY, *PipboyConstraintWidth, *PipboyConstraintHeight);
			LogPipBoyConstraints("PA PipBoy Constraints Post-Update:", *PipboyConstraintTLX_PowerArmor, *PipboyConstraintTLY_PowerArmor, *PipboyConstraintWidth_PowerArmor, *PipboyConstraintHeight_PowerArmor);

			return true;
		}
	}

	ModulePipBoyCursorConstraints::ModulePipBoyCursorConstraints() :
		Module("PipBoy Cursor Constraints", &bFixesPipBoyCursorConstraints)
	{}

	bool ModulePipBoyCursorConstraints::DoQuery() const noexcept
	{
		return true;
	}

	bool ModulePipBoyCursorConstraints::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return pipBoyCursorConstraintsDetail::UpdatePipBoyCursorConstraints();
	}

	bool ModulePipBoyCursorConstraints::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModulePipBoyCursorConstraints::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}