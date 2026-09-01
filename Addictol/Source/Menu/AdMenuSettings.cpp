#include <Menu/AdMenuSettings.h>

#include <Core/Settings/AdSettingsModel.h>
#include <DearModdingUI/IconGlyphs.h>
#include <Menu/AdMenu.h>

#include <REX/REX.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Addictol::Menu
{
	using namespace std::literals;

	namespace
	{
		SettingsDraftState g_draft;
		bool g_drawnThisFrame{ false };
		bool g_resetView{ false };

		[[nodiscard]] char32_t CategoryGlyph(
			SettingDisplayCategory a_category) noexcept
		{
			return DearModdingUI::ResolveIconGlyph(
				DearModdingUI::IconKind::kCategory,
				SettingDisplayCategoryName(a_category));
		}

		void EnsureDraft()
		{
			if (!g_draft.active)
			{
				g_draft = BeginSettingsDraft(
					SettingsRepository::GetSingleton().Snapshot());
			}
		}

		void DiscardDraft() noexcept
		{
			if (g_draft.active)
				LeaveSettingsDraft(g_draft);
		}

		void ApplyDraft()
		{
			const auto commit = PrepareSettingsDraftApply(g_draft);
			const auto result =
				SettingsRepository::GetSingleton().Apply(commit.values);
			if (result.success)
			{
				CompleteSettingsDraftApply(g_draft, commit);
				Menu::ReportStatus(
					DMUI_STATUS_SEVERITY_SUCCESS,
					"Settings saved.");
			}
			else
			{
				Menu::ReportStatus(
					DMUI_STATUS_SEVERITY_ERROR,
					result.error.c_str());
				REX::WARN(
					"Settings: AddictolCustom.toml could not be saved: {}"sv,
					result.error);
			}
		}

		template <class T>
		[[nodiscard]] std::optional<T> ConvertNumericBound(
			const std::optional<double>& a_bound) noexcept
		{
			if (!a_bound || !std::isfinite(*a_bound))
				return std::nullopt;
			if constexpr (std::is_same_v<T, double>)
			{
				return std::clamp(
					*a_bound,
					-static_cast<double>(
						(std::numeric_limits<float>::max)()),
					static_cast<double>(
						(std::numeric_limits<float>::max)()));
			}
			else if constexpr (std::is_same_v<T, int64_t>)
			{
				return static_cast<int64_t>(std::clamp(
					*a_bound,
					static_cast<double>(
						(std::numeric_limits<int32_t>::min)()),
					static_cast<double>(
						(std::numeric_limits<int32_t>::max)())));
			}
			else
			{
				return static_cast<uint64_t>(std::clamp(
					*a_bound,
					0.0,
					static_cast<double>(
						(std::numeric_limits<uint32_t>::max)())));
			}
		}

		template <class T>
		[[nodiscard]] std::optional<dmui::NumericSettingRange<T>>
			ConvertNumericRange(const SettingEntry& a_setting) noexcept
		{
			if (!a_setting.NumericRange())
				return std::nullopt;
			return dmui::NumericSettingRange<T>{
				.minimum = ConvertNumericBound<T>(
					a_setting.NumericRange()->minimum),
				.maximum = ConvertNumericBound<T>(
					a_setting.NumericRange()->maximum)
			};
		}

		template <class T>
		[[nodiscard]] dmui::NumericSettingControl<T> MakeNumericControl(
			const SettingEntry& a_setting)
		{
			auto control = dmui::NumericSettingControl<T>{
				.range = ConvertNumericRange<T>(a_setting),
				.dragSpeed = std::is_same_v<T, double> ? 0.01f : 1.0f
			};
			if constexpr (std::is_same_v<T, double>)
				control.format = "%.3f";
			else if constexpr (std::is_same_v<T, int64_t>)
				control.format = "%lld";
			else
				control.format = "%llu";
			return control;
		}

		[[nodiscard]] dmui::SettingControl MakeSettingControl(
			const SettingEntry& a_setting)
		{
			switch (SelectSettingControl(a_setting))
			{
			case SettingControlKind::kCheckbox:
				return dmui::CheckboxSettingControl{};
			case SettingControlKind::kCombo:
			{
				dmui::ChoiceSettingControl control;
				const auto options = SettingStringOptions(a_setting);
				control.options.reserve(options.size());
				for (const auto option : options)
				{
					control.options.push_back({
						std::string{ option },
						std::string{ option }
					});
				}
				return control;
			}
			case SettingControlKind::kTextInput:
				return dmui::TextSettingControl{ 512 };
			case SettingControlKind::kSlider:
			case SettingControlKind::kDrag:
			case SettingControlKind::kNumericInput:
				switch (a_setting.Type())
				{
				case SettingValueType::kFloat32:
					return MakeNumericControl<double>(a_setting);
				case SettingValueType::kInt32:
					return MakeNumericControl<int64_t>(a_setting);
				case SettingValueType::kUInt32:
					return MakeNumericControl<uint64_t>(a_setting);
				default:
					break;
				}
				break;
			}
			return dmui::UnsupportedSettingControl{
				static_cast<uint32_t>(a_setting.Type())
			};
		}

		template <dmui::SettingValueAlternative T>
		[[nodiscard]] dmui::SettingBinding BindDraftSetting(
			const SettingIdentity& a_identity,
			const T& a_fallback)
		{
			return dmui::BindSetting(
				[identity = a_identity, fallback = a_fallback]() -> T {
					const auto* entry =
						ResolveSettingDraftEntry(g_draft, identity);
					if (!entry || !std::holds_alternative<T>(entry->draft))
						return fallback;
					return std::get<T>(entry->draft);
				},
				[identity = a_identity, fallback = a_fallback](
					T a_value) -> T {
					auto* entry =
						ResolveSettingDraftEntry(g_draft, identity);
					if (!entry || !std::holds_alternative<T>(entry->draft))
						return fallback;
					entry->draft = NormalizeSettingDraftValue(
						*entry->setting,
						SettingValue{ std::move(a_value) });
					return std::get<T>(entry->draft);
				});
		}

		[[nodiscard]] dmui::SettingApplyTiming ConvertApplyTiming(
			SettingApplyTiming a_timing) noexcept
		{
			return a_timing == SettingApplyTiming::kImmediate ?
				dmui::SettingApplyTiming::kImmediate :
				dmui::SettingApplyTiming::kNextLaunch;
		}

		[[nodiscard]] dmui::SettingDescriptor MakeSettingDescriptor(
			const SettingEntry& a_setting)
		{
			const auto identity = MakeSettingIdentity(a_setting);
			const auto defaultValue = a_setting.DefaultValue();
			auto label = std::string{ a_setting.DisplayName() };
			label.append(" [");
			label.append(a_setting.Key());
			label.push_back(']');

			auto descriptor = dmui::SettingDescriptor{
				.id = identity.section + "." + identity.key,
				.label = std::move(label),
				.description = std::string{ a_setting.Description() },
				.control = MakeSettingControl(a_setting),
				.defaultValue = defaultValue,
				.binding = std::visit(
					[&]<class T>(const T& a_value) {
						return BindDraftSetting(identity, a_value);
					},
					defaultValue),
				.applyTiming = ConvertApplyTiming(a_setting.ApplyTiming()),
				.isDirty = [identity] {
					const auto* entry =
						ResolveSettingDraftEntry(g_draft, identity);
					return entry && entry->draft != entry->committed;
				},
				.isModified = [identity] {
					const auto* entry =
						ResolveSettingDraftEntry(g_draft, identity);
					return entry &&
						IsSettingModified(*entry->setting, entry->draft);
				}
			};
			return descriptor;
		}

		[[nodiscard]] std::vector<dmui::SettingGroup> MakeSettingGroups()
		{
			std::vector<dmui::SettingGroup> groups;
			groups.reserve(kSettingDisplayCategoryOrder.size());
			for (const auto category : kSettingDisplayCategoryOrder)
			{
				const auto name = SettingDisplayCategoryName(category);
				auto group = dmui::SettingGroup{
					.id = std::string{ name },
					.label = std::string{ name },
					.glyph = CategoryGlyph(category)
				};
				for (const auto& entry : g_draft.entries)
				{
					if (entry.setting->DisplayCategory() == category)
						group.settings.push_back(
							MakeSettingDescriptor(*entry.setting));
				}
				if (!group.settings.empty())
					groups.push_back(std::move(group));
			}
			return groups;
		}

		void PrepareSettingsView(dmui::SettingsPage& a_page)
		{
			if (g_resetView)
			{
				a_page.ResetView();
				g_resetView = false;
			}
			if (a_page.groups.empty())
				a_page.groups = MakeSettingGroups();
		}

		[[nodiscard]] dmui::SettingsPage MakeSettingsPage()
		{
			return {
				.actions = {
					.showReset = true,
					.reset = [] {
						ResetSettingsDraftToDefaults(g_draft);
					},
					.revert = [] {
						RevertSettingsDraft(g_draft);
					},
					.apply = [] {
						ApplyDraft();
					}
				},
				.actionTooltips = {
					.reset =
						"Reset loads every Addictol setting's shipped default "
						"into the draft. Use Apply to save them.",
					.revert =
						"Revert discards pending edits and restores saved settings.",
					.apply = [](size_t a_pending) {
						return "Apply saves " + std::to_string(a_pending) +
							" pending " +
							(a_pending == 1 ? "change" : "changes") +
							" to AddictolCustom.toml.";
					}
				},
				.filterOptions = {
					.showSearch = true,
					.showModifiedOnly = true,
					.searchHint = "Search settings..."
				},
				.notes = {
					{
						"Most settings take effect after the next game launch. "
						"Only settings labeled \"Applies now\" update immediately.",
						false
					},
					{
						"Menu appearance is owned by the gear panel's "
						"Interface Settings.",
						true
					}
				},
				.prepare = [] {
					g_drawnThisFrame = true;
					EnsureDraft();
				},
				.prepareView = &PrepareSettingsView
			};
		}
	}

	void BeginSettingsPageFrame() noexcept
	{
		g_drawnThisFrame = false;
	}

	void EndSettingsPageFrame(bool a_menuVisible) noexcept
	{
		if (!a_menuVisible)
		{
			CloseSettingsPage();
			return;
		}
		if (!g_drawnThisFrame)
			DiscardDraft();
	}

	void CloseSettingsPage() noexcept
	{
		DiscardDraft();
		g_resetView = true;
	}

	bool RegisterSettingsPage() noexcept
	{
		try
		{
			const auto page = Client().AddSettingsPage(
				"settings",
				"Settings",
				"Addictol",
				MakeSettingsPage(),
				"Configure Addictol fixes, performance, visuals, gameplay, "
				"and diagnostics.",
				100);
			if (!page)
			{
				REX::WARN(
					"Menu: page \"Settings\" rejected, result {}."sv,
					DMUI_ResultToString(Client().LastResult()));
			}
			return page.has_value();
		}
		catch (const std::bad_alloc&)
		{
			REX::ERROR(
				"Menu: page \"Settings\" could not allocate its descriptors."sv);
		}
		catch (...)
		{
			REX::ERROR(
				"Menu: page \"Settings\" descriptor construction failed."sv);
		}
		return false;
	}
}
