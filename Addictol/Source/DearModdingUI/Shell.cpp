// Sidebar composition adapted from Fallout 4 Community Shaders FeatureListRenderer.*, GPL-3.0.

#include <DearModdingUI/Shell.h>
#include <DearModdingUI/BackgroundBlur.h>
#include <DearModdingUI/Host.h>
#include <DearModdingUI/Theme.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string>
#include <string_view>

namespace Addictol::DearModdingUI
{
	namespace
	{
		struct ShellState
		{
			DMUI_ClientHandle activeClient{ DMUI_INVALID_CLIENT_HANDLE };
			DMUI_PageHandle activePage{ DMUI_INVALID_PAGE_HANDLE };
			std::array<char, 96> search{};
		};

		[[nodiscard]] ShellState& State() noexcept
		{
			static ShellState state;
			return state;
		}

		[[nodiscard]] size_t PageCount(const NavigationClient& a_client) noexcept
		{
			size_t count = 0;
			for (const auto& category : a_client.categories)
				count += category.pages.size();
			return count;
		}

		[[nodiscard]] DMUI_PageHandle FirstPage(
			const NavigationClient& a_client) noexcept
		{
			for (const auto& category : a_client.categories)
			{
				if (!category.pages.empty())
					return category.pages.front().handle;
			}
			return DMUI_INVALID_PAGE_HANDLE;
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
				Lower(a_page.category).contains(search) ||
				Lower(a_page.summary).contains(search);
		}

		void SelectClient(ShellState& a_state, const NavigationClient& a_client) noexcept
		{
			a_state.activeClient = a_client.handle;
			a_state.activePage = FirstPage(a_client);
			a_state.search.front() = '\0';
		}

		void DrawBrandHeader(const NavigationModel& a_model) noexcept
		{
			const auto scale = Theme::Scale();
			const auto start = ImGui::GetCursorScreenPos();
			{
				const Theme::FontGuard title{ Theme::FontRole::kTitle };
				ImGui::TextUnformatted("Dear Modding");
			}
			const auto titleEnd = ImGui::GetItemRectMax();
			const auto textColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
			const auto detail = std::to_string(a_model.clients.size()) +
				(a_model.clients.size() == 1 ? " mod" : " mods");
			const auto detailSize = ImGui::CalcTextSize(detail.c_str());
			ImGui::GetWindowDrawList()->AddText(
				{ ImGui::GetWindowContentRegionMax().x + ImGui::GetWindowPos().x - detailSize.x,
					start.y + (titleEnd.y - start.y - detailSize.y) * 0.5f },
				textColor,
				detail.c_str());
			const auto lineY = titleEnd.y + 6.0f * scale;
			auto* drawList = ImGui::GetWindowDrawList();
			drawList->AddRectFilled(
				{ start.x, lineY },
				{ start.x + 72.0f * scale, lineY + 3.0f * scale },
				ImGui::GetColorU32(Theme::colors::kAccent),
				2.0f * scale);
			drawList->AddRectFilled(
				{ start.x + 78.0f * scale, lineY + scale },
				{ ImGui::GetWindowContentRegionMax().x + ImGui::GetWindowPos().x,
					lineY + 2.0f * scale },
				ImGui::GetColorU32(ImGuiCol_Separator));
			ImGui::Dummy({ 0.0f, 14.0f * scale });
		}

