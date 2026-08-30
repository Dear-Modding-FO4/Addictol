#include <DearModdingUI/Shell.h>
#include <DearModdingUI/BackgroundBlur.h>
#include <DearModdingUI/Host.h>
#include <DearModdingUI/HostSettings.h>
#include <DearModdingUI/HostSettingsView.h>
#include <DearModdingUI/IconGlyphs.h>
#include <DearModdingUI/Theme.h>
#include <DearModdingUI/VisualDecisions.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <map>
#include <numbers>
#include <string>
#include <string_view>

namespace Addictol::DearModdingUI
{
	namespace
	{
		struct ShellState : ClientSelectionState
		{
			std::map<std::string, bool> categoryExpansion;
		};

		[[nodiscard]] ShellState& State() noexcept
		{
			static ShellState state;
			return state;
		}

		[[nodiscard]] std::string Lower(std::string_view a_value)
		{
			std::string result{ a_value };
			std::ranges::transform(result, result.begin(), [](unsigned char a_character) {
				return static_cast<char>(std::tolower(a_character));
			});
			return result;
		}

		[[nodiscard]] bool Matches(
			const NavigationPage& a_page,
			std::string_view a_search)
		{
			if (a_search.empty())
				return true;
			const auto search = Lower(a_search);
			return Lower(a_page.displayName).contains(search) ||
				Lower(a_page.id).contains(search) ||
				Lower(a_page.category).contains(search) ||
				Lower(a_page.summary).contains(search);
		}

		[[nodiscard]] bool HasIconGlyph(char32_t a_glyph) noexcept
		{
			if (!a_glyph)
				return false;
			auto* font = ImGui::GetFont();
			return font &&
				font->IsGlyphInFont(static_cast<ImWchar>(a_glyph));
		}

		[[nodiscard]] ImU32 IconColor(ImU32 a_textColor) noexcept
		{
			auto tint = Theme::IconTint();
			tint.w *= ImGui::ColorConvertU32ToFloat4(a_textColor).w;
			return ImGui::ColorConvertFloat4ToU32(tint);
		}

		void DrawIcon(
			ImDrawList* a_drawList,
			char32_t a_glyph,
			const ImVec2& a_position,
			float a_size,
			ImU32 a_color,
			const ImVec4* a_clip) noexcept
		{
			auto* font = ImGui::GetFont();
			if (!font || !HasIconGlyph(a_glyph) || a_size <= 0.0f)
				return;
			font->RenderChar(
				a_drawList,
				a_size,
				a_position,
				a_color,
				static_cast<ImWchar>(a_glyph),
				a_clip);
		}

		void DrawCenteredIcon(
			ImDrawList* a_drawList,
			char32_t a_glyph,
			const ImRect& a_bounds,
			float a_size,
			ImU32 a_color,
			const ImVec4* a_clip) noexcept
		{
			auto* font = ImGui::GetFont();
			if (!font || !HasIconGlyph(a_glyph) || a_size <= 0.0f)
				return;

			const auto center = a_bounds.GetCenter();
			ImVec2 position{
				center.x - a_size * 0.5f,
				center.y - a_size * 0.5f
			};
			if (auto* baked = font->GetFontBaked(a_size);
				baked && baked->Size > 0.0f)
			{
				if (const auto* glyph = baked->FindGlyphNoFallback(
						static_cast<ImWchar>(a_glyph)))
				{
					const auto origin = ResolveCenteredGlyphOrigin(
						center.x,
						center.y,
						glyph->X0,
						glyph->Y0,
						glyph->X1,
						glyph->Y1,
						a_size / baked->Size);
					position = { origin.x, origin.y };
				}
			}
			DrawIcon(
				a_drawList,
				a_glyph,
				position,
				a_size,
				a_color,
				a_clip);
		}

		[[nodiscard]] float CurrentFontOpticalTextOffsetY(
			float a_rowHeight,
			float a_fontSize) noexcept
		{
			auto* font = ImGui::GetFont();
			if (!font || a_fontSize <= 0.0f)
				return CenterOffsetY(a_rowHeight, a_fontSize);
			if (auto* baked = font->GetFontBaked(a_fontSize);
				baked && baked->Size > 0.0f)
			{
				if (const auto* reference =
						baked->FindGlyphNoFallback(
							static_cast<ImWchar>('H')))
				{
					return OpticalTextOffsetY(
						a_rowHeight,
						a_fontSize,
						reference->Y0,
						reference->Y1,
						a_fontSize / baked->Size);
				}
			}
			return CenterOffsetY(a_rowHeight, a_fontSize);
		}

		void DrawIconText(
			const ImVec2& a_position,
			float a_height,
			char32_t a_glyph,
			const char* a_text,
			ImU32 a_color,
			const ImVec4* a_clip = nullptr) noexcept
		{
			const auto textSize = ImGui::CalcTextSize(a_text);
			const auto layout = DecideInlineIconLayout(
				HasIconGlyph(a_glyph),
				textSize.x,
				textSize.y,
				ImGui::GetFontSize(),
				ImGui::GetStyle().ItemSpacing.x);
			const auto contentY =
				a_position.y + ((std::max)(a_height, layout.contentHeight) -
					layout.contentHeight) * 0.5f;
			if (layout.drawIcon)
			{
				DrawCenteredIcon(
					ImGui::GetWindowDrawList(),
					a_glyph,
					ImRect{
						{ a_position.x, contentY },
						{
							a_position.x + layout.iconSize,
							contentY + layout.contentHeight
						}
					},
					layout.iconSize,
					IconColor(a_color),
					a_clip);
			}
			ImGui::GetWindowDrawList()->AddText(
				ImGui::GetFont(),
				ImGui::GetFontSize(),
				{
					a_position.x + layout.textOffset,
					contentY + (layout.contentHeight - textSize.y) * 0.5f
				},
				a_color,
				a_text,
				nullptr,
				0.0f,
				a_clip);
		}

		[[nodiscard]] float GetPillRounding(
			const ImVec2& a_min,
			const ImVec2& a_max) noexcept
		{
			return ImMin(a_max.x - a_min.x, a_max.y - a_min.y) * 0.5f;
		}

		[[nodiscard]] bool DrawRoundedButtonHighlight(
			const ImVec2& a_min,
			const ImVec2& a_max,
			bool a_hovered,
			bool a_active,
			ImDrawList* a_drawList) noexcept
		{
			if (!a_hovered && !a_active)
				return false;
			const auto rounding = ImMin(
				ImMax(ImGui::GetStyle().FrameRounding, 0.0f),
				GetPillRounding(a_min, a_max));
			a_drawList->AddRectFilled(
				a_min,
				a_max,
				ImGui::GetColorU32(
					a_active ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered),
				rounding);
			return true;
		}

