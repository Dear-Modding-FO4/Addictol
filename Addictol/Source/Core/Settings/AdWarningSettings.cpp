#include <Core/Settings/AdSettings.h>

namespace Addictol
{
	using namespace std::literals;

	static dmui::localize::LocalizeString lsWarningsImageSpaceAdapter(
		"$Setting_WarningsImageSpaceAdapter",
		"Warns on bad IMAD definitions which will corrupt your memory and crash your game."
	);
	static dmui::localize::LocalizeString lsWarningsDuplicateAddonNodeIndex(
		"$Setting_WarningsDuplicateAddonNodeIndex",
		"Warns if you have two AddonNode forms with the same index in your load order, which will cause errors with visual effects."
	);
	static dmui::localize::LocalizeString lsWarningsReferenceHandleLimit(
		"$Setting_WarningsReferenceHandleLimit",
		"Warns if you are approaching the reference handle limit or exceed the reference handle limit. Terminate process if out of entries!"
	);
	static dmui::localize::LocalizeString lsWarningsSlowPlugin(
		"$Setting_WarningsSlowPlugin",
		"Warns when an F4SE plugin takes over a second to load or handle a game event, which delays the main menu."
	);

	BoolSetting bWarningsImageSpaceAdapter{
		"Warnings"sv,
		"bImageSpaceAdapter"sv,
		SettingDisplayCategory::kDiagnostics,
		true,
		std::addressof(lsWarningsImageSpaceAdapter),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bWarningsDuplicateAddonNodeIndex{
		"Warnings"sv,
		"bDuplicateAddonNodeIndex"sv,
		SettingDisplayCategory::kDiagnostics,
		true,
		std::addressof(lsWarningsDuplicateAddonNodeIndex),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bWarningsReferenceHandleLimit{
		"Warnings"sv,
		"bReferenceHandleLimit"sv,
		SettingDisplayCategory::kDiagnostics,
		true,
		std::addressof(lsWarningsReferenceHandleLimit),
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bWarningsSlowPlugin{
		"Warnings"sv,
		"bSlowPlugin"sv,
		SettingDisplayCategory::kDiagnostics,
		true,
		std::addressof(lsWarningsSlowPlugin),
		SettingApplyTiming::kNextLaunch
	};
}
