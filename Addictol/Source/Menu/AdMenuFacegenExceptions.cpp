#include <Menu/AdMenuFacegenExceptions.h>

#include <Core/Settings/AdSettings.h>
#include <DearModdingUI/IconGlyphs.h>
#include <Menu/AdMenu.h>
#include <Menu/AdMenuWidgets.h>
#include <Modules/AdFacegenExceptions.h>

#include <DearModdingUI/ImGuiForward.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstring>

namespace Addictol::Menu
{
	namespace
	{
		struct FacegenExceptionsPageState
		{
			bool initialized{ false };
			bool dirty{ false };
			uint64_t sourceRevision{ 0 };
			std::vector<FacegenExceptionDraft> entries;
			std::optional<size_t> editingIndex;
			FacegenExceptionDraft editor;
			std::string operationError;
		};

		FacegenExceptionsPageState g_pageState;

		[[nodiscard]] std::string_view StatusLabel(FacegenExceptionStatus a_status) noexcept
		{
			switch (a_status)
			{
			case FacegenExceptionStatus::kResolved:
				return "Resolved"sv;
			case FacegenExceptionStatus::kPluginNotFound:
				return "Plugin not loaded"sv;
			case FacegenExceptionStatus::kMissingPluginName:
				return "Missing plugin name"sv;
			case FacegenExceptionStatus::kFatalError:
				return "Fatal resolution error"sv;
			case FacegenExceptionStatus::kEmptyValue:
				return "Empty value"sv;
			case FacegenExceptionStatus::kMalformedFormID:
				return "Malformed FormID"sv;
			case FacegenExceptionStatus::kDataNotReady:
				return "Game data not ready"sv;
			}
			return "Unknown"sv;
		}

		void DrawStatus(FacegenExceptionStatus a_status) noexcept
		{
			const auto label = StatusLabel(a_status);
			if (a_status == FacegenExceptionStatus::kResolved)
				MenuUi::MonoCell(label);
			else if (a_status == FacegenExceptionStatus::kEmptyValue)
				MenuUi::Warn(label);
			else
				MenuUi::Error(label);
		}

		[[nodiscard]] float FormIDColumnWidth(std::string_view a_heading) noexcept
		{
			const auto style = MenuUi::StyleMetrics();
			if (!style)
				return 0.0f;
			const auto valueWidth = ImGui::CalcTextSize("0x00000000").x;
			const auto headingWidth =
				ImGui::CalcTextSize(
					a_heading.data(),
					a_heading.data() + a_heading.size()).x;
			return (std::max)(valueWidth, headingWidth) +
				style->cellPadding.x * 2.0f;
		}

		[[nodiscard]] float ActionsColumnWidth() noexcept
		{
			const auto style = MenuUi::StyleMetrics();
			if (!style)
				return 0.0f;
			return ImGui::CalcTextSize("Edit").x +
				ImGui::CalcTextSize("Remove").x +
				style->framePadding.x * 4.0f +
				style->itemSpacing.x +
				style->cellPadding.x * 2.0f;
		}

		[[nodiscard]] FacegenExceptionDraft DraftFromRecord(
			const FacegenExceptionRecord& a_record)
		{
			const auto parsed = ParseFacegenExceptionValue(a_record.rawValue);
			return { a_record.key, parsed.formID, parsed.pluginName };
		}

		void RefreshDraft(const FacegenExceptionSnapshot& a_snapshot)
		{
			g_pageState.entries.clear();
			g_pageState.entries.reserve(a_snapshot.entries.size());
			for (const auto& entry : a_snapshot.entries)
				g_pageState.entries.push_back(DraftFromRecord(entry));
			g_pageState.initialized = true;
			g_pageState.dirty = false;
			g_pageState.sourceRevision = a_snapshot.revision;
			g_pageState.editingIndex.reset();
			g_pageState.editor = {};
			g_pageState.operationError.clear();
		}

		void EnsureDraft(const FacegenExceptionSnapshot& a_snapshot)
		{
			if (!g_pageState.initialized ||
				(!g_pageState.dirty &&
					g_pageState.sourceRevision != a_snapshot.revision))
				RefreshDraft(a_snapshot);
		}

		void BeginAdd()
		{
			g_pageState.editingIndex = g_pageState.entries.size();
			g_pageState.editor = {};
			g_pageState.operationError.clear();
		}

		void BeginEdit(size_t a_index)
		{
			g_pageState.editingIndex = a_index;
			g_pageState.editor = g_pageState.entries[a_index];
			if (g_pageState.editor.pluginName &&
				g_pageState.editor.pluginName->empty())
				g_pageState.editor.pluginName.reset();
			g_pageState.operationError.clear();
		}

