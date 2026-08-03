#include <Modules/AdModuleAIProcess3DUpdateFlag.h>
#include <AdUtils.h>

#include <RE/A/AIProcess.h>
#include <RE/M/MiddleHighProcessData.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesAIProcess3DUpdateFlag{ "Fixes"sv, "bAIProcess3DUpdateFlag"sv, true };

	namespace Detail
	{
		// Fixes an error where there is no check for nullptr when AI for a character is disabled.

		static void AIProcess__Set3DUpdateFlag(RE::AIProcess* a_this, uint16_t a_update3DModel) noexcept
		{
			if (!a_this || !a_this->middleHigh)
				return;

			a_this->middleHigh->update3DModel |= a_update3DModel;
		}

		static void AIProcess__Clear3DUpdateFlag(RE::AIProcess* a_this, uint16_t a_update3DModel) noexcept
		{
			if (!a_this || !a_this->middleHigh)
				return;

			a_this->middleHigh->update3DModel &= ~a_update3DModel;
		}

		static void AIProcess__ClearAll3DUpdateFlags(RE::AIProcess* a_this) noexcept
		{
			if (!a_this || !a_this->middleHigh)
				return;

			a_this->middleHigh->update3DModel = 0;
		}

		static bool AIProcess__Get3DUpdateFlag(RE::AIProcess* a_this, uint16_t a_update3DModel) noexcept
		{
			if (!a_this || !a_this->middleHigh)
				return false;

			return (a_this->middleHigh->update3DModel & a_update3DModel) != 0;
		}

		static uint16_t AIProcess__GetAll3DUpdateFlags(RE::AIProcess* a_this) noexcept
		{
			if (!a_this || !a_this->middleHigh)
				return 0;

			return a_this->middleHigh->update3DModel;
		}
	}

	ModuleAIProcess3DUpdateFlag::ModuleAIProcess3DUpdateFlag() :
		Module("AIProcess 3DUpdateFlag", &bFixesAIProcess3DUpdateFlag)
	{}

	bool ModuleAIProcess3DUpdateFlag::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleAIProcess3DUpdateFlag::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg)
			return false;

		const auto targetSet		= REL::ID{ 236542,	2232389 }.address();
		const auto targetClear		= REL::ID{ 1113891,	2232390 }.address();
		const auto targetClearAll	= REL::ID{ 409001,	2232391 }.address();
		const auto targetGet		= REL::ID{ 1286688,	2232392 }.address();
		const auto targetGetAll		= REL::ID{ 582098,	2232393 }.address();
		const auto checkCode		= std::initializer_list<uint8_t>{ 0x48, 0x8B, 0x41, 0x08 };

		if (!RELEX::Validate(targetSet,			checkCode) ||
			!RELEX::Validate(targetClear,		checkCode) ||
			!RELEX::Validate(targetClearAll,	checkCode) ||
			!RELEX::Validate(targetGet,			checkCode) ||
			!RELEX::Validate(targetGetAll,		checkCode))
			return false;

		return 
			(RELEX::DetourJump(targetSet,		reinterpret_cast<uintptr_t>(&Detail::AIProcess__Set3DUpdateFlag)) != 0) &&
			(RELEX::DetourJump(targetClear,		reinterpret_cast<uintptr_t>(&Detail::AIProcess__Clear3DUpdateFlag)) != 0) &&
			(RELEX::DetourJump(targetClearAll,	reinterpret_cast<uintptr_t>(&Detail::AIProcess__ClearAll3DUpdateFlags)) != 0) &&
			(RELEX::DetourJump(targetGet,		reinterpret_cast<uintptr_t>(&Detail::AIProcess__Get3DUpdateFlag)) != 0) &&
			(RELEX::DetourJump(targetGetAll,	reinterpret_cast<uintptr_t>(&Detail::AIProcess__GetAll3DUpdateFlags)) != 0);
	}

	bool ModuleAIProcess3DUpdateFlag::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleAIProcess3DUpdateFlag::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}