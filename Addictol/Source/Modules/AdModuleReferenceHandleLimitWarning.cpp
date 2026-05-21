#include <Modules/AdModuleReferenceHandleLimitWarning.h>
#include <AdUtils.h>
#include <AdGameUtils.h>

#include <RE/C/ConsoleLog.h>

// the ratio at which we start warning in console log.
// this can be set to anything, i just set it to 80% for right now
#define WARNING_MIN_RATIO 0.80f

namespace Addictol
{
	// credit: Warnings::ReferenceHandleCap in Daytripper 4 by mith077 under MIT
	// https://www.nexusmods.com/fallout4/mods/91141?tab=files

	// data
	static inline constexpr uint32_t MAX_HANDLE_LIMIT = 0x100000;
	static const std::string WarningMessage_ReferenceHandleLimitExceeded = "Addictol::ReferenceHandleLimitWarning: "
		"Reference Handle Limit Exceeded! This can cause CTDs. Consider trimming your load order for your next save.";
	static const std::string WarningMessage_ReferenceHandleLimitAlmostExceeded = "Addictol::ReferenceHandleLimitWarning: "
		"TODO";

	// cached values
	static uint32_t lastCount = 0;
	static float lastRatio = 0.0f;

	// tested on AE, but should be fine on OG/NG since daytripper's addresses work on OG/NG
	static const auto ReferenceHandleArray_addresses = REL::VariantID(1103816, 2688744).address();
	static const auto ReferenceHandleArray = reinterpret_cast<uint64_t*>(ReferenceHandleArray_addresses);

	static REX::TOML::Bool<> bWarningsReferenceHandleLimit{ "Warnings"sv, "bReferenceHandleLimit"sv, true };

	ModuleReferenceHandleLimitWarning::ModuleReferenceHandleLimitWarning() :
		Module("Reference Handle Limit Warning", &bWarningsReferenceHandleLimit)
	{}

	// note: could be useful to have in AdGameUtils but compiler was being annoying
	std::string GetMessagingInterfaceString(F4SE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg)
			return "ERROR_NULL_MESSAGE";
		
		switch (a_msg->type) {
			case F4SE::MessagingInterface::kPostLoad:
				return "kPostLoad";
			case F4SE::MessagingInterface::kPostPostLoad:
				return "kPostPostLoad";
			case F4SE::MessagingInterface::kPreLoadGame:
				return "kPreLoadGame";
			case F4SE::MessagingInterface::kPostLoadGame:
				return "kPostLoadGame";
			case F4SE::MessagingInterface::kPreSaveGame:
				return "kPreSaveGame";
			case F4SE::MessagingInterface::kPostSaveGame:
				return "kPostSaveGame";
			case F4SE::MessagingInterface::kDeleteGame:
				return "kDeleteGame";
			case F4SE::MessagingInterface::kInputLoaded:
				return "kInputLoaded";
			case F4SE::MessagingInterface::kNewGame:
				return "kNewGame";
			case F4SE::MessagingInterface::kGameLoaded:
				return "kGameLoaded";
			case F4SE::MessagingInterface::kGameDataReady:
				return "kGameDataReady";
			default:
				return "ERROR_NO_TYPE";
		}

		return "ERROR_IDEK_HONESTLY";
	}

	uint32_t GetReferenceHandleCount() noexcept
	{
		uint32_t count = 0;
		for (uint32_t i = 0; i < MAX_HANDLE_LIMIT; i++) {
			if ((ReferenceHandleArray[i] >> 26) & 1) {
				count++;
			}
		}

		return count;
	}

	void CheckReferenceHandleLimit(std::string_view eventName, bool cacheResults = true) noexcept
	{
		uint32_t count = GetReferenceHandleCount();
		float ratio = static_cast<float>(count) / static_cast<float>(MAX_HANDLE_LIMIT);

		if (cacheResults) {
			// we can cache it so we dont have to iterate again unless we need to measure it again
			lastCount = count;
			lastRatio = ratio;
		}

		REX::INFO("Reference Handle Count ({}): {}. Ratio: {:.4}%"sv, eventName, count, ratio);

		// warn if needed
		if (ratio >= WARNING_MIN_RATIO) {
			REX::WARN(std::string_view(WarningMessage_ReferenceHandleLimitExceeded));

			auto* consoleLog = RE::ConsoleLog::GetSingleton();
			if (consoleLog) {
				consoleLog->AddString(WarningMessage_ReferenceHandleLimitExceeded.c_str());
			}
		}
	}

	bool ModuleReferenceHandleLimitWarning::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleReferenceHandleLimitWarning::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (!a_msg)
			return true;
		
		std::string eventName = GetMessagingInterfaceString(a_msg);
		CheckReferenceHandleLimit(eventName);
		
		return true;
	}

	bool ModuleReferenceHandleLimitWarning::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleReferenceHandleLimitWarning::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}