		void RemoveEntry(size_t a_index)
		{
			g_pageState.entries.erase(g_pageState.entries.begin() + a_index);
			g_pageState.dirty = true;
			if (!g_pageState.editingIndex)
				return;
			if (*g_pageState.editingIndex == a_index)
			{
				g_pageState.editingIndex.reset();
				g_pageState.editor = {};
			}
			else if (*g_pageState.editingIndex > a_index)
				--*g_pageState.editingIndex;
		}

		void DrawEditor()
		{
			if (!g_pageState.editingIndex)
				return;

			const auto adding =
				*g_pageState.editingIndex == g_pageState.entries.size();
			MenuUi::Heading(adding ? "Add exception"sv : "Edit exception"sv);

			std::array<char, 256> keyBuffer{};
			std::array<char, 64> formIDBuffer{};
			std::array<char, 256> pluginBuffer{};
			strncpy_s(
				keyBuffer.data(),
				keyBuffer.size(),
				g_pageState.editor.key.c_str(),
				_TRUNCATE);
			strncpy_s(
				formIDBuffer.data(),
				formIDBuffer.size(),
				g_pageState.editor.formID.c_str(),
				_TRUNCATE);
			const auto pluginName =
				g_pageState.editor.pluginName.value_or("");
			strncpy_s(
				pluginBuffer.data(),
				pluginBuffer.size(),
				pluginName.c_str(),
				_TRUNCATE);

			ImGui::TextUnformatted("Unique name");
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::InputText(
					"##facegen_exception_key",
					keyBuffer.data(),
					keyBuffer.size()))
				g_pageState.editor.key = keyBuffer.data();
			ImGui::TextUnformatted("FormID");
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::InputText(
					"##facegen_exception_formid",
					formIDBuffer.data(),
					formIDBuffer.size()))
				g_pageState.editor.formID = formIDBuffer.data();
			ImGui::TextUnformatted("Plugin name (optional)");
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::InputText(
					"##facegen_exception_plugin",
					pluginBuffer.data(),
					pluginBuffer.size()))
			{
				if (pluginBuffer[0])
					g_pageState.editor.pluginName = pluginBuffer.data();
				else
					g_pageState.editor.pluginName.reset();
			}

			const auto ignoredIndex =
				adding ? std::optional<size_t>{} : g_pageState.editingIndex;
			const auto validation = ValidateFacegenException(
				g_pageState.editor,
				g_pageState.entries,
				ignoredIndex);
			if (validation.valid)
			{
				MenuUi::LabeledState("Validation:", true, "Valid");
				MenuUi::LabeledValue(
					"Resolved runtime FormID:",
					MenuUi::Print("0x%08X", *validation.resolvedFormID));
			}
			else
				MenuUi::Error(validation.message);

			ImGui::BeginDisabled(!validation.valid);
			if (ImGui::Button(adding ? "Add entry" : "Update entry"))
			{
				auto entry = g_pageState.editor;
				TrimFacegenExceptionField(entry.key);
				TrimFacegenExceptionField(entry.formID);
				if (entry.pluginName)
				{
					TrimFacegenExceptionField(*entry.pluginName);
					if (entry.pluginName->empty())
						entry.pluginName.reset();
				}
				if (adding)
					g_pageState.entries.push_back(std::move(entry));
				else
					g_pageState.entries[*g_pageState.editingIndex] =
						std::move(entry);
				g_pageState.dirty = true;
				g_pageState.editingIndex.reset();
				g_pageState.editor = {};
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				g_pageState.editingIndex.reset();
				g_pageState.editor = {};
			}
			ImGui::Spacing();
		}

		void DrawEditActions(const FacegenExceptionSnapshot& a_snapshot)
		{
			if (ImGui::Button("Add exception"))
				BeginAdd();
			ImGui::SameLine();
			ImGui::BeginDisabled(!g_pageState.dirty);
			if (ImGui::Button("Save changes"))
			{
				const auto result =
					SaveFacegenExceptions(g_pageState.entries);
				if (result.success)
				{
					RefreshDraft(GetFacegenExceptionSnapshot());
					Menu::ReportStatus(
						DMUI_STATUS_SEVERITY_SUCCESS,
						"Facegen exceptions saved.");
				}
				else
				{
					g_pageState.operationError = result.error;
					Menu::ReportStatus(
						DMUI_STATUS_SEVERITY_ERROR,
						result.error.c_str());
				}
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(!g_pageState.dirty);
			if (ImGui::Button("Discard changes"))
				RefreshDraft(a_snapshot);
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(g_pageState.dirty);
			if (ImGui::Button("Reload from file"))
			{
				const auto result = ReloadFacegenExceptions();
				RefreshDraft(GetFacegenExceptionSnapshot());
				if (result.success)
				{
					Menu::ReportStatus(
						DMUI_STATUS_SEVERITY_SUCCESS,
						"Facegen exceptions reloaded.");
				}
				else
				{
					g_pageState.operationError = result.error;
					Menu::ReportStatus(
						DMUI_STATUS_SEVERITY_ERROR,
						result.error.c_str());
				}
			}
			ImGui::EndDisabled();

			if (g_pageState.dirty)
				MenuUi::Warn("Unsaved changes. Save or discard before reloading from file.");
			if (!g_pageState.operationError.empty())
				MenuUi::Error(g_pageState.operationError);
			MenuUi::Muted(
				"Saved changes affect NPCs processed afterward. NPCs already using preprocessed head data must be reloaded.");
		}

		void DrawOverview(
			const FacegenExceptionSnapshot& a_snapshot,
			bool a_facegenEnabled) noexcept
		{
			const auto resolvedCount = static_cast<size_t>(std::count_if(
				a_snapshot.entries.begin(),
				a_snapshot.entries.end(),
				[](const auto& a_entry) {
					return a_entry.status == FacegenExceptionStatus::kResolved;
				}));
			const auto failureCount = a_snapshot.entries.size() - resolvedCount;
			const auto effectiveCount =
				a_facegenEnabled ? a_snapshot.effectiveExceptionCount : 0;

			MenuUi::Heading("Status");
			MenuUi::LabeledState(
				"Facegen module:",
				a_facegenEnabled,
				a_facegenEnabled ? "Enabled"sv : "Disabled"sv);
			MenuUi::LabeledValue(
				"Exceptions in effect:",
				MenuUi::FormatCount(effectiveCount));
			MenuUi::LabeledValue(
				"Configured coverage:",
				MenuUi::Print(
					"%llu built-in, %llu resolved user-defined",
					static_cast<unsigned long long>(kFacegenPrimaryExceptions.size()),
					static_cast<unsigned long long>(resolvedCount)));
			MenuUi::LabeledValue(
				"Debug output ([Additional] bDbgFacegenOutput):",
				bAdditionalDbgFacegenOutput.GetValue() ? "Enabled"sv : "Disabled"sv);

			if (!a_facegenEnabled)
			{
				MenuUi::Warn(
					"Exceptions are inactive because [Patches] bFacegen is disabled. Enable it and restart the game.");
			}
			if (failureCount != 0)
			{
				MenuUi::Error(MenuUi::Print(
					"%llu user-defined entr%s failed and %s not in effect.",
					static_cast<unsigned long long>(failureCount),
					failureCount == 1 ? "y" : "ies",
					failureCount == 1 ? "is" : "are"));
			}
			else if (!bAdditionalDbgFacegenOutput.GetValue())
			{
				MenuUi::Muted(
					"Enable bDbgFacegenOutput for NPC facegen presence messages in the console and log.");
			}
		}

		void DrawConfigurationState(
			const FacegenExceptionSnapshot& a_snapshot,
			bool a_facegenEnabled) noexcept
		{
			MenuUi::Heading("Configuration");
			MenuUi::LabeledValue("File:", kFacegenExceptionsPath);
			if (!a_snapshot.readAttempted)
			{
				MenuUi::LabeledValue(
					"INI state:",
					a_facegenEnabled ? "Not read yet"sv : "Not read while module is disabled"sv);
				MenuUi::LabeledValue("Section [FacegenException]:", "Not checked");
				return;
			}

			MenuUi::LabeledState(
				"INI state:",
				a_snapshot.iniFound,
				a_snapshot.iniFound ? "Found"sv : "Missing"sv);
			if (a_snapshot.iniFound)
			{
				MenuUi::LabeledState(
					"Section [FacegenException]:",
					a_snapshot.sectionFound,
					a_snapshot.sectionFound ? "Found"sv : "Missing"sv);
			}
			else
				MenuUi::LabeledValue("Section [FacegenException]:", "Not checked");
		}

		void DrawPrimaryExceptions() noexcept
		{
			MenuUi::Heading("Built-in primary exceptions");
			MenuUi::Muted("These six exceptions are configured without user INI entries.");
			if (!ImGui::BeginTable(
					"##facegen_primary_exceptions",
					2,
					MenuUi::kTableFlags & ~ImGuiTableFlags_ScrollY))
				return;

			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 3.0f);
			ImGui::TableSetupColumn(
				"FormID",
				ImGuiTableColumnFlags_WidthFixed,
				FormIDColumnWidth("FormID"));
			ImGui::TableHeadersRow();
			for (const auto& exception : kFacegenPrimaryExceptions)
			{
				ImGui::TableNextRow();
				(void)ImGui::TableSetColumnIndex(0);
				MenuUi::MonoCell(exception.name);
				(void)ImGui::TableSetColumnIndex(1);
				MenuUi::MonoCell(MenuUi::Print("0x%08X", exception.formID));
			}
			ImGui::EndTable();
		}

		void DrawEmptyEntryState(const FacegenExceptionSnapshot& a_snapshot) noexcept
		{
			if (!a_snapshot.readAttempted)
			{
				MenuUi::Muted("User-defined exceptions will appear after the module reads the INI.");
			}
			else if (!a_snapshot.iniFound)
			{
				MenuUi::Error("The exceptions INI was not found.");
				MenuUi::Muted(
					"Create the file at the path above and add a [FacegenException] section.");
			}
			else if (!a_snapshot.sectionFound)
			{
				MenuUi::Error("The INI does not contain a [FacegenException] section.");
				MenuUi::Muted("Add the section, then use UniqueName=FormID or UniqueName=FormID:PluginName.");
			}
			else
			{
				MenuUi::Muted("No user-defined facegen exceptions are configured.");
				MenuUi::Muted("Use UniqueName=FormID or UniqueName=FormID:PluginName.");
			}
		}

		void DrawUserExceptions(const FacegenExceptionSnapshot& a_snapshot)
		{
			MenuUi::Heading("User-defined exceptions");
			DrawEditActions(a_snapshot);
			DrawEditor();
			if (g_pageState.entries.empty())
			{
				DrawEmptyEntryState(a_snapshot);
				return;
			}

			const auto minimumHeight =
				ImGui::GetTextLineHeightWithSpacing() * 6.0f;
			const auto height =
				(std::max)(ImGui::GetContentRegionAvail().y, minimumHeight);
			if (!ImGui::BeginTable(
					"##facegen_user_exceptions",
					6,
					MenuUi::kTableFlags,
					{ 0.0f, height }))
				return;

			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 1.5f);
			ImGui::TableSetupColumn("FormID", ImGuiTableColumnFlags_WidthStretch, 1.2f);
			ImGui::TableSetupColumn("Plugin", ImGuiTableColumnFlags_WidthStretch, 1.5f);
			ImGui::TableSetupColumn(
				"Resolved FormID",
				ImGuiTableColumnFlags_WidthFixed,
				FormIDColumnWidth("Resolved FormID"));
			ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 1.2f);
			ImGui::TableSetupColumn(
				"Actions",
				ImGuiTableColumnFlags_WidthFixed,
				ActionsColumnWidth());
			ImGui::TableHeadersRow();

			for (size_t index = 0; index < g_pageState.entries.size(); ++index)
			{
				const auto& entry = g_pageState.entries[index];
				const auto validation =
					ValidateFacegenException(entry, g_pageState.entries, index);
				ImGui::PushID(static_cast<int>(index));
				ImGui::TableNextRow();
				(void)ImGui::TableSetColumnIndex(0);
				MenuUi::MonoCell(entry.key);
				(void)ImGui::TableSetColumnIndex(1);
				MenuUi::MonoCell(entry.formID);
				(void)ImGui::TableSetColumnIndex(2);
				if (entry.pluginName && !entry.pluginName->empty())
					MenuUi::MonoCell(*entry.pluginName);
				else
					MenuUi::Muted("-");
				(void)ImGui::TableSetColumnIndex(3);
				if (validation.resolvedFormID)
					MenuUi::MonoCell(MenuUi::Print(
						"0x%08X",
						*validation.resolvedFormID));
				else
					MenuUi::Muted("-");
				(void)ImGui::TableSetColumnIndex(4);
				DrawStatus(validation.status);
				(void)ImGui::TableSetColumnIndex(5);
				if (ImGui::Button("Edit"))
					BeginEdit(index);
				ImGui::SameLine();
				if (ImGui::Button("Remove"))
				{
					RemoveEntry(index);
					ImGui::PopID();
					break;
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
	}

	void DrawFacegenExceptionsPage([[maybe_unused]] void* a_userData) noexcept
	{
		const auto snapshot = GetFacegenExceptionSnapshot();
		const auto facegenEnabled = bPatchesFacegen.GetValue();
		EnsureDraft(snapshot);

		(void)Client().DrawSectionHeader(
			"Facegen Exceptions",
			DearModdingUI::PhosphorGlyph::kFiles);
		DrawOverview(snapshot, facegenEnabled);
		ImGui::Spacing();
		DrawConfigurationState(snapshot, facegenEnabled);
		ImGui::Spacing();
		DrawPrimaryExceptions();
		ImGui::Spacing();
		DrawUserExceptions(snapshot);
	}
}
