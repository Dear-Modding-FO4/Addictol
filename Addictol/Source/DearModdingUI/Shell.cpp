#include <DearModdingUI/Shell.h>
#include <DearModdingUI/BackgroundBlur.h>
#include <DearModdingUI/Host.h>
#include <DearModdingUI/HostSettings.h>
#include <DearModdingUI/IconGlyphs.h>
#include <DearModdingUI/Theme.h>
#include <DearModdingUI/VisualDecisions.h>
#include <Menu/AdMenu.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
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
				DrawIcon(
					ImGui::GetWindowDrawList(),
					a_glyph,
					{
						a_position.x,
						contentY
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
		inline constexpr char kHostSettingsPopup[]{
			"##DearModdingUI.HostSettings"
		};

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
			ImU32 a_color) noexcept
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
				bounds.Min, bounds.Max, hovered, held, window->DrawList);
			const auto center = bounds.GetCenter();
			if (a_glyph)
			{
				const auto fontSize = ImGui::GetFontSize();
				DrawIcon(
					window->DrawList,
					a_glyph,
					{
						center.x - fontSize * 0.5f,
						center.y - fontSize * 0.5f
					},
					fontSize,
					a_color,
					nullptr);
			}
			else if (a_text)
			{
				const auto textSize = ImGui::CalcTextSize(a_text);
				window->DrawList->AddText(
					{
						center.x - textSize.x * 0.5f,
						center.y - textSize.y * 0.5f
					},
					a_color,
					a_text);
			}
			if (hovered && a_tooltip)
				ImGui::SetTooltip("%s", a_tooltip);
			ImGui::SetCursorScreenPos(restore);
			// A restored cursor must be followed by an item or ImGui reports an unbounded SetCursorPos.
			ImGui::Dummy({ 0.0f, 0.0f });
			return pressed;
		}

		[[nodiscard]] bool DrawHeader(
			const NavigationModel& a_model,
			const ShellState& a_state,
			bool a_drawClose) noexcept
		{
			const auto* client = a_model.FindClient(a_state.activeClient);
			const auto breadcrumb = BuildHostBreadcrumb(
				"Evil Modding",
				client ? std::string_view{ client->displayName } : std::string_view{});
			const auto textScale = Theme::kHeaderFallbackTextScale;
			const auto start = ImGui::GetCursorScreenPos();
			const auto contentMaxX =
				start.x + ImGui::GetContentRegionAvail().x;
			const auto buttonExtent = TitleBarButtonExtent(
				ImGui::GetFontSize(), kTitleBarButtonPadding);
			const auto buttonLayout = ResolveTrailingControlLayout(
				start.x,
				contentMaxX,
				a_drawClose ? buttonExtent : 0.0f,
				a_drawClose ? ImGui::GetStyle().ItemSpacing.x : 0.0f);
			const auto titleMaxX = a_drawClose ?
				buttonLayout.adjacentMaxX :
				contentMaxX;
			ImVec2 textSize{};
			{
				const Theme::FontGuard font{ Theme::FontRole::kTitle };
				ImGui::SetWindowFontScale(textScale);
				textSize = ImGui::CalcTextSize(breadcrumb.c_str());
				const auto rowHeight =
					(std::max)(textSize.y, a_drawClose ? buttonExtent : 0.0f);
				if (titleMaxX > start.x)
				{
					ImGui::RenderTextEllipsis(
						ImGui::GetWindowDrawList(),
						start,
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
				const auto rowHeight =
					(std::max)(textSize.y, buttonExtent);
				closePressed = DrawCompactChromeButton(
					"##DearModdingUI.HostCloseButton",
					{
						buttonLayout.controlMinX,
						start.y + (rowHeight - buttonExtent) * 0.5f
					},
					{ buttonExtent, buttonExtent },
					PhosphorGlyph::kX,
					nullptr,
					"Close menu",
					ImGui::GetColorU32(ImGuiCol_Text));
			}

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
			char text[256]{};
			std::snprintf(text, sizeof(text), "%s (%zu)", a_name, a_count);
			auto* drawList = ImGui::GetWindowDrawList();
			const auto position = ImGui::GetCursorScreenPos();
			const auto availableWidth = ImGui::GetContentRegionAvail().x;
			const auto textSize = ImGui::CalcTextSize(text);
			const auto glyph = ResolveCategoryIconGlyph(
				a_name,
				a_client.displayName,
				a_client.id);
			const auto layout = DecideInlineIconLayout(
				HasIconGlyph(glyph),
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
				"##CategoryHeader",
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
				glyph,
				text,
				packed);
			if (clicked)
				a_expanded = !a_expanded;
			ImGui::PopID();

			ImGui::SetCursorScreenPos(
				{ position.x, position.y + layout.contentHeight + 8.0f });
			ImGui::Dummy({ availableWidth, 0.0f });
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
			ImGui::PushID("PageSearchBar");
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
					"##page_search",
					"Search Pages...",
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
			DrawSectionHeader("Mods");
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
						(void)SelectClient(a_model, client.handle, a_state);
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
			DrawSectionHeader("Pages");
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
						a_state.activePage = page.handle;
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
			const auto start = ImGui::GetCursorScreenPos();
			const auto contentMaxX =
				start.x + ImGui::GetContentRegionAvail().x;
			const auto buttonExtent = TitleBarButtonExtent(
				ImGui::GetFontSize(), kTitleBarButtonPadding);
			const auto layout = ResolveTrailingControlLayout(
				start.x,
				contentMaxX,
				buttonExtent,
				ImGui::GetStyle().ItemSpacing.x);
			ImGui::PushClipRect(
				start,
				{ layout.adjacentMaxX, start.y + ImGui::GetFrameHeight() },
				true);
			ImGui::BulletText("Host: Evil Modding");
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
			}
			ImGui::PopClipRect();
			const auto rowHeight =
				(std::max)(ImGui::GetItemRectMax().y - start.y, buttonExtent);
			if (DrawCompactChromeButton(
					"##DearModdingUI.HostSettingsButton",
					{
						layout.controlMinX,
						start.y + (rowHeight - buttonExtent) * 0.5f
					},
					{ buttonExtent, buttonExtent },
					PhosphorGlyph::kGear,
					nullptr,
					"Interface settings",
					ImGui::GetColorU32(ImGuiCol_Text)))
			{
				HostSettings::RequestPanelOpen(true);
				ImGui::OpenPopup(kHostSettingsPopup);
			}
		}

		void DrawReadOnlyHostFact(
			const char* a_label,
			const char* a_value,
			const char* a_source) noexcept
		{
			{
				const Theme::FontGuard font{ Theme::FontRole::kHeading };
				ImGui::TextUnformatted(a_label);
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(a_value);
			{
				const Theme::FontGuard font{ Theme::FontRole::kSubtext };
				ImGui::TextDisabled("%s", a_source);
			}
			ImGui::Spacing();
		}

		void DrawHostSettingsPopup() noexcept
		{
			const auto requested = HostSettings::IsPanelOpen();
			const auto popupOpen = ImGui::IsPopupOpen(kHostSettingsPopup);
			if (!requested && !popupOpen)
				return;
			if (requested && !popupOpen)
			{
				HostSettings::DismissPanel();
				return;
			}

			const auto* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(
				viewport->GetCenter(),
				ImGuiCond_Appearing,
				{ 0.5f, 0.5f });
			ImGui::SetNextWindowSizeConstraints(
				{ ImGui::GetFontSize() * 24.0f, 0.0f },
				{ viewport->WorkSize.x * 0.6f, FLT_MAX });
			if (!ImGui::BeginPopup(
					kHostSettingsPopup,
					ImGuiWindowFlags_AlwaysAutoResize |
						ImGuiWindowFlags_NoSavedSettings))
			{
				if (requested)
					HostSettings::DismissPanel();
				return;
			}

			if (!requested)
			{
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				return;
			}

			{
				const Theme::FontGuard font{ Theme::FontRole::kTitle };
				ImGui::SetWindowFontScale(Theme::kFeatureTitleScale);
				ImGui::TextUnformatted("Interface Settings");
				ImGui::SetWindowFontScale(1.0f);
			}
			ImGui::TextDisabled(
				"Host-owned options apply immediately and are saved with Addictol.");
			ImGui::Spacing();
			DrawSectionHeader("Appearance");

			auto settings = HostSettings::Current();
			auto changed = false;
			auto iconMode =
				settings.iconColorMode == Theme::IconColorMode::kMonochrome ? 1 : 0;
			constexpr const char* iconModes[]{ "Colored", "Monochrome" };
			if (ImGui::Combo(
					"Icon color mode",
					&iconMode,
					iconModes,
					static_cast<int>(std::size(iconModes))))
			{
				settings.iconColorMode = iconMode == 1 ?
					Theme::IconColorMode::kMonochrome :
					Theme::IconColorMode::kColored;
				changed = true;
			}
			changed |= ImGui::Checkbox(
				"Background blur", &settings.backgroundBlur);
			if (changed)
				HostSettings::Apply(settings);

			ImGui::Spacing();
			DrawSectionHeader("Read-only host facts");
			ImGui::TextDisabled(
				"Menu timing and key bindings are configured outside the game.");
			ImGui::Spacing();

			char toggleKey[32]{};
			const auto key = Menu::ToggleKeyName();
			std::snprintf(
				toggleKey,
				sizeof(toggleKey),
				"%.*s",
				static_cast<int>(key.size()),
				key.data());
			DrawReadOnlyHostFact(
				"Menu toggle key",
				toggleKey,
				"Override [Additional] sMenuToggleKey in Data/F4SE/Plugins/AddictolCustom.toml.");

			char refresh[32]{};
			std::snprintf(
				refresh,
				sizeof(refresh),
				"%u ms",
				Menu::RefreshMs());
			DrawReadOnlyHostFact(
				"Menu refresh interval",
				refresh,
				"Override [Additional] uMenuRefreshMs in Data/F4SE/Plugins/AddictolCustom.toml.");

			const auto* body = Theme::GetFonts().body;
			char typography[32]{};
			std::snprintf(
				typography,
				sizeof(typography),
				"%.0f px",
				body ? body->LegacySize : ImGui::GetFontSize());
			DrawReadOnlyHostFact(
				"Resolved typography size",
				typography,
				"Resolved by DearModdingUI from the current backbuffer height.");

			char scale[32]{};
			std::snprintf(
				scale,
				sizeof(scale),
				"%.2fx",
				Theme::Scale());
			DrawReadOnlyHostFact(
				"Resolved UI scale",
				scale,
				"Resolved by DearModdingUI from the current backbuffer height.");
			ImGui::EndPopup();
		}

		void SaveLayout() noexcept
		{
			const auto& io = ImGui::GetIO();
			if (io.IniFilename)
				ImGui::SaveIniSettingsToDisk(io.IniFilename);
		}
	}

	void DrawSectionHeader(const char* a_text) noexcept
	{
		auto* drawList = ImGui::GetWindowDrawList();
		const auto position = ImGui::GetCursorScreenPos();
		const auto availableWidth = ImGui::GetContentRegionAvail().x;
		const auto textSize = ImGui::CalcTextSize(a_text);
		const auto lineY = position.y + textSize.y * 0.5f;
		const auto lineLength =
			(availableWidth - textSize.x - 20.0f) * 0.5f;
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
			position.x + lineLength + 10.0f + textSize.x + 10.0f;
		if (rightLineStart < position.x + availableWidth)
		{
			drawList->AddLine(
				{ rightLineStart, lineY },
				{ position.x + availableWidth, lineY },
				color);
		}
		drawList->AddText(
			{ position.x + lineLength + 10.0f, position.y + 2.0f },
			color,
			a_text);
		ImGui::SetCursorScreenPos(
			{ position.x, position.y + textSize.y + 8.0f });
		ImGui::Dummy({ availableWidth, 0.0f });
	}

	void DrawShell() noexcept
	{
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
			const auto footerHeight =
				ImGui::GetFrameHeightWithSpacing() +
				ImGui::GetStyle().ItemSpacing.y * 3.0f +
				Theme::kSeparatorThickness;
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
			ImGui::Spacing();
			DrawFooter(model, state);
		}
		DrawHostSettingsPopup();
		ImGui::End();

		if (!open)
		{
			(void)SetMenuVisible(false);
			SaveLayout();
		}
	}
}