		inline constexpr float kTitleBarButtonPadding{ 2.0f };
		inline constexpr float kCloseCrossDiagonalScale{
			0.5f / std::numbers::sqrt2_v<float>
		};
		inline constexpr float kCloseCrossInset{ 1.0f };
		inline constexpr ImVec4 kTransparentButtonChrome{ 0, 0, 0, 0 };
		[[nodiscard]] ImRect TitleBarButtonRect(
			const ImVec2& a_origin,
			float a_fontSize) noexcept
		{
			const auto full = a_fontSize + kTitleBarButtonPadding * 2.0f;
			return { a_origin, { a_origin.x + full, a_origin.y + full } };
		}

		[[nodiscard]] ImVec2 RightTitleBarButtonOrigin(
			ImGuiWindow* a_window,
			float a_fontSize,
			float a_offset = 0.0f) noexcept
		{
			const auto& style = ImGui::GetStyle();
			return {
				RightTitleBarButtonOriginX(
					a_window->Rect().Max.x,
					a_window->WindowBorderSize,
					style.FramePadding.x,
					a_fontSize,
					a_offset,
					kTitleBarButtonPadding),
				a_window->Rect().Min.y +
					style.FramePadding.y -
					kTitleBarButtonPadding
			};
		}

		[[nodiscard]] bool IsTitleBarButtonHovered(
			ImGuiWindow* a_window,
			const ImRect& a_bounds) noexcept
		{
			auto& context = *ImGui::GetCurrentContext();
			return context.HoveredWindow == a_window &&
				ImGui::IsMouseHoveringRect(
					a_bounds.Min, a_bounds.Max, false);
		}

		class NativeTitleBarButtonHighlightGuard
		{
		public:
			NativeTitleBarButtonHighlightGuard() noexcept
			{
				ImGui::PushStyleColor(
					ImGuiCol_ButtonHovered, kTransparentButtonChrome);
				ImGui::PushStyleColor(
					ImGuiCol_ButtonActive, kTransparentButtonChrome);
			}

			~NativeTitleBarButtonHighlightGuard() noexcept
			{
				ImGui::PopStyleColor(2);
			}

			NativeTitleBarButtonHighlightGuard(
				const NativeTitleBarButtonHighlightGuard&) = delete;
			NativeTitleBarButtonHighlightGuard& operator=(
				const NativeTitleBarButtonHighlightGuard&) = delete;
		};

		void DrawRoundedCloseHighlight(ImGuiWindow* a_window) noexcept
		{
			if (!a_window ||
				(a_window->Flags & ImGuiWindowFlags_NoTitleBar))
				return;

			const auto size = ImGui::GetFontSize();
			const auto position = RightTitleBarButtonOrigin(a_window, size);
			const auto bounds = TitleBarButtonRect(position, size);
			const auto hovered = IsTitleBarButtonHovered(a_window, bounds);
			const auto held = hovered &&
				ImGui::IsMouseDown(ImGuiMouseButton_Left);

			a_window->DrawList->PushClipRect(
				a_window->Rect().Min, a_window->Rect().Max);
			if (DrawRoundedButtonHighlight(
					bounds.Min,
					bounds.Max,
					hovered,
					held,
					a_window->DrawList))
			{
				const auto center = bounds.GetCenter();
				const auto diagonal =
					size * kCloseCrossDiagonalScale - kCloseCrossInset;
				const auto color = ImGui::GetColorU32(ImGuiCol_Text);
				a_window->DrawList->AddLine(
					{ center.x - diagonal, center.y - diagonal },
					{ center.x + diagonal, center.y + diagonal },
					color);
				a_window->DrawList->AddLine(
					{ center.x + diagonal, center.y - diagonal },
					{ center.x - diagonal, center.y + diagonal },
					color);
			}
			a_window->DrawList->PopClipRect();
		}

		[[nodiscard]] bool BeginWithRoundedTitleBarButtons(
			const char* a_name,
			bool* a_open,
			ImGuiWindowFlags a_flags) noexcept
		{
			bool visible = false;
			{
				const NativeTitleBarButtonHighlightGuard guard;
				visible = ImGui::Begin(a_name, a_open, a_flags);
			}
			auto* window = ImGui::GetCurrentWindowRead();
			DrawRoundedCloseHighlight(window);
			return visible;
		}

		[[nodiscard]] bool DrawCompactChromeButton(
			const char* a_id,
			const ImVec2& a_origin,
			const ImVec2& a_size,
			char32_t a_glyph,
			const char* a_text,
			const char* a_tooltip,
			ImU32 a_color,
			bool a_active = false,
			float a_glyphSize = 0.0f) noexcept
		{
			auto* window = ImGui::GetCurrentWindow();
			if (!window)
				return false;

			const auto restore = ImGui::GetCursorScreenPos();
			ImGui::SetCursorScreenPos(a_origin);
			const auto pressed = ImGui::InvisibleButton(a_id, a_size);
			const auto hovered = ImGui::IsItemHovered(
				ImGuiHoveredFlags_AllowWhenDisabled);
			const auto held = ImGui::IsItemActive();
			const ImRect bounds{
				a_origin, { a_origin.x + a_size.x, a_origin.y + a_size.y }
			};

			(void)DrawRoundedButtonHighlight(
				bounds.Min,
				bounds.Max,
				hovered || a_active,
				held,
				window->DrawList);
			if (a_glyph)
			{
				const auto glyphSize = a_glyphSize > 0.0f ?
					a_glyphSize :
					ImGui::GetFontSize();
				DrawCenteredIcon(
					window->DrawList,
					a_glyph,
					bounds,
					glyphSize,
					a_color,
					nullptr);
			}
			else if (a_text)
			{
				const auto textSize = ImGui::CalcTextSize(a_text);
				const ImVec2 textOrigin{
					bounds.Min.x,
					bounds.Min.y + CurrentFontOpticalTextOffsetY(
						a_size.y,
						ImGui::GetFontSize())
				};
				ImGui::PushStyleColor(ImGuiCol_Text, a_color);
				ImGui::RenderTextClipped(
					textOrigin,
					bounds.Max,
					a_text,
					nullptr,
					&textSize,
					{ 0.5f, 0.0f },
					&bounds);
				ImGui::PopStyleColor();
			}
			if (hovered && a_tooltip)
				ImGui::SetTooltip("%s", a_tooltip);
			ImGui::SetCursorScreenPos(restore);
			// A restored cursor must be followed by an item or ImGui reports an unbounded SetCursorPos.
			ImGui::Dummy({ 0.0f, 0.0f });
			return pressed;
		}

		struct HostSettingsTitleButton
		{
			SettingsAction action;
			const char* id;
			const char* label;
			const char* tooltip;
			float width;
		};