		void DrawClientSelector(
			const NavigationModel& a_model,
			ShellState& a_state) noexcept
		{
			const auto* active = a_model.FindClient(a_state.activeClient);
			if (!active)
				return;
			ImGui::TextDisabled("Installed mods");
			if (a_model.clients.size() == 1)
			{
				const Theme::FontGuard heading{ Theme::FontRole::kHeading };
				ImGui::TextUnformatted(active->displayName.c_str());
			}
			else
			{
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::BeginCombo("##DearModdingClient", active->displayName.c_str()))
				{
					for (const auto& client : a_model.clients)
					{
						const auto selected = client.handle == active->handle;
						if (ImGui::Selectable(client.displayName.c_str(), selected))
							SelectClient(a_state, client);
						if (selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
			}
			const auto major = active->version >> 16;
			const auto minor = active->version & 0xFFFFu;
			char version[32]{};
			std::snprintf(version, sizeof(version), "Version %u.%u", major, minor);
			{
				const Theme::FontGuard subtext{ Theme::FontRole::kSubtext };
				ImGui::TextColored(Theme::colors::kMuted, "%s", version);
			}
		}

		void DrawNavigation(
			const NavigationModel& a_model,
			ShellState& a_state) noexcept
		{
			ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::colors::kSidebar);
			if (!ImGui::BeginChild("##DearModdingNavigation", {}, ImGuiChildFlags_None))
			{
				ImGui::EndChild();
				ImGui::PopStyleColor();
				return;
			}

			DrawClientSelector(a_model, a_state);
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			const auto* client = a_model.FindClient(a_state.activeClient);
			if (client && PageCount(*client) > 8)
			{
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::InputTextWithHint(
					"##DearModdingSearch",
					"Search settings",
					a_state.search.data(),
					a_state.search.size());
				ImGui::Spacing();
			}

			if (client)
			{
				for (const auto& category : client->categories)
				{
					const auto hasMatch = std::ranges::any_of(category.pages, [&](const auto& a_page) {
						return Matches(a_page, a_state.search.data());
					});
					if (!hasMatch)
						continue;
					ImGui::Spacing();
					{
						const Theme::FontGuard subtext{ Theme::FontRole::kSubtext };
						ImGui::TextColored(Theme::colors::kMuted, "%s", category.displayName.c_str());
					}
					for (const auto& page : category.pages)
					{
						if (!Matches(page, a_state.search.data()))
							continue;
						const auto failed = PageFailed(page.handle);
						const auto selected = page.handle == a_state.activePage;
						const auto label = (failed ? "!  " : "") +
							page.displayName +
							"###DearModdingPage/" +
							page.id;
						if (failed)
							ImGui::PushStyleColor(ImGuiCol_Text, Theme::colors::kError);
						if (ImGui::Selectable(
								label.c_str(),
								selected,
								ImGuiSelectableFlags_None,
								{ 0.0f, ImGui::GetFrameHeight() * 1.15f }))
							a_state.activePage = page.handle;
						if (failed)
							ImGui::PopStyleColor();
					}
				}
			}

			ImGui::EndChild();
			ImGui::PopStyleColor();
		}

		void DrawFailure(const NavigationPage& a_page) noexcept
		{
			{
				const Theme::FontGuard heading{ Theme::FontRole::kHeading };
				ImGui::TextColored(
					Theme::colors::kError,
					"%s could not be displayed",
					a_page.displayName.c_str());
			}
			ImGui::Spacing();
			ImGui::TextWrapped(
				"The mod's page callback failed and has been disabled for this session. "
				"Other pages remain available.");
		}

		void DrawContent(const NavigationModel& a_model, ShellState& a_state) noexcept
		{
			ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::colors::kContent);
			if (!ImGui::BeginChild("##DearModdingContent", {}, ImGuiChildFlags_None))
			{
				ImGui::EndChild();
				ImGui::PopStyleColor();
				return;
			}

			const auto* page = a_model.FindPage(a_state.activePage);
			if (!page)
			{
				ImGui::TextDisabled("No settings pages are available.");
				ImGui::EndChild();
				ImGui::PopStyleColor();
				return;
			}

			{
				const Theme::FontGuard title{ Theme::FontRole::kTitle };
				ImGui::TextUnformatted(page->displayName.c_str());
			}
			const auto* client = a_model.FindClientForPage(page->handle);
			const auto context = client ?
				client->displayName + "  /  " + page->category :
				page->category;
			{
				const Theme::FontGuard subtext{ Theme::FontRole::kSubtext };
				ImGui::TextColored(Theme::colors::kMuted, "%s", context.c_str());
				if (!page->summary.empty())
				{
					ImGui::Spacing();
					ImGui::TextWrapped("%s", page->summary.c_str());
				}
			}
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			const auto presentation = DecidePagePresentation(page, PageFailed(page->handle));
			if (presentation == PagePresentation::kFailure)
				DrawFailure(*page);
			else if (presentation == PagePresentation::kContent)
			{
				ImGui::PushID(static_cast<int>(page->handle));
				if (!DrawPage(page->handle))
				{
					ImGui::Spacing();
					ImGui::Separator();
					ImGui::Spacing();
					DrawFailure(*page);
				}
				ImGui::PopID();
			}

			ImGui::EndChild();
			ImGui::PopStyleColor();
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
		const auto& model = Navigation();
		auto& state = State();
		const auto requested = SelectedPage();
		state.activePage = ResolvePageSelection(model, requested, state.activePage);
		if (requested != DMUI_INVALID_PAGE_HANDLE)
			ClearPageSelection(requested);
		if (const auto* client = model.FindClientForPage(state.activePage))
			state.activeClient = client->handle;

		const auto scale = Theme::Scale();
		const auto* viewport = ImGui::GetMainViewport();
		ImGui::DockSpaceOverViewport(
			0,
			viewport,
			ImGuiDockNodeFlags_PassthruCentralNode);
		const auto maximumSize = ImVec2(
			(std::max)(320.0f, viewport->WorkSize.x * 0.96f),
			(std::max)(280.0f, viewport->WorkSize.y * 0.96f));
		const auto minimumSize = ImVec2(
			(std::min)(680.0f * scale, maximumSize.x),
			(std::min)(460.0f * scale, maximumSize.y));
		const auto defaultSize = ImVec2(
			std::clamp(
				viewport->WorkSize.x * 0.82f,
				minimumSize.x,
				(std::min)(1240.0f * scale, maximumSize.x)),
			std::clamp(
				viewport->WorkSize.y * 0.80f,
				minimumSize.y,
				(std::min)(860.0f * scale, maximumSize.y)));
		ImGui::SetNextWindowSize(defaultSize, ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_FirstUseEver, { 0.5f, 0.5f });
		ImGui::SetNextWindowSizeConstraints(minimumSize, maximumSize);
		ImGuiWindowClass windowClass{};
		windowClass.ClassId = ImHashStr("DearModdingUI.Host");
		windowClass.DockingAllowUnclassed = true;
		ImGui::SetNextWindowClass(&windowClass);

		auto open = true;
		const auto visible = ImGui::Begin(
			"Dear Modding###DearModdingUI.Host",
			&open,
			ImGuiWindowFlags_None);
		const auto position = ImGui::GetWindowPos();
		const auto size = ImGui::GetWindowSize();
		const auto framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
		BackgroundBlur::SetHostWindow(
			(position.x - viewport->Pos.x) * framebufferScale.x,
			(position.y - viewport->Pos.y) * framebufferScale.y,
			(position.x + size.x - viewport->Pos.x) * framebufferScale.x,
			(position.y + size.y - viewport->Pos.y) * framebufferScale.y,
			ImGui::GetStyle().WindowRounding *
				(std::max)(framebufferScale.x, framebufferScale.y));

		if (visible)
		{
			DrawBrandHeader(model);
			if (ImGui::BeginTable(
					"##DearModdingLayout",
					2,
					ImGuiTableFlags_Resizable |
						ImGuiTableFlags_SizingStretchProp |
						ImGuiTableFlags_BordersInnerV))
			{
				ImGui::TableSetupColumn(
					"Navigation",
					ImGuiTableColumnFlags_WidthStretch,
					2.6f);
				ImGui::TableSetupColumn(
					"Settings",
					ImGuiTableColumnFlags_WidthStretch,
					7.4f);
				ImGui::TableNextColumn();
				DrawNavigation(model, state);
				ImGui::TableNextColumn();
				DrawContent(model, state);
				ImGui::EndTable();
			}
		}
		ImGui::End();

		if (!open)
		{
			(void)SetMenuVisible(false);
			SaveLayout();
		}
	}
}
