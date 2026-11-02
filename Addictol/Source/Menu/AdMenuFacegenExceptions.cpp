#include <Menu/AdMenuFacegenExceptions.h>

#include <Core/Settings/AdSettings.h>
#include <DearModdingUI/IconGlyphs.h>
#include <Menu/AdMenu.h>
#include <Menu/AdMenuFormatting.h>
#include <Modules/AdFacegenExceptions.h>

#include <DearModdingUI/UI.h>
#include <RE/T/TESDataHandler.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace Addictol::Menu
{
	namespace
	{
		struct EditorInputState
		{
			// Load order captured when the editor opens.
			std::vector<std::string> plugins;
			std::vector<size_t> completions;
			size_t completionIndex{ 0 };
			std::string plugin;
			bool reloadPlugin{ false };
			bool focusKey{ false };
		};

		struct FacegenExceptionsPageState
		{
			bool initialized{ false };
			bool dirty{ false };
			uint64_t sourceRevision{ 0 };
			std::vector<FacegenExceptionDraft> entries;
			std::optional<size_t> editingIndex;
			FacegenExceptionDraft editor;
			bool editorOpen{ false };
			bool editorPending{ false };
			EditorInputState editorInput;
			dmui::DialogSession confirmation;
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
			g_pageState.editorOpen = false;
			g_pageState.editorPending = false;
		}

		void EnsureDraft(const FacegenExceptionSnapshot& a_snapshot)
		{
			if (!g_pageState.initialized ||
				(!g_pageState.dirty && !g_pageState.editorOpen &&
					g_pageState.sourceRevision != a_snapshot.revision))
				RefreshDraft(a_snapshot);
		}

		void BeginAdd()
		{
			g_pageState.editingIndex = g_pageState.entries.size();
			g_pageState.editor = {};
			g_pageState.editorOpen = true;
			g_pageState.editorPending = true;
		}

		void BeginEdit(size_t a_index)
		{
			g_pageState.editingIndex = a_index;
			g_pageState.editor = g_pageState.entries[a_index];
			if (g_pageState.editor.pluginName &&
				g_pageState.editor.pluginName->empty())
				g_pageState.editor.pluginName.reset();
			g_pageState.editorOpen = true;
			g_pageState.editorPending = true;
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

		[[nodiscard]] std::vector<std::string> LoadedPluginNames()
		{
			std::vector<std::string> names;
			const auto* dataHandler = RE::TESDataHandler::GetSingleton();
			if (!dataHandler)
				return names;
			for (const auto* files : { &dataHandler->compiledFileCollection.files,
					 &dataHandler->compiledFileCollection.smallFiles })
			{
				for (const auto* file : *files)
				{
					if (file)
						names.emplace_back(file->filename);
				}
			}
			return names;
		}

		void UpdatePluginCompletions()
		{
			auto& input = g_pageState.editorInput;
			input.completions.clear();
			input.completionIndex = 0;
			const std::string_view typed = input.plugin;
			if (typed.empty())
				return;
			for (size_t index = 0; index < input.plugins.size(); ++index)
			{
				const std::string_view name = input.plugins[index];
				if (name.size() > typed.size() &&
					FacegenExceptionKeysEqual(name.substr(0, typed.size()), typed))
					input.completions.push_back(index);
			}
		}

		void CloseEditor()
		{
			g_pageState.editingIndex.reset();
			g_pageState.editor = {};
			g_pageState.editorInput = {};
			g_pageState.editorOpen = false;
			dmui::ui::CloseCurrentPopup();
		}

		void CommitEditor(bool a_adding)
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
			if (a_adding)
				g_pageState.entries.push_back(std::move(entry));
			else
				g_pageState.entries[*g_pageState.editingIndex] = std::move(entry);
			g_pageState.dirty = true;
			CloseEditor();
		}

		// ABI 2.1 reports Enter, Escape, Tab and arrows; 2.0 hosts get a plain field.
		[[nodiscard]] dmui::ui::TextEditState DrawEditorField(
			const char* a_label,
			const char* a_id,
			std::string& a_text,
			dmui::ui::TextEditFlags a_flags,
			size_t a_cursor,
			float a_width)
		{
			dmui::ui::TextUnformatted(a_label);
			dmui::ui::SetNextItemWidth(a_width);
			dmui::ui::TextEditState state{};
			if (Client().AbiMinor() >= 1)
			{
				(void)dmui::ui::InputTextEditor(
					a_id,
					"",
					a_text,
					a_flags | dmui::ui::TextEditFlags::kCaptureCancel,
					a_cursor,
					state);
				return state;
			}
			std::array<char, 256> buffer{};
			strncpy_s(buffer.data(), buffer.size(), a_text.c_str(), _TRUNCATE);
			if (dmui::ui::InputText(a_id, buffer.data(), buffer.size()))
			{
				a_text = buffer.data();
				state.events = static_cast<uint32_t>(dmui::ui::TextEditEvents::kEdited);
			}
			return state;
		}

		void DrawPluginCompletions(const dmui::ui::TextEditState& a_state)
		{
			const auto& input = g_pageState.editorInput;
			if (!a_state.active || input.completions.empty())
				return;
			const dmui::ui::Vec2 below{
				a_state.caretPosition.x,
				a_state.caretPosition.y + a_state.lineHeight
			};
			if (!dmui::ui::BeginTooltipAt(below))
				return;
			constexpr size_t kMaximumShown{ 8 };
			const auto first = input.completionIndex < kMaximumShown ?
				0 :
				input.completionIndex - kMaximumShown + 1;
			const auto last = (std::min)(input.completions.size(), first + kMaximumShown);
			for (auto index = first; index < last; ++index)
			{
				const auto& name = input.plugins[input.completions[index]];
				if (index == input.completionIndex)
					dmui::ui::TextColored(dmui::ToUIVec4(ThemeColors().accent), "%s", name.c_str());
				else
					dmui::ui::TextUnformatted(name);
			}
			ReportPresentationResult(dmui::DrawStyledText(
				Client(), "Tab completes, Up/Down selects", kMutedText));
			dmui::ui::EndTooltip();
		}

		[[nodiscard]] bool HandlePluginField(const dmui::ui::TextEditState& a_state)
		{
			using dmui::ui::HasTextEditEvent;
			using dmui::ui::TextEditEvents;
			auto& input = g_pageState.editorInput;
			if (HasTextEditEvent(a_state, TextEditEvents::kEdited))
				UpdatePluginCompletions();
			const auto count = input.completions.size();
			if (count && HasTextEditEvent(a_state, TextEditEvents::kHistoryNext))
				input.completionIndex = (input.completionIndex + 1) % count;
			if (count && HasTextEditEvent(a_state, TextEditEvents::kHistoryPrevious))
				input.completionIndex = (input.completionIndex + count - 1) % count;
			if (count && HasTextEditEvent(a_state, TextEditEvents::kCompletion))
			{
				input.plugin = input.plugins[input.completions[input.completionIndex]];
				input.reloadPlugin = true;
				UpdatePluginCompletions();
				return true;
			}
			return HasTextEditEvent(a_state, TextEditEvents::kEdited);
		}

		void DrawEditor()
		{
			if (!g_pageState.editingIndex)
				return;

			constexpr auto popup = "Exception editor";
			if (std::exchange(g_pageState.editorPending, false))
			{
				dmui::ui::OpenPopup(popup);
				auto& input = g_pageState.editorInput;
				input = {};
				input.plugins = LoadedPluginNames();
				input.plugin = g_pageState.editor.pluginName.value_or("");
				input.focusKey = true;
			}
			dmui::ui::ModalScope modal{ popup, g_pageState.editorOpen };
			if (!g_pageState.editorOpen)
			{
				g_pageState.editingIndex.reset();
				g_pageState.editor = {};
				g_pageState.editorInput = {};
				return;
			}
			if (!modal)
				return;

			using dmui::ui::HasTextEditEvent;
			using dmui::ui::TextEditEvents;
			using dmui::ui::TextEditFlags;
			auto& input = g_pageState.editorInput;
			const auto adding =
				*g_pageState.editingIndex == g_pageState.entries.size();
			const auto inputWidth = dmui::ui::CalcTextSize("00000000000000000000000000000000").x;
			ReportPresentationResult(dmui::DrawStyledText(
				Client(), adding ? "Add exception"sv : "Edit exception"sv, kHeadingText));

			auto submitted = false;
			auto canceled = false;
			const auto track = [&](const dmui::ui::TextEditState& a_state) {
				submitted = submitted || HasTextEditEvent(a_state, TextEditEvents::kSubmitted);
				canceled = canceled || HasTextEditEvent(a_state, TextEditEvents::kCanceled);
			};

			track(DrawEditorField(
				"Unique name",
				"##facegen_exception_key",
				g_pageState.editor.key,
				std::exchange(input.focusKey, false) ? TextEditFlags::kRequestFocus : TextEditFlags::kNone,
				0,
				inputWidth));
			track(DrawEditorField(
				"FormID",
				"##facegen_exception_formid",
				g_pageState.editor.formID,
				TextEditFlags::kNone,
				0,
				inputWidth));

			auto pluginFlags = TextEditFlags::kHistoryKeys | TextEditFlags::kCompletionKey;
			if (std::exchange(input.reloadPlugin, false))
				pluginFlags |= TextEditFlags::kReload;
			const auto pluginState = DrawEditorField(
				"Plugin name (optional)",
				"##facegen_exception_plugin",
				input.plugin,
				pluginFlags,
				input.plugin.size(),
				inputWidth);
			track(pluginState);
			if (HandlePluginField(pluginState))
			{
				if (input.plugin.empty())
					g_pageState.editor.pluginName.reset();
				else
					g_pageState.editor.pluginName = input.plugin;
			}
			DrawPluginCompletions(pluginState);

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
			submitted = dmui::ui::Button(adding ? "Add entry" : "Update entry") || submitted;
			dmui::ui::EndDisabled();
			dmui::ui::SameLine();
			canceled = dmui::ui::Button("Cancel") || canceled;

			if (canceled)
				CloseEditor();
			else if (submitted && validation.valid)
				CommitEditor(adding);
		}

		void Confirm(const char* a_title, const char* a_body,
			const char* a_acceptLabel, dmui::DialogSession::Submit a_submit)
		{
			if (!g_pageState.confirmation.Open(Client(), {
					.kind = DMUI_DIALOG_KIND_CONFIRM,
					.title = a_title,
					.body = a_body,
					.acceptLabel = a_acceptLabel,
					.cancelLabel = "Cancel"
				}, std::move(a_submit)))
				Notify(DMUI_STATUS_SEVERITY_ERROR, "Could not open confirmation.");
		}

		void DrawEditActions()
		{
			dmui::ui::BeginDisabled(g_pageState.confirmation.Active());
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
					Menu::Notify(
						DMUI_STATUS_SEVERITY_SUCCESS,
						"Facegen exceptions saved.");
				}
				else
				{
					Menu::Notify(
						DMUI_STATUS_SEVERITY_ERROR,
						result.error.c_str());
				}
			}
			dmui::ui::EndDisabled();
			dmui::ui::SameLine();
			dmui::ui::BeginDisabled(!g_pageState.dirty);
			if (dmui::ui::Button("Discard changes"))
				Confirm("Discard changes?", "Discard all unsaved facegen exception changes?",
					"Discard", [](std::string_view) -> std::optional<std::string> {
						RefreshDraft(GetFacegenExceptionSnapshot());
						return std::nullopt;
					});
			dmui::ui::EndDisabled();
			dmui::ui::SameLine();
			dmui::ui::BeginDisabled(g_pageState.dirty);
			if (dmui::ui::Button("Reload from file"))
			{
				Confirm("Reload exceptions?", "Replace the current facegen exceptions with the file contents?",
					"Reload", [](std::string_view) -> std::optional<std::string> {
						const auto result = ReloadFacegenExceptions();
						RefreshDraft(GetFacegenExceptionSnapshot());
						Menu::Notify(
							result.success ? DMUI_STATUS_SEVERITY_SUCCESS : DMUI_STATUS_SEVERITY_ERROR,
							result.success ? "Facegen exceptions reloaded." : result.error.c_str());
						return std::nullopt;
					});
			}
			dmui::ui::EndDisabled();
			dmui::ui::EndDisabled();

			if (g_pageState.dirty)
				ReportPresentationResult(dmui::DrawStyledText(
					Client(), "Unsaved changes. Save or discard before reloading from file.",
					{ .tone = dmui::TextTone::kWarning }));
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
			DrawEditActions();
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

	void PollFacegenExceptionConfirmation() noexcept
	{
		if (!g_pageState.confirmation.Active())
			return;
		g_pageState.confirmation.Poll();
		if (g_pageState.confirmation.LastResult() != DMUI_RESULT_OK)
		{
			REX::WARN("Menu: confirmation failed, result {}."sv,
				DMUI_ResultToString(g_pageState.confirmation.LastResult()));
			Notify(DMUI_STATUS_SEVERITY_ERROR, "Facegen exception confirmation failed.");
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
		DrawEditor();
	}
}