		[[nodiscard]] bool DrawHeader(
			const NavigationModel& a_model,
			const ShellState& a_state,
			bool a_drawClose) noexcept
		{
			const auto* client = a_model.FindClient(a_state.activeClient);
			const auto breadcrumb = BuildHostBreadcrumb(
				"Evil Modding",
				HostSettings::IsPanelOpen() ?
					std::string_view{ "Interface Settings" } :
					(client ?
							std::string_view{ client->displayName } :
							std::string_view{}));
			const auto textScale = Theme::kHeaderFallbackTextScale;
			const auto start = ImGui::GetCursorScreenPos();
			const auto contentMaxX =
				start.x + ImGui::GetContentRegionAvail().x;
			const auto iconSize = HostChromeIconSize(ImGui::GetFontSize());
			const auto buttonExtent = HostChromeButtonExtent(
				ImGui::GetFontSize(), kTitleBarButtonPadding);
			const auto hasCloseGlyph = HasIconGlyph(PhosphorGlyph::kX);
			constexpr const char* closeLabel{ "Close" };
			const auto buttonWidth = ActionButtonWidth(
				hasCloseGlyph,
				ImGui::CalcTextSize(closeLabel).x,
				buttonExtent,
				ImGui::GetStyle().FramePadding.x);
			const auto buttonLayout = ResolveTrailingControlLayout(
				start.x,
				contentMaxX,
				a_drawClose ? buttonWidth : 0.0f,
				a_drawClose ? ImGui::GetStyle().ItemSpacing.x : 0.0f);
			const auto titleMaxX = a_drawClose ?
				buttonLayout.adjacentMaxX :
				contentMaxX;
			const auto titleMinX = start.x + BulletRunContentInset(
				ImGui::GetStyle().FramePadding.x,
				ImGui::GetFontSize());
			ImVec2 textSize{};
			float rowHeight{ 0.0f };
			{
				const Theme::FontGuard font{ Theme::FontRole::kTitle };
				ImGui::SetWindowFontScale(textScale);
				textSize = ImGui::CalcTextSize(breadcrumb.c_str());
				rowHeight = (std::max)(
					textSize.y,
					a_drawClose ? buttonExtent : 0.0f);
				const auto titleFontSize = ImGui::GetFontSize();
				const ImVec2 titlePosition{
					titleMinX,
					start.y + CurrentFontOpticalTextOffsetY(
						rowHeight,
						titleFontSize)
				};
				if (titleMaxX > titleMinX)
				{
					ImGui::RenderTextEllipsis(
						ImGui::GetWindowDrawList(),
						titlePosition,
						{ titleMaxX, start.y + rowHeight },
						titleMaxX,
						breadcrumb.c_str(),
						nullptr,
						&textSize);
				}
				ImGui::SetWindowFontScale(1.0f);
				ImGui::Dummy({
					contentMaxX - start.x,
					rowHeight
				});
			}
			auto closePressed = false;
			if (a_drawClose)
			{
				closePressed = DrawCompactChromeButton(
					"##DearModdingUI.HostCloseButton",
					{
						buttonLayout.controlMinX,
						start.y + CenterOffsetY(rowHeight, buttonExtent)
					},
					{ buttonWidth, buttonExtent },
					hasCloseGlyph ? PhosphorGlyph::kX : char32_t{},
					hasCloseGlyph ? nullptr : closeLabel,
					"Close menu",
					hasCloseGlyph ?
						IconColor(ImGui::GetColorU32(ImGuiCol_Text)) :
						ImGui::GetColorU32(ImGuiCol_Text),
					false,
					iconSize);
			}

			ImGui::SetCursorScreenPos({
				start.x,
				start.y + rowHeight + ImGui::GetStyle().ItemSpacing.y
			});
			ImGui::SeparatorEx(
				ImGuiSeparatorFlags_Horizontal,
				Theme::kSeparatorThickness);
			ImGui::Spacing();
			return closePressed;
		}

		void DrawCategoryHeader(
			const char* a_key,
			const NavigationClient& a_client,
			const char* a_name,
			bool& a_expanded,
			size_t a_count) noexcept
		{
			const auto glyph = ResolveCategoryIconGlyph(
				a_name,
				a_client.displayName,
				a_client.id);
			DrawCollapsingSectionHeader(
				a_key,
				a_name,
				glyph,
				a_expanded,
				a_count);
		}

		void DrawSearchIcon(
			const ImVec2& a_position,
			float a_size,
			float a_alpha) noexcept
		{
			auto* drawList = ImGui::GetWindowDrawList();
			const ImVec2 center{
				a_position.x + a_size * 0.46f,
				a_position.y + a_size * 0.5f
			};
			const auto radius = a_size * 0.3f;
			auto color = Theme::kFullPalette[ImGuiCol_Text];
			color.w *= a_alpha;
			const auto packed = ImGui::GetColorU32(color);
			drawList->AddCircle(
				center,
				radius,
				packed,
				12,
				a_size * Theme::kSearchIconStrokeRatio);
			const ImVec2 handleStart{
				center.x + radius * 0.81f,
				center.y + radius * 0.81f
			};
			const ImVec2 handleEnd{
				handleStart.x + a_size * 0.29f,
				handleStart.y + a_size * 0.29f
			};
			drawList->AddLine(
				handleStart,
				handleEnd,
				packed,
				a_size * Theme::kSearchIconHandleStrokeRatio);
		}

		void DrawPageSearch(std::string& a_search) noexcept
		{
			DrawSearchInput("PageSearchBar", "Search Pages...", a_search);
		}

		[[nodiscard]] std::string CategoryKey(
			const NavigationClient& a_client,
			const NavigationCategory& a_category)
		{
			return a_client.id + "/" + a_category.displayName;
		}

