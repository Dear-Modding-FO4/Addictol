#include <DearModdingUI/Shell.h>
#include <DearModdingUI/BackgroundBlur.h>
#include <DearModdingUI/Host.h>
#include <DearModdingUI/IconGlyphs.h>
#include <DearModdingUI/Theme.h>
#include <DearModdingUI/VisualDecisions.h>

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
				a_window->Rect().Max.x -
					a_window->WindowBorderSize -
					style.FramePadding.x -
					a_fontSize -
					a_offset -
					kTitleBarButtonPadding,
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

		[[nodiscard]] bool BeginWithRoundedClose(
			const char* a_name,
			bool* a_open,
			ImGuiWindowFlags a_flags) noexcept
		{
			bool visible = false;
			{
				const NativeTitleBarButtonHighlightGuard guard;
				visible = ImGui::Begin(a_name, a_open, a_flags);
			}
			DrawRoundedCloseHighlight(ImGui::GetCurrentWindowRead());
			return visible;
		}

		[[nodiscard]] float GetCenterOffsetForContent(
			float a_contentWidth) noexcept
		{
			const auto fullWidth = ImGui::GetWindowWidth();
			const auto padding = ImGui::GetStyle().WindowPadding.x;
			const auto available = fullWidth - padding * 2.0f;
			const auto center = (available - a_contentWidth) * 0.5f;
			return (std::max)(0.0f, padding + center - ImGui::GetCursorPosX());
		}

		void DrawHeader() noexcept
		{
			const auto textScale = Theme::kHeaderFallbackTextScale;
			float textWidth = 0.0f;
			{
				const Theme::FontGuard font{ Theme::FontRole::kTitle };
				ImGui::SetWindowFontScale(textScale);
				textWidth = ImGui::CalcTextSize("Evil Modding").x;
				ImGui::SetWindowFontScale(1.0f);
			}
			const auto offset = GetCenterOffsetForContent(textWidth);
			if (offset > 0.0f)
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);

			ImGui::SetWindowFontScale(textScale);
			{
				const Theme::FontGuard font{ Theme::FontRole::kTitle };
				ImGui::TextUnformatted("Evil Modding");
			}
			ImGui::SetWindowFontScale(1.0f);
			ImGui::SeparatorEx(
				ImGuiSeparatorFlags_Horizontal,
				Theme::kSeparatorThickness);
			ImGui::Spacing();
		}

		void DrawSectionHeader(
			const char* a_text,
			const ImVec4& a_color) noexcept
		{
			auto* drawList = ImGui::GetWindowDrawList();
			const auto position = ImGui::GetCursorScreenPos();
			const auto availableWidth = ImGui::GetContentRegionAvail().x;
			const auto textSize = ImGui::CalcTextSize(a_text);
			const auto lineY = position.y + textSize.y * 0.5f;
			const auto lineLength =
				(availableWidth - textSize.x - 20.0f) * 0.5f;
			const auto color = ImGui::GetColorU32(a_color);

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

		void DrawCategoryHeader(
			const char* a_key,
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
			const auto glyph = ResolveIconGlyph(IconKind::kCategory, a_name);
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
			DrawSectionHeader(
				"Mods", Theme::kFullPalette[ImGuiCol_Text]);
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
			DrawSectionHeader(
				"Pages", Theme::kFullPalette[ImGuiCol_Text]);
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

		void DrawPageHeader(const NavigationPage& a_page) noexcept
		{
			const auto start = ImGui::GetCursorScreenPos();
			{
				const Theme::FontGuard font{ Theme::FontRole::kTitle };
				ImGui::SetWindowFontScale(Theme::kFeatureTitleScale);
				ImGui::TextUnformatted(a_page.displayName.c_str());
				ImGui::SetWindowFontScale(1.0f);
			}
			const auto titleHeight =
				ImGui::GetItemRectMax().y - start.y;

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
		}

		void SaveLayout() noexcept
		{
			const auto& io = ImGui::GetIO();
			if (io.IniFilename)
				ImGui::SaveIniSettingsToDisk(io.IniFilename);
		}
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

		const auto visible = BeginWithRoundedClose(
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
			DrawHeader();
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
		ImGui::End();

		if (!open)
		{
			(void)SetMenuVisible(false);
			SaveLayout();
		}
	}
}
