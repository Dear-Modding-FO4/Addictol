#include <Modules/AdModulePipBoyCursorConstraints.h>
#include <AdUtils.h>

#include <RE/M/MenuCursor.h>
#include <RE/S/Setting.h>

#define AD_NOMESSAGE_PIPBOY_CURSOR_CONSTRAINTS 1

namespace Addictol
{
	static REX::TOML::Bool<> bFixesPipBoyCursorConstraints{ "Fixes"sv, "bPipBoyCursorConstraints"sv, true };

	ModulePipBoyCursorConstraints::ModulePipBoyCursorConstraints() :
		Module("PipBoy Cursor Constraints", &bFixesPipBoyCursorConstraints)
	{}

	bool ModulePipBoyCursorConstraints::DoQuery() const noexcept
	{
		return true;
	}

	bool ModulePipBoyCursorConstraints::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// Get PipBoy Resolution
		RE::Setting* uPipboyTargetHeight = nullptr;
		RE::Setting* uPipboyTargetWidth = nullptr;
		auto iniPrefs = RE::INIPrefSettingCollection::GetSingleton();
		if (iniPrefs)
		{
			uPipboyTargetHeight = iniPrefs->GetSetting("uPipboyTargetHeight:Display"sv); 	// Default: 700
			uPipboyTargetWidth = iniPrefs->GetSetting("uPipboyTargetWidth:Display"sv);		// Default: 876
		}

		// Fail if we couldn't get the Settings
		if (!uPipboyTargetHeight || !uPipboyTargetWidth)
		{
			REX::WARN("Could not find one of the uPipboyTargetHeight/Width settings!");
			return false;
		}

#if !AD_NOMESSAGE_PIPBOY_CURSOR_CONSTRAINTS
		// Log PipBoy Resolution
		REX::INFO("uPipboyTargetHeight: {}", uPipboyTargetHeight->GetUInt());
		REX::INFO("uPipboyTargetWidth: {}", uPipboyTargetWidth->GetUInt());
#endif

		// PipBoy Constraints
		REL::Relocation<std::uint32_t*> PipboyConstraintTLX{ RE::ID::MenuCursor::PipboyConstraintTLX }; 							// Default: 75
		REL::Relocation<std::uint32_t*> PipboyConstraintTLY{ RE::ID::MenuCursor::PipboyConstraintTLY };								// Default: 95
		REL::Relocation<std::uint32_t*> PipboyConstraintWidth{ RE::ID::MenuCursor::PipboyConstraintWidth };							// Default: 725
		REL::Relocation<std::uint32_t*> PipboyConstraintHeight{ RE::ID::MenuCursor::PipboyConstraintHeight };						// Default: 510

		// Power Armor PipBoy Constraints
		REL::Relocation<std::uint32_t*> PipboyConstraintTLX_PowerArmor{ RE::ID::MenuCursor::PipboyConstraintTLX_PowerArmor };		// Default: 160
		REL::Relocation<std::uint32_t*> PipboyConstraintTLY_PowerArmor{ RE::ID::MenuCursor::PipboyConstraintTLY_PowerArmor };		// Default: 160
		REL::Relocation<std::uint32_t*> PipboyConstraintWidth_PowerArmor{ RE::ID::MenuCursor::PipboyConstraintWidth_PowerArmor };	// Default: 555
		REL::Relocation<std::uint32_t*> PipboyConstraintHeight_PowerArmor{ RE::ID::MenuCursor::PipboyConstraintHeight_PowerArmor };	// Default: 380

#if !AD_NOMESSAGE_PIPBOY_CURSOR_CONSTRAINTS
		// Log before updating PipBoy Constraints
		REX::INFO("TLX Before Update: {}", *PipboyConstraintTLX);
		REX::INFO("TLY Before Update: {}", *PipboyConstraintTLY);
		REX::INFO("Width Before Update: {}", *PipboyConstraintWidth);
		REX::INFO("Height Before Update: {}", *PipboyConstraintHeight);

		// Log before updating Power Armor PipBoy Constraints
		REX::INFO("PA TLX Before Update: {}", *PipboyConstraintTLX_PowerArmor);
		REX::INFO("PA TLY Before Update: {}", *PipboyConstraintTLY_PowerArmor);
		REX::INFO("PA Width Before Update: {}", *PipboyConstraintWidth_PowerArmor);
		REX::INFO("PA Height Before Update: {}", *PipboyConstraintHeight_PowerArmor);
#endif

		// Update PipBoy Constraints
		*PipboyConstraintTLX 	= static_cast<std::uint32_t>(std::ceil(uPipboyTargetWidth->GetUInt() 	* 0.08561f));
		*PipboyConstraintTLY 	= static_cast<std::uint32_t>(std::ceil(uPipboyTargetHeight->GetUInt() 	* 0.13571f));
		*PipboyConstraintWidth 	= static_cast<std::uint32_t>(std::ceil(uPipboyTargetWidth->GetUInt() 	* 0.82762f));
		*PipboyConstraintHeight = static_cast<std::uint32_t>(std::ceil(uPipboyTargetHeight->GetUInt() 	* 0.72857f));

		// Update Power Armor PipBoy Constraints
		*PipboyConstraintTLX_PowerArmor 	= static_cast<std::uint32_t>(std::ceil(uPipboyTargetWidth->GetUInt() 	* 0.18264f));
		*PipboyConstraintTLY_PowerArmor 	= static_cast<std::uint32_t>(std::ceil(uPipboyTargetHeight->GetUInt() 	* 0.22857f));
		*PipboyConstraintWidth_PowerArmor 	= static_cast<std::uint32_t>(std::ceil(uPipboyTargetWidth->GetUInt() 	* 0.63356f));
		*PipboyConstraintHeight_PowerArmor 	= static_cast<std::uint32_t>(std::ceil(uPipboyTargetHeight->GetUInt() 	* 0.54285f));

#if !AD_NOMESSAGE_PIPBOY_CURSOR_CONSTRAINTS
		// Log after updating PipBoy Constraints
		REX::INFO("TLX After Update: {}", *PipboyConstraintTLX);
		REX::INFO("TLY After Update: {}", *PipboyConstraintTLY);
		REX::INFO("Width After Update: {}", *PipboyConstraintWidth);
		REX::INFO("Height After Update: {}", *PipboyConstraintHeight);

		// Log after updating Power Armor PipBoy Constraints
		REX::INFO("PA TLX After Update: {}", *PipboyConstraintTLX_PowerArmor);
		REX::INFO("PA TLY After Update: {}", *PipboyConstraintTLY_PowerArmor);
		REX::INFO("PA Width After Update: {}", *PipboyConstraintWidth_PowerArmor);
		REX::INFO("PA Height After Update: {}", *PipboyConstraintHeight_PowerArmor);
#endif

		return true;
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