		void DrawClientList(
			const NavigationModel& a_model,
			ShellState& a_state) noexcept
		{
			DrawSectionHeader("Mods", PhosphorGlyph::kSquaresFour);
			const Theme::FontGuard font{ Theme::FontRole::kSubheading };
			const auto* active = a_model.FindClient(a_state.activeClient);
			const char* previewText =
				active ? active->displayName.c_str() : "No mods registered";
			const auto previewGlyph = active ?
				ResolveIconGlyph(IconKind::kClient, active->id) :
				char32_t{};

			ImGui::SetNextItemWidth(-FLT_MIN);
			const auto open = ImGui::BeginCombo(
				"##DearModdingClientSelector",
				"",
				ImGuiComboFlags_CustomPreview);
			if (open)
			{
				for (const auto& client : a_model.clients)
				{
					const auto glyph =
						ResolveIconGlyph(IconKind::kClient, client.id);
					const auto textSize =
						ImGui::CalcTextSize(client.displayName.c_str());
					const auto layout = DecideInlineIconLayout(
						HasIconGlyph(glyph),
						textSize.x,
						textSize.y,
						ImGui::GetFontSize(),
						ImGui::GetStyle().ItemSpacing.x);
					const auto rowHeight =
						layout.contentHeight +
						ImGui::GetStyle().FramePadding.y * 2.0f;
					const auto selected =
						client.handle == a_state.activeClient;
					const auto label =
						"###DearModdingClient/" + client.id;
					if (ImGui::Selectable(
							label.c_str(),
							selected,
							ImGuiSelectableFlags_None,
							{ 0.0f, rowHeight }))
					{
						HostSettings::NotifyModSelected();
						Theme::ApplyStyle();
						(void)SelectClient(a_model, client.handle, a_state);
					}
					const auto itemMin = ImGui::GetItemRectMin();
					const auto itemMax = ImGui::GetItemRectMax();
					const ImVec4 clip{
						itemMin.x,
						itemMin.y,
						itemMax.x,
						itemMax.y
					};
					DrawIconText(
						{
							itemMin.x + ImGui::GetStyle().FramePadding.x,
							itemMin.y
						},
						rowHeight,
						glyph,
						client.displayName.c_str(),
						ImGui::GetColorU32(ImGuiCol_Text),
						&clip);
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			if (ImGui::BeginComboPreview())
			{
				const auto position = ImGui::GetCursorScreenPos();
				const auto textSize = ImGui::CalcTextSize(previewText);
				const auto layout = DecideInlineIconLayout(
					active && HasIconGlyph(previewGlyph),
					textSize.x,
					textSize.y,
					ImGui::GetFontSize(),
					ImGui::GetStyle().ItemSpacing.x);
				DrawIconText(
					position,
					layout.contentHeight,
					previewGlyph,
					previewText,
					ImGui::GetColorU32(ImGuiCol_Text));
				ImGui::Dummy({ layout.contentWidth, layout.contentHeight });
				ImGui::EndComboPreview();
			}
		}

		void DrawPageList(
			const NavigationClient& a_client,
			ShellState& a_state) noexcept
		{
			DrawSectionHeader("Pages", PhosphorGlyph::kFiles);
			DrawPageSearch(a_state.search);

			for (const auto& category : a_client.categories)
			{
				const auto hasMatch = std::ranges::any_of(
					category.pages,
					[&](const auto& a_page) {
						return Matches(a_page, a_state.search);
					});
				if (!hasMatch)
					continue;

				const auto key = CategoryKey(a_client, category);
				auto state =
					a_state.categoryExpansion.try_emplace(key, true).first;
				{
					const Theme::FontGuard font{ Theme::FontRole::kHeading };
					DrawCategoryHeader(
						key.c_str(),
						a_client,
						category.displayName.c_str(),
						state->second,
						category.pages.size());
				}
				if (!state->second)
					continue;

				for (const auto& page : category.pages)
				{
					if (!Matches(page, a_state.search))
						continue;
					const auto failed = PageFailed(page.handle);
					const auto selected = page.handle == a_state.activePage;
					const auto label =
						" " + page.displayName + " ###DearModdingPage/" + page.id;
					const Theme::FontGuard font{ Theme::FontRole::kSubheading };
					if (failed)
					{
						ImGui::PushStyleColor(
							ImGuiCol_Text,
							Theme::kStatusPaletteDefaults.error);
					}
					if (ImGui::Selectable(
							label.c_str(),
							selected,
							ImGuiSelectableFlags_SpanAllColumns))
					{
						HostSettings::NotifyModSelected();
						a_state.activePage = page.handle;
					}
					if (failed)
						ImGui::PopStyleColor();
				}
			}
		}

		void DrawNavigation(
			const NavigationModel& a_model,
			ShellState& a_state) noexcept
		{
			ImGui::TableNextColumn();
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4());
			if (ImGui::BeginListBox(
					"##DearModdingMenusList",
					{ -FLT_MIN, -FLT_MIN }))
			{
				DrawClientList(a_model, a_state);
				if (const auto* client =
						a_model.FindClient(a_state.activeClient))
					DrawPageList(*client, a_state);
				ImGui::EndListBox();
			}
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
		}

		void DrawFailure(const NavigationPage& a_page) noexcept
		{
			{
				const Theme::FontGuard font{ Theme::FontRole::kHeading };
				ImGui::TextColored(
					Theme::kStatusPaletteDefaults.error,
					"%s could not be displayed",
					a_page.displayName.c_str());
			}
			ImGui::Spacing();
			ImGui::TextWrapped(
				"The mod's page callback failed and has been disabled for this session. "
				"Other pages remain available.");
		}

		[[nodiscard]] bool ActionHasGlyph(
			const RegisteredAction& a_action,
			char32_t& a_glyph) noexcept
		{
			a_glyph = ResolveActionIconGlyph(a_action.iconName);
			return HasIconGlyph(a_glyph);
		}

		[[nodiscard]] float ActionButtonWidthFor(
			const RegisteredAction& a_action,
			float a_buttonExtent) noexcept
		{
			char32_t glyph{};
			const auto hasGlyph = ActionHasGlyph(a_action, glyph);
			return ActionButtonWidth(
				hasGlyph,
				ImGui::CalcTextSize(a_action.displayLabel.c_str()).x,
				a_buttonExtent,
				ImGui::GetStyle().FramePadding.x);
		}

		void DrawClientActions(
			const NavigationPage& a_page,
			const PageActionRowLayout& a_layout,
			float a_rowTop,
			float a_rowHeight,
			float a_buttonExtent) noexcept
		{
			auto positionX = a_layout.actionsMinX;
			const auto spacing = ImGui::GetStyle().ItemSpacing.x;
			for (const auto& action : OrderedActions())
			{
				if (action.client != a_page.client)
					continue;

				char32_t glyph{};
				if (!ActionHasGlyph(action, glyph))
					glyph = {};
				const auto width = ActionButtonWidthFor(action, a_buttonExtent);
				const auto failed = ActionFailed(action.handle);
				ImGui::PushID(&action);
				ImGui::BeginDisabled(failed);
				const auto pressed = DrawCompactChromeButton(
					"##ClientAction",
					{
						positionX,
						a_rowTop + (a_rowHeight - a_buttonExtent) * 0.5f
					},
					{ width, a_buttonExtent },
					glyph,
					glyph ? nullptr : action.displayLabel.c_str(),
					failed ?
						"Action disabled after its callback failed." :
						(action.tooltip.empty() ?
								action.displayLabel.c_str() :
								action.tooltip.c_str()),
					glyph ?
						IconColor(ImGui::GetColorU32(ImGuiCol_Text)) :
						ImGui::GetColorU32(ImGuiCol_Text));
				ImGui::EndDisabled();
				if (pressed && !failed)
					(void)InvokeAction(action.handle);
				ImGui::PopID();
				positionX += width + spacing;
			}
		}

		void DrawPageHeader(const NavigationPage& a_page) noexcept
		{
			const auto start = ImGui::GetCursorScreenPos();
			const auto contentMaxX =
				start.x + ImGui::GetContentRegionAvail().x;
			const auto actionButtonExtent = TitleBarButtonExtent(
				ImGui::GetFontSize(), kTitleBarButtonPadding);
			size_t actionCount = 0;
			float actionButtonWidthSum = 0.0f;
			for (const auto& action : OrderedActions())
			{
				if (action.client != a_page.client)
					continue;
				++actionCount;
				actionButtonWidthSum +=
					ActionButtonWidthFor(action, actionButtonExtent);
			}
			const auto actionLayout = ResolvePageActionRowLayout(
				start.x,
				contentMaxX,
				actionButtonWidthSum,
				actionCount,
				ImGui::GetStyle().ItemSpacing.x);
			ImVec2 titleSize{};
			{
				const Theme::FontGuard font{ Theme::FontRole::kTitle };
				ImGui::SetWindowFontScale(Theme::kFeatureTitleScale);
				titleSize = ImGui::CalcTextSize(a_page.displayName.c_str());
				const auto rowHeight =
					(std::max)(
						titleSize.y,
						actionCount > 0 ? actionButtonExtent : 0.0f);
				if (actionLayout.titleMaxX > start.x)
				{
					ImGui::RenderTextEllipsis(
						ImGui::GetWindowDrawList(),
						start,
						{ actionLayout.titleMaxX, start.y + rowHeight },
						actionLayout.titleMaxX,
						a_page.displayName.c_str(),
						nullptr,
						&titleSize);
				}
				ImGui::SetWindowFontScale(1.0f);
				ImGui::Dummy({
					contentMaxX - start.x,
					rowHeight
				});
			}
			const auto titleHeight =
				(std::max)(
					titleSize.y,
					actionCount > 0 ? actionButtonExtent : 0.0f);
			DrawClientActions(
				a_page,
				actionLayout,
				start.y,
				titleHeight,
				actionButtonExtent);

			if (!a_page.summary.empty())
			{
				ImGui::SetCursorScreenPos({
					start.x,
					start.y +
						titleHeight +
						ImGui::GetStyle().ItemSpacing.y * 0.25f
				});
				auto color = Theme::kFullPalette[ImGuiCol_Text];
				color.w *= Theme::kVersionTextOpacity;
				const Theme::FontGuard font{ Theme::FontRole::kSubtext };
				ImGui::PushStyleColor(ImGuiCol_Text, color);
				ImGui::TextWrapped("%s", a_page.summary.c_str());
				ImGui::PopStyleColor();
			}
			ImGui::Spacing();
			ImGui::SeparatorEx(
				ImGuiSeparatorFlags_Horizontal,
				Theme::kSeparatorThickness);
			ImGui::Spacing();
		}

		void DrawContent(
			const NavigationModel& a_model,
			ShellState& a_state) noexcept
		{
			ImGui::TableNextColumn();
			if (!ImGui::BeginChild(
					"##DearModdingPageFrame",
					{},
					ImGuiChildFlags_Borders))
			{
				ImGui::EndChild();
				return;
			}

			if (HostSettings::IsPanelOpen())
			{
				const auto start = ImGui::GetCursorScreenPos();
				const auto contentMaxX =
					start.x + ImGui::GetContentRegionAvail().x;
				const auto buttonExtent = TitleBarButtonExtent(
					ImGui::GetFontSize(), kTitleBarButtonPadding);
				const auto hasCloseGlyph = HasIconGlyph(PhosphorGlyph::kX);
				constexpr const char* fallbackLabel{ "Back" };
				const auto closeButtonWidth = ActionButtonWidth(
					hasCloseGlyph,
					ImGui::CalcTextSize(fallbackLabel).x,
					buttonExtent,
					ImGui::GetStyle().FramePadding.x);
				const std::array actions{
					HostSettingsTitleButton{
						SettingsAction::kReset,
						"##DearModdingUI.HostSettingsResetButton",
						"Reset",
						"Reset loads shipped interface defaults into the draft. "
						"Use Apply to save them.",
						SettingsActionButtonWidth(
							SettingsAction::kReset,
							"Reset",
							buttonExtent) },
					HostSettingsTitleButton{
						SettingsAction::kRevert,
						"##DearModdingUI.HostSettingsRevertButton",
						"Revert",
						"Revert discards pending interface edits and restores "
						"saved settings.",
						SettingsActionButtonWidth(
							SettingsAction::kRevert,
							"Revert",
							buttonExtent) },
					HostSettingsTitleButton{
						SettingsAction::kApply,
						"##DearModdingUI.HostSettingsApplyButton",
						"Apply",
						"Apply saves interface settings to AddictolCustom.toml. "
						"Appearance previews update immediately; typography "
						"rebuilds once if needed.",
						SettingsActionButtonWidth(
							SettingsAction::kApply,
							"Apply",
							buttonExtent) }
				};
				const auto dirty =
					HostSettingsTitleActionEnabled(SettingsAction::kApply);
				const std::array actionWidths{
					actions[0].width,
					actions[1].width,
					actions[2].width
				};
				const auto actionButtonWidthSum =
					ResolveSettingsActionButtonWidthSum(
						actionWidths,
						dirty,
						dirty ? 1u : 0u);
				const auto spacing = ImGui::GetStyle().ItemSpacing.x;
				const auto layout = ResolveHostSettingsTitleRowLayout(
					start.x,
					contentMaxX,
					actionButtonWidthSum,
					actions.size(),
					closeButtonWidth,
					spacing);
				ImVec2 titleSize{};
				{
					const Theme::FontGuard font{ Theme::FontRole::kTitle };
					ImGui::SetWindowFontScale(Theme::kFeatureTitleScale);
					titleSize = ImGui::CalcTextSize("Interface Settings");
					const auto rowHeight =
						(std::max)(titleSize.y, buttonExtent);
					if (layout.titleMaxX > start.x)
					{
						ImGui::RenderTextEllipsis(
							ImGui::GetWindowDrawList(),
							start,
							{ layout.titleMaxX, start.y + rowHeight },
							layout.titleMaxX,
							"Interface Settings",
							nullptr,
							&titleSize);
					}
					ImGui::SetWindowFontScale(1.0f);
					ImGui::Dummy({
						contentMaxX - start.x,
						rowHeight
					});
				}
				const auto rowHeight =
					(std::max)(titleSize.y, buttonExtent);
				auto actionX = layout.actionsMinX;
				for (const auto& action : actions)
				{
					const auto enabled =
						SettingsActionEnabled(action.action, dirty);
					const auto pressed = DrawSettingsActionButton(
						action.id,
						{
							actionX,
							start.y + (rowHeight - buttonExtent) * 0.5f
						},
						{ action.width, buttonExtent },
						action.action,
						action.label,
						action.tooltip,
						enabled);
					if (pressed)
						InvokeHostSettingsTitleAction(action.action);
					actionX += action.width + spacing;
				}
				const auto dismiss = DrawCompactChromeButton(
					"##DearModdingUI.HostSettingsBackButton",
					{
						layout.closeMinX,
						start.y + (rowHeight - buttonExtent) * 0.5f
					},
					{ closeButtonWidth, buttonExtent },
					hasCloseGlyph ? PhosphorGlyph::kX : char32_t{},
					hasCloseGlyph ? nullptr : fallbackLabel,
					"Back to the current mod page",
					hasCloseGlyph ?
						IconColor(ImGui::GetColorU32(ImGuiCol_Text)) :
						ImGui::GetColorU32(ImGuiCol_Text));
				ImGui::Spacing();
				ImGui::SeparatorEx(
					ImGuiSeparatorFlags_Horizontal,
					Theme::kSeparatorThickness);
				ImGui::Spacing();
				DrawHostSettingsControls();
				if (dismiss)
					HostSettings::DismissPanel();
				ImGui::EndChild();
				return;
			}

			const auto* page = a_model.FindPage(a_state.activePage);
			if (!page)
			{
				ImGui::TextDisabled("Please select a page from the left.");
				ImGui::EndChild();
				return;
			}

			DrawPageHeader(*page);
			const auto presentation =
				DecidePagePresentation(page, PageFailed(page->handle));
			if (presentation == PagePresentation::kFailure)
			{
				DrawFailure(*page);
			}
			else if (presentation == PagePresentation::kContent)
			{
				ImGui::PushID(static_cast<int>(page->handle));
				if (!DrawPage(page->handle))
				{
					ImGui::Spacing();
					ImGui::SeparatorEx(
						ImGuiSeparatorFlags_Horizontal,
						Theme::kSeparatorThickness);
					ImGui::Spacing();
					DrawFailure(*page);
				}
				ImGui::PopID();
			}
			ImGui::EndChild();
		}

		void DrawFooter(
			const NavigationModel& a_model,
			const ShellState& a_state) noexcept
		{
			const auto status = CurrentStatus();
			const auto start = ImGui::GetCursorScreenPos();
			const auto contentMaxX =
				start.x + ImGui::GetContentRegionAvail().x;
			const auto iconSize = HostChromeIconSize(ImGui::GetFontSize());
			const auto settingsButtonExtent = HostChromeButtonExtent(
				ImGui::GetFontSize(), kTitleBarButtonPadding);
			const auto dismissButtonExtent = TitleBarButtonExtent(
				ImGui::GetFontSize(), kTitleBarButtonPadding);
			const auto rowHeight = (std::max)(
				ImGui::GetFrameHeight(),
				settingsButtonExtent);
			const auto hasGearGlyph = HasIconGlyph(PhosphorGlyph::kGear);
			constexpr const char* fallbackLabel{ "Settings" };
			const auto buttonWidth = ActionButtonWidth(
				hasGearGlyph,
				ImGui::CalcTextSize(fallbackLabel).x,
				settingsButtonExtent,
				ImGui::GetStyle().FramePadding.x);
			const auto persistent = status && status->persistent;
			const auto hasDismissGlyph = HasIconGlyph(PhosphorGlyph::kX);
			constexpr const char* dismissLabel{ "Dismiss" };
			const auto dismissWidth = ActionButtonWidth(
				hasDismissGlyph,
				ImGui::CalcTextSize(dismissLabel).x,
				dismissButtonExtent,
				ImGui::GetStyle().FramePadding.x);
			const auto statusTextSize = status ?
				ImGui::CalcTextSize(status->attributedText.c_str()) :
				ImVec2{};
			const auto metadataLayout = ResolveFooterStatusLayout(
				start.x,
				contentMaxX,
				buttonWidth,
				start.x,
				statusTextSize.x,
				dismissWidth,
				ImGui::GetStyle().ItemSpacing.x,
				rowHeight,
				ImGui::GetStyle().ItemSpacing.y,
				ImGui::GetStyle().WindowPadding.y,
				Theme::kSeparatorThickness,
				status.has_value(),
				persistent);
			ImGui::PushClipRect(
				start,
				{ metadataLayout.metadataMaxX, start.y + rowHeight },
				true);
			ImGui::SetCursorScreenPos({
				start.x,
				start.y + CurrentFontOpticalTextOffsetY(
					rowHeight,
					ImGui::GetFontSize())
			});
			ImGui::BulletText("Host: Evil Modding");
			auto metadataRight = ImGui::GetItemRectMax().x;
			if (const auto* client =
					a_model.FindClient(a_state.activeClient))
			{
				ImGui::SameLine();
				ImGui::BulletText("Mod: %s", client->displayName.c_str());
				ImGui::SameLine();
				ImGui::BulletText(
					"Version: %u.%u",
					client->version >> 16,
					client->version & 0xFFFFu);
				metadataRight = ImGui::GetItemRectMax().x;
			}
			ImGui::PopClipRect();
			auto layout = ResolveFooterStatusLayout(
				start.x,
				contentMaxX,
				buttonWidth,
				metadataRight,
				statusTextSize.x,
				dismissWidth,
				ImGui::GetStyle().ItemSpacing.x,
				rowHeight,
				ImGui::GetStyle().ItemSpacing.y,
				ImGui::GetStyle().WindowPadding.y,
				Theme::kSeparatorThickness,
				status.has_value(),
				persistent);
			std::optional<StatusTextPresentation> presentation;
			auto visibleTextSize = statusTextSize;
			const auto preliminaryStatusWidth =
				layout.statusMaxX - layout.statusMinX;
			if (status && preliminaryStatusWidth > 0.0f)
			{
				try
				{
					presentation = FitStatusText(
						status->attributedText,
						preliminaryStatusWidth,
						[](std::string_view a_text) {
							return ImGui::CalcTextSize(
								a_text.data(),
								a_text.data() + a_text.size()).x;
						});
					visibleTextSize = ImGui::CalcTextSize(
						presentation->visible.data(),
						presentation->visible.data() +
							presentation->visible.size());
					layout = ResolveFooterStatusLayout(
						start.x,
						contentMaxX,
						buttonWidth,
						metadataRight,
						visibleTextSize.x,
						dismissWidth,
						ImGui::GetStyle().ItemSpacing.x,
						rowHeight,
						ImGui::GetStyle().ItemSpacing.y,
						ImGui::GetStyle().WindowPadding.y,
						Theme::kSeparatorThickness,
						true,
						persistent);
				}
				catch (...)
				{}
			}
			const auto statusWidth = layout.statusMaxX - layout.statusMinX;
			if (statusWidth > 0.0f)
			{
				ImGui::SetCursorScreenPos({ layout.statusMinX, start.y });
				ImGui::InvisibleButton(
					"##DearModdingUI.StatusArea",
					{ statusWidth, layout.rowHeight });
				const auto hovered = ImGui::IsItemHovered(
					ImGuiHoveredFlags_DelayNormal);
				if (status && presentation)
				{
					const ImVec4 clip{
						layout.statusMinX,
						start.y,
						layout.statusMaxX,
						start.y + layout.rowHeight
					};
					ImGui::GetWindowDrawList()->AddText(
						ImGui::GetFont(),
						ImGui::GetFontSize(),
						{
							layout.statusMinX,
							start.y + CurrentFontOpticalTextOffsetY(
								layout.rowHeight,
								ImGui::GetFontSize())
						},
						ImGui::ColorConvertFloat4ToU32(
							status->severity ==
									DMUI_STATUS_SEVERITY_SUCCESS ?
								Theme::kStatusPaletteDefaults.success :
								status->severity ==
										DMUI_STATUS_SEVERITY_WARNING ?
									Theme::kStatusPaletteDefaults.warning :
									status->severity ==
											DMUI_STATUS_SEVERITY_ERROR ?
										Theme::kStatusPaletteDefaults.error :
										Theme::kStatusPaletteDefaults.info),
						presentation->visible.data(),
						presentation->visible.data() +
							presentation->visible.size(),
						0.0f,
						&clip);
					if (hovered && presentation->truncated)
						ImGui::SetTooltip(
							"%s",
							presentation->full.c_str());
				}
			}

			const auto actualDismissWidth =
				layout.dismissMaxX - layout.dismissMinX;
			if (persistent && actualDismissWidth > 0.0f &&
				DrawCompactChromeButton(
						"##DearModdingUI.StatusDismissButton",
						{
							layout.dismissMinX,
							start.y + CenterOffsetY(
								layout.rowHeight,
								dismissButtonExtent)
						},
						{ actualDismissWidth, dismissButtonExtent },
						hasDismissGlyph ? PhosphorGlyph::kX : char32_t{},
						hasDismissGlyph ? nullptr : dismissLabel,
						"Dismiss status",
						hasDismissGlyph ?
							IconColor(ImGui::GetColorU32(ImGuiCol_Text)) :
							ImGui::GetColorU32(ImGuiCol_Text)))
			{
				(void)DismissStatus(status->generation);
			}
			if (DrawCompactChromeButton(
					"##DearModdingUI.HostSettingsButton",
					{
						layout.settingsMinX,
						start.y + CenterOffsetY(
							layout.rowHeight,
							settingsButtonExtent)
					},
					{ buttonWidth, settingsButtonExtent },
					hasGearGlyph ? PhosphorGlyph::kGear : char32_t{},
					hasGearGlyph ? nullptr : fallbackLabel,
					"Interface settings",
					hasGearGlyph ?
						IconColor(ImGui::GetColorU32(ImGuiCol_Text)) :
						ImGui::GetColorU32(ImGuiCol_Text),
					HostSettings::IsPanelOpen(),
					iconSize))
			{
				HostSettings::TogglePanel(true);
			}
			ImGui::SetCursorScreenPos(start);
			ImGui::Dummy({ contentMaxX - start.x, layout.rowHeight });
		}

		void SaveLayout() noexcept
		{
			const auto& io = ImGui::GetIO();
			if (io.IniFilename)
				ImGui::SaveIniSettingsToDisk(io.IniFilename);
		}
	}

