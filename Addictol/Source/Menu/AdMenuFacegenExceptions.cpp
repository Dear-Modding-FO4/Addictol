#include <Menu/AdMenuFacegenExceptions.h>

#include <Core/Settings/AdSettings.h>
#include <DearModdingUI/IconGlyphs.h>
#include <Menu/AdMenu.h>
#include <Menu/AdMenuFormatting.h>
#include <Modules/AdFacegenExceptions.h>

#include <DearModdingUI/UI.h>

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
				ReportPresentationResult(dmui::DrawStyledText(Client(), label, kBodyText));
			else if (a_status == FacegenExceptionStatus::kEmptyValue)
				ReportPresentationResult(dmui::DrawStyledText(
					Client(), label, { .tone = dmui::TextTone::kWarning }));
			else
				ReportPresentationResult(dmui::DrawStyledText(
					Client(), label, { .tone = dmui::TextTone::kError }));
		}

		[[nodiscard]] float FormIDColumnWidth(std::string_view a_heading) noexcept
		{
			const auto style = StyleMetrics();
			if (!style)
				return 0.0f;
			const auto valueWidth = dmui::ui::CalcTextSize("0x00000000").x;
			const auto headingWidth =
				dmui::ui::CalcTextSize(
					a_heading.data(),
					a_heading.data() + a_heading.size()).x;
			return (std::max)(valueWidth, headingWidth) +
				style->cellPadding.x * 2.0f;
		}

		[[nodiscard]] float ActionsColumnWidth() noexcept
		{
			const auto style = StyleMetrics();
			if (!style)
				return 0.0f;
			return dmui::ui::CalcTextSize("Edit").x +
				dmui::ui::CalcTextSize("Remove").x +
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
			ReportPresentationResult(dmui::DrawStyledText(
				Client(), adding ? "Add exception"sv : "Edit exception"sv, kHeadingText));

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

			dmui::ui::TextUnformatted("Unique name");
			dmui::ui::SetNextItemWidth(-FLT_MIN);
			if (dmui::ui::InputText(
					"##facegen_exception_key",
					keyBuffer.data(),
					keyBuffer.size()))
				g_pageState.editor.key = keyBuffer.data();
			dmui::ui::TextUnformatted("FormID");
			dmui::ui::SetNextItemWidth(-FLT_MIN);
			if (dmui::ui::InputText(
					"##facegen_exception_formid",
					formIDBuffer.data(),
					formIDBuffer.size()))
				g_pageState.editor.formID = formIDBuffer.data();
			dmui::ui::TextUnformatted("Plugin name (optional)");
			dmui::ui::SetNextItemWidth(-FLT_MIN);
			if (dmui::ui::InputText(
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
				ReportPresentationResult(dmui::DrawLabeledValue(Client(), "Validation:", "Valid", {
					.valueStyle = {
						.fontRole = DMUI_FONT_ROLE_HEADING,
						.tone = dmui::TextTone::kSuccess
					}
				}));
				ReportPresentationResult(dmui::DrawLabeledValue(
					Client(), "Resolved runtime FormID:",
					MenuUi::Print("0x%08X", *validation.resolvedFormID),
					{ .valueStyle = kBodyText }));
			}
			else
				ReportPresentationResult(dmui::DrawStyledText(
					Client(), validation.message, { .tone = dmui::TextTone::kError }));

			dmui::ui::BeginDisabled(!validation.valid);
			if (dmui::ui::Button(adding ? "Add entry" : "Update entry"))
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
			dmui::ui::EndDisabled();
			dmui::ui::SameLine();
			if (dmui::ui::Button("Cancel"))
			{
				g_pageState.editingIndex.reset();
				g_pageState.editor = {};
			}
			dmui::ui::Spacing();
		}

		void DrawEditActions(const FacegenExceptionSnapshot& a_snapshot)
		{
			if (dmui::ui::Button("Add exception"))
				BeginAdd();
			dmui::ui::SameLine();
			dmui::ui::BeginDisabled(!g_pageState.dirty);
			if (dmui::ui::Button("Save changes"))
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
			dmui::ui::EndDisabled();
			dmui::ui::SameLine();
			dmui::ui::BeginDisabled(!g_pageState.dirty);
			if (dmui::ui::Button("Discard changes"))
				RefreshDraft(a_snapshot);
			dmui::ui::EndDisabled();
			dmui::ui::SameLine();
			dmui::ui::BeginDisabled(g_pageState.dirty);
			if (dmui::ui::Button("Reload from file"))
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
			dmui::ui::EndDisabled();

			if (g_pageState.dirty)
				ReportPresentationResult(dmui::DrawStyledText(
					Client(), "Unsaved changes. Save or discard before reloading from file.",
					{ .tone = dmui::TextTone::kWarning }));
			if (!g_pageState.operationError.empty())
				ReportPresentationResult(dmui::DrawStyledText(
					Client(), g_pageState.operationError, { .tone = dmui::TextTone::kError }));
			ReportPresentationResult(dmui::DrawStyledText(
				Client(), "Saved changes affect NPCs processed afterward. NPCs already using preprocessed head data must be reloaded.",
				kMutedText));
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

			ReportPresentationResult(dmui::DrawStyledText(Client(), "Status", kHeadingText));
			ReportPresentationResult(dmui::DrawLabeledValue(
				Client(), "Facegen module:", a_facegenEnabled ? "Enabled"sv : "Disabled"sv, {
					.valueStyle = {
						.fontRole = DMUI_FONT_ROLE_HEADING,
						.tone = a_facegenEnabled ? dmui::TextTone::kSuccess : dmui::TextTone::kWarning
					}
				}));
			ReportPresentationResult(dmui::DrawLabeledValue(
				Client(), "Exceptions in effect:", MenuUi::FormatCount(effectiveCount),
				{ .valueStyle = kBodyText }));
			ReportPresentationResult(dmui::DrawLabeledValue(
				Client(), "Configured coverage:",
				MenuUi::Print(
					"%llu built-in, %llu resolved user-defined",
					static_cast<unsigned long long>(kFacegenPrimaryExceptions.size()),
					static_cast<unsigned long long>(resolvedCount)),
				{ .valueStyle = kBodyText }));
			ReportPresentationResult(dmui::DrawLabeledValue(
				Client(), "Debug output ([Additional] bDbgFacegenOutput):",
				bAdditionalDbgFacegenOutput.GetValue() ? "Enabled"sv : "Disabled"sv,
				{ .valueStyle = kBodyText }));

			if (!a_facegenEnabled)
			{
				ReportPresentationResult(dmui::DrawStyledText(
					Client(), "Exceptions are inactive because [Patches] bFacegen is disabled. Enable it and restart the game.",
					{ .tone = dmui::TextTone::kWarning }));
			}
			if (failureCount != 0)
			{
				ReportPresentationResult(dmui::DrawStyledText(Client(), MenuUi::Print(
					"%llu user-defined entr%s failed and %s not in effect.",
					static_cast<unsigned long long>(failureCount),
					failureCount == 1 ? "y" : "ies",
					failureCount == 1 ? "is" : "are"), { .tone = dmui::TextTone::kError }));
			}
			else if (!bAdditionalDbgFacegenOutput.GetValue())
			{
				ReportPresentationResult(dmui::DrawStyledText(
					Client(), "Enable bDbgFacegenOutput for NPC facegen presence messages in the console and log.",
					kMutedText));
			}
		}

		void DrawConfigurationState(
			const FacegenExceptionSnapshot& a_snapshot,
			bool a_facegenEnabled) noexcept
		{
			ReportPresentationResult(dmui::DrawStyledText(Client(), "Configuration", kHeadingText));
			ReportPresentationResult(dmui::DrawLabeledValue(
				Client(), "File:", kFacegenExceptionsPath, { .valueStyle = kBodyText }));
			if (!a_snapshot.readAttempted)
			{
				ReportPresentationResult(dmui::DrawLabeledValue(
					Client(), "INI state:",
					a_facegenEnabled ? "Not read yet"sv : "Not read while module is disabled"sv,
					{ .valueStyle = kBodyText }));
				ReportPresentationResult(dmui::DrawLabeledValue(
					Client(), "Section [FacegenException]:", "Not checked", { .valueStyle = kBodyText }));
				return;
			}

			ReportPresentationResult(dmui::DrawLabeledValue(
				Client(), "INI state:", a_snapshot.iniFound ? "Found"sv : "Missing"sv, {
					.valueStyle = {
						.fontRole = DMUI_FONT_ROLE_HEADING,
						.tone = a_snapshot.iniFound ? dmui::TextTone::kSuccess : dmui::TextTone::kWarning
					}
				}));
			if (a_snapshot.iniFound)
			{
				ReportPresentationResult(dmui::DrawLabeledValue(
					Client(), "Section [FacegenException]:",
					a_snapshot.sectionFound ? "Found"sv : "Missing"sv, {
						.valueStyle = {
							.fontRole = DMUI_FONT_ROLE_HEADING,
							.tone = a_snapshot.sectionFound ? dmui::TextTone::kSuccess : dmui::TextTone::kWarning
						}
					}));
			}
			else
				ReportPresentationResult(dmui::DrawLabeledValue(
					Client(), "Section [FacegenException]:", "Not checked", { .valueStyle = kBodyText }));
		}

		void DrawPrimaryExceptions() noexcept
		{
			ReportPresentationResult(dmui::DrawStyledText(Client(), "Built-in primary exceptions", kHeadingText));
			ReportPresentationResult(dmui::DrawStyledText(
				Client(), "These six exceptions are configured without user INI entries.", kMutedText));
			if (!dmui::ui::BeginTable(
					"##facegen_primary_exceptions",
					2,
					kStaticDiagnosticTableFlags))
				return;

			dmui::ui::TableSetupColumn("Name", dmui::ui::TableColumnFlags::kWidthStretch, 3.0f);
			dmui::ui::TableSetupColumn(
				"FormID",
				dmui::ui::TableColumnFlags::kWidthFixed,
				FormIDColumnWidth("FormID"));
			dmui::ui::TableHeadersRow();
			for (const auto& exception : kFacegenPrimaryExceptions)
			{
				dmui::ui::TableNextRow();
				(void)dmui::ui::TableSetColumnIndex(0);
				ReportPresentationResult(dmui::DrawStyledText(Client(), exception.name, kBodyText));
				(void)dmui::ui::TableSetColumnIndex(1);
				ReportPresentationResult(dmui::DrawStyledText(
					Client(), MenuUi::Print("0x%08X", exception.formID), kBodyText));
			}
			dmui::ui::EndTable();
		}

		void DrawEmptyEntryState(const FacegenExceptionSnapshot& a_snapshot) noexcept
		{
			if (!a_snapshot.readAttempted)
			{
				ReportPresentationResult(dmui::DrawStyledText(
					Client(), "User-defined exceptions will appear after the module reads the INI.", kMutedText));
			}
			else if (!a_snapshot.iniFound)
			{
				ReportPresentationResult(dmui::DrawStyledText(
					Client(), "The exceptions INI was not found.", { .tone = dmui::TextTone::kError }));
				ReportPresentationResult(dmui::DrawStyledText(
					Client(), "Create the file at the path above and add a [FacegenException] section.", kMutedText));
			}
			else if (!a_snapshot.sectionFound)
			{
				ReportPresentationResult(dmui::DrawStyledText(
					Client(), "The INI does not contain a [FacegenException] section.",
					{ .tone = dmui::TextTone::kError }));
				ReportPresentationResult(dmui::DrawStyledText(
					Client(), "Add the section, then use UniqueName=FormID or UniqueName=FormID:PluginName.", kMutedText));
			}
			else
			{
				ReportPresentationResult(dmui::DrawStyledText(
					Client(), "No user-defined facegen exceptions are configured.", kMutedText));
				ReportPresentationResult(dmui::DrawStyledText(
					Client(), "Use UniqueName=FormID or UniqueName=FormID:PluginName.", kMutedText));
			}
		}

		void DrawUserExceptions(const FacegenExceptionSnapshot& a_snapshot)
		{
			ReportPresentationResult(dmui::DrawStyledText(Client(), "User-defined exceptions", kHeadingText));
			DrawEditActions(a_snapshot);
			DrawEditor();
			if (g_pageState.entries.empty())
			{
				DrawEmptyEntryState(a_snapshot);
				return;
			}

			const auto minimumHeight =
				dmui::ui::GetTextLineHeightWithSpacing() * 6.0f;
			const auto height =
				(std::max)(dmui::ui::GetContentRegionAvail().y, minimumHeight);
			if (!dmui::ui::BeginTable(
					"##facegen_user_exceptions",
					6,
					kDiagnosticTableFlags,
					{ 0.0f, height }))
				return;

			dmui::ui::TableSetupScrollFreeze(0, 1);
			dmui::ui::TableSetupColumn("Key", dmui::ui::TableColumnFlags::kWidthStretch, 1.5f);
			dmui::ui::TableSetupColumn("FormID", dmui::ui::TableColumnFlags::kWidthStretch, 1.2f);
			dmui::ui::TableSetupColumn("Plugin", dmui::ui::TableColumnFlags::kWidthStretch, 1.5f);
			dmui::ui::TableSetupColumn(
				"Resolved FormID",
				dmui::ui::TableColumnFlags::kWidthFixed,
				FormIDColumnWidth("Resolved FormID"));
			dmui::ui::TableSetupColumn("Status", dmui::ui::TableColumnFlags::kWidthStretch, 1.2f);
			dmui::ui::TableSetupColumn(
				"Actions",
				dmui::ui::TableColumnFlags::kWidthFixed,
				ActionsColumnWidth());
			dmui::ui::TableHeadersRow();

			for (size_t index = 0; index < g_pageState.entries.size(); ++index)
			{
				const auto& entry = g_pageState.entries[index];
				const auto validation =
					ValidateFacegenException(entry, g_pageState.entries, index);
				dmui::ui::PushID(static_cast<int>(index));
				dmui::ui::TableNextRow();
				(void)dmui::ui::TableSetColumnIndex(0);
				ReportPresentationResult(dmui::DrawStyledText(Client(), entry.key, kBodyText));
				(void)dmui::ui::TableSetColumnIndex(1);
				ReportPresentationResult(dmui::DrawStyledText(Client(), entry.formID, kBodyText));
				(void)dmui::ui::TableSetColumnIndex(2);
				if (entry.pluginName && !entry.pluginName->empty())
					ReportPresentationResult(dmui::DrawStyledText(Client(), *entry.pluginName, kBodyText));
				else
					ReportPresentationResult(dmui::DrawStyledText(Client(), "-", kMutedText));
				(void)dmui::ui::TableSetColumnIndex(3);
				if (validation.resolvedFormID)
					ReportPresentationResult(dmui::DrawStyledText(Client(), MenuUi::Print(
						"0x%08X",
						*validation.resolvedFormID), kBodyText));
				else
					ReportPresentationResult(dmui::DrawStyledText(Client(), "-", kMutedText));
				(void)dmui::ui::TableSetColumnIndex(4);
				DrawStatus(validation.status);
				(void)dmui::ui::TableSetColumnIndex(5);
				if (dmui::ui::Button("Edit"))
					BeginEdit(index);
				dmui::ui::SameLine();
				if (dmui::ui::Button("Remove"))
				{
					RemoveEntry(index);
					dmui::ui::PopID();
					break;
				}
				dmui::ui::PopID();
			}
			dmui::ui::EndTable();
		}
	}

	void DrawFacegenExceptionsPage([[maybe_unused]] void* a_userData) noexcept
	{
		const auto snapshot = GetFacegenExceptionSnapshot();
		const auto facegenEnabled = bPatchesFacegen.GetValue();
		EnsureDraft(snapshot);

		ReportPresentationResult(Client().DrawSectionHeader(
			"Facegen Exceptions",
			DearModdingUI::PhosphorGlyph::kFiles));
		DrawOverview(snapshot, facegenEnabled);
		dmui::ui::Spacing();
		DrawConfigurationState(snapshot, facegenEnabled);
		dmui::ui::Spacing();
		DrawPrimaryExceptions();
		dmui::ui::Spacing();
		DrawUserExceptions(snapshot);
	}
}
