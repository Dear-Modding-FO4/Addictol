#include <Menu/AdMenuChangelog.h>

#include <Menu/AdMenu.h>

#include <REX/W32/KERNEL32.h>
#include <resource_version2.h>

#include <DearModdingUI/UI.h>

#include <Windows.h>

#undef ERROR

namespace Addictol::Menu
{
	namespace
	{
		[[nodiscard]] ChangelogHistory LoadChangelog()
		{
			ChangelogHistory result;
			const auto module = REX::W32::GetCurrentModule();
			if (!module)
			{
				result.error = "current module handle is unavailable";
				return result;
			}
			const auto nativeModule = reinterpret_cast<HMODULE>(module);

			const auto resource = ::FindResourceW(
				nativeModule,
				MAKEINTRESOURCEW(IDR_CHANGELOG),
				MAKEINTRESOURCEW(10));
			if (!resource)
			{
				result.error = "RCDATA resource was not found";
				return result;
			}

			const auto size = ::SizeofResource(nativeModule, resource);
			if (size == 0)
			{
				result.error = "RCDATA resource is empty";
				return result;
			}
			const auto loaded = ::LoadResource(nativeModule, resource);
			if (!loaded)
			{
				result.error = "RCDATA resource could not be loaded";
				return result;
			}
			const auto* data = static_cast<const char*>(::LockResource(loaded));
			if (!data)
			{
				result.error = "RCDATA resource could not be read";
				return result;
			}
			return ParseChangelogHistory({ data, size });
		}

		[[nodiscard]] const ChangelogHistory& Changelog() noexcept
		{
			static const auto history = [] {
				auto result = LoadChangelog();
				if (!result.error.empty())
				{
					REX::ERROR("Menu: changelog unavailable: {}"sv, result.error);
				}
				return result;
			}();
			return history;
		}
	}

	void DrawChangelogPage([[maybe_unused]] void* a_userData) noexcept
	{
		ReportPresentationResult(Client().DrawSectionHeader(
			"Changelog",
			DearModdingUI::FindPhosphorSlugGlyphOrZero("notebook")));

		dmui::FontGuard font{ Client(), DMUI_FONT_ROLE_BODY };
		if (!font.Pushed())
		{
			ReportPresentationResult(false);
			return;
		}

		const auto& history = Changelog();
		if (!history.error.empty())
		{
			dmui::ui::TextWrapped(
				"Changelog unavailable. See Addictol.log for details.");
			ReportPresentationResult(font.End());
			return;
		}

		for (size_t index = 0; index < history.releases.size(); ++index)
		{
			const auto& release = history.releases[index];
			dmui::ui::PushID(release.version.c_str());
			const auto flags = index == 0 ?
				dmui::ui::TreeNodeFlags::kDefaultOpen :
				dmui::ui::TreeNodeFlags{};
			if (dmui::ui::CollapsingHeader(release.version.c_str(), flags))
			{
				dmui::ui::PushTextWrapPos(0.0f);
				for (const auto& item : release.items)
				{
					ReportPresentationResult(
						Client().DrawBulletText(item.c_str()));
				}
				dmui::ui::PopTextWrapPos();
			}
			dmui::ui::PopID();
		}

		ReportPresentationResult(font.End());
	}
}