	float SettingsActionButtonExtent() noexcept
	{
		return TitleBarButtonExtent(
			ImGui::GetFontSize(),
			kTitleBarButtonPadding);
	}

	void DrawSearchInput(
		const char* a_id,
		const char* a_hint,
		std::string& a_search) noexcept
	{
		ImGui::PushID(a_id);
		const auto scale = Theme::SearchScale();
		const auto iconSize = Theme::kSearchIconSize * scale;
		const auto iconSpace =
			iconSize + Theme::kSearchInputPaddingExtra * scale;
		const auto cursor = ImGui::GetCursorScreenPos();
		const auto availableWidth = ImGui::GetContentRegionAvail().x;
		const auto frameHeight = ImGui::GetFrameHeight();

		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4());
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4());
		ImGui::PushStyleColor(
			ImGuiCol_FrameBgActive,
			ImVec4(0.3f, 0.3f, 0.3f, 0.9f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4());
		ImGui::PushStyleColor(
			ImGuiCol_Text,
			Theme::kFullPalette[ImGuiCol_Text]);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ImGui::PushStyleVar(
			ImGuiStyleVar_FramePadding,
			ImVec2(
				iconSpace,
				Theme::kSearchInputFramePaddingY * scale));
		ImGui::SetNextItemWidth(availableWidth);

		char buffer[256]{};
		strncpy_s(buffer, a_search.c_str(), sizeof(buffer) - 1);
		if (ImGui::InputTextWithHint(
				"##search",
				a_hint,
				buffer,
				sizeof(buffer)))
			a_search = buffer;

		DrawSearchIcon(
			{
				cursor.x + Theme::kSearchIconOffsetX * scale,
				cursor.y + (frameHeight - iconSize) * 0.5f
			},
			iconSize,
			Theme::kSearchIconAlpha);
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(5);
		ImGui::PopID();
	}

