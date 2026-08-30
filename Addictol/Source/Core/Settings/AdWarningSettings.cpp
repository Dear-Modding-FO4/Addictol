#include <Core/Settings/AdSettings.h>

namespace Addictol
{
	using namespace std::literals;

	BoolSetting bWarningsImageSpaceAdapter{
		"Warnings"sv,
		"bImageSpaceAdapter"sv,
		SettingDisplayCategory::kDiagnostics,
		true,
		"Warns on bad IMAD definitions which will corrupt your memory and crash your game."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bWarningsDuplicateAddonNodeIndex{
		"Warnings"sv,
		"bDuplicateAddonNodeIndex"sv,
		SettingDisplayCategory::kDiagnostics,
		true,
		"Warns if you have two AddonNode forms with the same index in your load order, which will cause errors with visual effects."sv,
		SettingApplyTiming::kNextLaunch
	};

	BoolSetting bWarningsReferenceHandleLimit{
		"Warnings"sv,
		"bReferenceHandleLimit"sv,
		SettingDisplayCategory::kDiagnostics,
		true,
		"Warns if you are approaching the reference handle limit or exceed the reference handle limit. Terminate process if out of entries!"sv,
		SettingApplyTiming::kNextLaunch
	};
}