	float SettingsActionButtonWidth(
		SettingsAction a_action,
		const char* a_fallbackLabel,
		float a_buttonExtent) noexcept
	{
		const auto presentation = ResolveSettingsActionButtonPresentation(
			a_action,
			HasIconGlyph(SettingsActionGlyph(a_action)));
		return ActionButtonWidth(
			!presentation.useTextFallback,
			a_fallbackLabel ? ImGui::CalcTextSize(a_fallbackLabel).x : 0.0f,
			a_buttonExtent,
			ImGui::GetStyle().FramePadding.x);
	}

	bool DrawSettingsActionButton(
		const char* a_id,
		const ImVec2& a_origin,
		const ImVec2& a_size,
		SettingsAction a_action,
		const char* a_fallbackLabel,
		const char* a_tooltip,
		bool a_enabled) noexcept
	{
		const auto presentation = ResolveSettingsActionButtonPresentation(
			a_action,
			HasIconGlyph(SettingsActionGlyph(a_action)));
		ImGui::BeginDisabled(!a_enabled);
		const auto pressed = DrawCompactChromeButton(
			a_id,
			a_origin,
			a_size,
			presentation.glyph,
			presentation.useTextFallback ? a_fallbackLabel : nullptr,
			a_tooltip,
			presentation.useTextFallback ?
				ImGui::GetColorU32(ImGuiCol_Text) :
				IconColor(ImGui::GetColorU32(ImGuiCol_Text)));
		ImGui::EndDisabled();
		return pressed && a_enabled;
	}

	void DrawCollapsingSectionHeader(
		const char* a_key,
		const char* a_text,
		char32_t a_glyph,
		bool& a_expanded,
		size_t a_count) noexcept
	{
		char text[256]{};
		std::snprintf(text, sizeof(text), "%s (%zu)", a_text, a_count);
		auto* drawList = ImGui::GetWindowDrawList();
		const auto position = ImGui::GetCursorScreenPos();
		const auto availableWidth = ImGui::GetContentRegionAvail().x;
		const auto textSize = ImGui::CalcTextSize(text);
		const auto layout = DecideInlineIconLayout(
			HasIconGlyph(a_glyph),
			textSize.x,
			textSize.y,
			ImGui::GetFontSize(),
			ImGui::GetStyle().ItemSpacing.x);
		const auto lineY = position.y + textSize.y * 0.5f;
		const auto lineLength =
			(availableWidth - layout.contentWidth - 20.0f) * 0.5f;

		ImGui::PushID(a_key);
		ImGui::SetCursorScreenPos(position);
		const auto clicked = ImGui::InvisibleButton(
			"##CollapsingSectionHeader",
			{ availableWidth, layout.contentHeight + 4.0f });
		const auto hovered = ImGui::IsItemHovered();

		auto color = Theme::kFullPalette[ImGuiCol_Text];
		if (!a_expanded)
			color.w *= Theme::kFeatureHeadingDefaults.minimizedFactor;
		if (hovered)
			color.w *= 0.8f;
		const auto packed = ImGui::GetColorU32(color);

		if (lineLength > 0.0f)
		{
			drawList->AddLine(
				{ position.x, lineY },
				{ position.x + lineLength, lineY },
				packed);
		}
		const auto rightLineStart =
			position.x + lineLength + 10.0f + layout.contentWidth + 10.0f;
		if (rightLineStart < position.x + availableWidth)
		{
			drawList->AddLine(
				{ rightLineStart, lineY },
				{ position.x + availableWidth, lineY },
				packed);
		}
		DrawIconText(
			{ position.x + lineLength + 10.0f, position.y + 2.0f },
			layout.contentHeight,
			a_glyph,
			text,
			packed);
		if (clicked)
			a_expanded = !a_expanded;
		ImGui::PopID();

		ImGui::SetCursorScreenPos(
			{ position.x, position.y + layout.contentHeight + 8.0f });
		ImGui::Dummy({ availableWidth, 0.0f });
	}

	void DrawSectionHeader(const char* a_text, char32_t a_glyph) noexcept
	{
		auto* drawList = ImGui::GetWindowDrawList();
		const auto position = ImGui::GetCursorScreenPos();
		const auto availableWidth = ImGui::GetContentRegionAvail().x;
		const auto textSize = ImGui::CalcTextSize(a_text);
		const auto layout = DecideInlineIconLayout(
			HasIconGlyph(a_glyph),
			textSize.x,
			textSize.y,
			ImGui::GetFontSize(),
			ImGui::GetStyle().ItemSpacing.x);
		const auto lineY = position.y + layout.contentHeight * 0.5f;
		const auto lineLength =
			(availableWidth - layout.contentWidth - 20.0f) * 0.5f;
		const auto color = ImGui::GetColorU32(
			Theme::kFullPalette[ImGuiCol_Text]);

		if (lineLength > 0.0f)
		{
			drawList->AddLine(
				{ position.x, lineY },
				{ position.x + lineLength, lineY },
				color);
		}
		const auto rightLineStart =
			position.x + lineLength + 10.0f + layout.contentWidth + 10.0f;
		if (rightLineStart < position.x + availableWidth)
		{
			drawList->AddLine(
				{ rightLineStart, lineY },
				{ position.x + availableWidth, lineY },
				color);
		}
		DrawIconText(
			{ position.x + lineLength + 10.0f, position.y + 2.0f },
			layout.contentHeight,
			a_glyph,
			a_text,
			color);
		ImGui::SetCursorScreenPos(
			{ position.x, position.y + layout.contentHeight + 8.0f });
		ImGui::Dummy({ availableWidth, 0.0f });
	}

	void DrawShell() noexcept
	{
		if (HostSettings::IsPanelOpen() &&
			ImGui::IsKeyPressed(ImGuiKey_Escape, false))
			HostSettings::DismissPanel();
		Theme::ApplyStyle();
		const auto& model = Navigation();
		auto& state = State();
		const auto requested = SelectedPage();
		state.activePage =
			ResolvePageSelection(model, requested, state.activePage);
		if (requested != DMUI_INVALID_PAGE_HANDLE)
			ClearPageSelection(requested);
		if (const auto* client =
				model.FindClientForPage(state.activePage))
			state.activeClient = client->handle;

		const auto* viewport = ImGui::GetMainViewport();
		ImGui::DockSpaceOverViewport(
			0,
			viewport,
			ImGuiDockNodeFlags_PassthruCentralNode);
		ImGui::SetNextWindowPos(
			{ viewport->Size.x * 0.5f, viewport->Size.y * 0.5f },
			ImGuiCond_FirstUseEver,
			{ 0.5f, 0.5f });
		ImGui::SetNextWindowSize(
			{ viewport->Size.x * 0.8f, viewport->Size.y * 0.8f },
			ImGuiCond_FirstUseEver);

		ImGuiWindowClass windowClass{};
		windowClass.ClassId = ImHashStr("DearModdingUI.Host");
		windowClass.DockingAllowUnclassed = true;
		ImGui::SetNextWindowClass(&windowClass);

		auto open = true;
		auto windowFlags =
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoScrollbar;
		static bool wasDocked = false;
		if (!wasDocked)
			windowFlags |= ImGuiWindowFlags_NoTitleBar;

		const auto visible = BeginWithRoundedTitleBarButtons(
			"Evil Modding###DearModdingUI.Host",
			&open,
			windowFlags);
		wasDocked = ImGui::IsWindowDocked();

		const auto position = ImGui::GetWindowPos();
		const auto size = ImGui::GetWindowSize();
		const auto framebufferScale =
			ImGui::GetIO().DisplayFramebufferScale;
		BackgroundBlur::SetHostWindow(
			(position.x - viewport->Pos.x) * framebufferScale.x,
			(position.y - viewport->Pos.y) * framebufferScale.y,
			(position.x + size.x - viewport->Pos.x) * framebufferScale.x,
			(position.y + size.y - viewport->Pos.y) * framebufferScale.y,
			ImGui::GetStyle().WindowRounding *
				(std::max)(framebufferScale.x, framebufferScale.y));

		if (visible)
		{
			const auto drawHeaderClose =
				ShouldDrawHeaderClose(
					ImGui::IsWindowDocked(),
					(ImGui::GetCurrentWindow()->Flags &
						ImGuiWindowFlags_NoTitleBar) != 0);
			if (DrawHeader(model, state, drawHeaderClose))
				open = false;
			const auto footerButtonExtent = HostChromeButtonExtent(
				ImGui::GetFontSize(),
				kTitleBarButtonPadding);
			const auto footerHeight =
				ReservedFooterHeight(
					(std::max)(
						ImGui::GetFrameHeight(),
						footerButtonExtent),
					ImGui::GetStyle().ItemSpacing.y,
					ImGui::GetStyle().WindowPadding.y,
					Theme::kSeparatorThickness);
			ImGui::BeginChild(
				"Dear Modding Menus Table",
				{ 0.0f, -footerHeight });
			if (ImGui::BeginTable(
					"Dear Modding Menus Table",
					2,
					ImGuiTableFlags_SizingStretchProp |
						ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn(
					"##DearModdingList",
					ImGuiTableColumnFlags_None,
					2.0f);
				ImGui::TableSetupColumn(
					"##DearModdingPage",
					ImGuiTableColumnFlags_None,
					8.0f);
				DrawNavigation(model, state);
				DrawContent(model, state);
				ImGui::EndTable();
			}
			ImGui::EndChild();

			ImGui::Spacing();
			ImGui::SeparatorEx(
				ImGuiSeparatorFlags_Horizontal,
				Theme::kSeparatorThickness);
			const auto footerPosition = ImGui::GetCursorScreenPos();
			ImGui::SetCursorScreenPos({
				footerPosition.x,
				footerPosition.y + FooterRowAdjustmentY(
					ImGui::GetStyle().ItemSpacing.y,
					ImGui::GetStyle().WindowPadding.y)
			});
			DrawFooter(model, state);
		}
		ImGui::End();

		if (!open)
		{
			(void)SetMenuVisible(false);
			SaveLayout();
		}
	}
}
