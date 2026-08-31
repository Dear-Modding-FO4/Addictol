#include <Menu/AdMenuFacegenExceptions.h>

#include <Core/Settings/AdSettings.h>
#include <DearModdingUI/IconGlyphs.h>
#include <Menu/AdMenu.h>
#include <Menu/AdMenuWidgets.h>
#include <Modules/AdFacegenExceptions.h>

#include <DearModdingUI/ImGuiForward.h>

#include <algorithm>

namespace Addictol::Menu
{
	namespace
	{
		[[nodiscard]] std::string_view StatusLabel(FacegenExceptionStatus a_status) noexcept
		{
			switch (a_status)
			{
			case FacegenExceptionStatus::kResolved:
				return "Resolved"sv;
			case FacegenExceptionStatus::kPluginNotFound:
				return "Plugin not found"sv;
			case FacegenExceptionStatus::kMissingPluginName:
				return "Missing plugin name"sv;
			case FacegenExceptionStatus::kFatalError:
				return "Fatal resolution error"sv;
			case FacegenExceptionStatus::kEmptyValue:
				return "Empty value"sv;
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
			ImGui::TableSetupColumn("FormID", ImGuiTableColumnFlags_WidthFixed, 120.0f);
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

		void DrawUserExceptions(const FacegenExceptionSnapshot& a_snapshot) noexcept
		{
			MenuUi::Heading("User-defined exceptions");
			if (a_snapshot.entries.empty())
			{
				DrawEmptyEntryState(a_snapshot);
				return;
			}

			const auto height = (std::max)(ImGui::GetContentRegionAvail().y, 180.0f);
			if (!ImGui::BeginTable(
					"##facegen_user_exceptions",
					5,
					MenuUi::kTableFlags,
					{ 0.0f, height }))
				return;

			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 1.5f);
			ImGui::TableSetupColumn("Raw value", ImGuiTableColumnFlags_WidthStretch, 1.5f);
			ImGui::TableSetupColumn("Plugin", ImGuiTableColumnFlags_WidthStretch, 1.5f);
			ImGui::TableSetupColumn("Resolved FormID", ImGuiTableColumnFlags_WidthFixed, 130.0f);
			ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 1.2f);
			ImGui::TableHeadersRow();

			for (const auto& entry : a_snapshot.entries)
			{
				ImGui::TableNextRow();
				(void)ImGui::TableSetColumnIndex(0);
				MenuUi::MonoCell(entry.key);
				(void)ImGui::TableSetColumnIndex(1);
				MenuUi::MonoCell(entry.rawValue);
				(void)ImGui::TableSetColumnIndex(2);
				if (entry.pluginName && !entry.pluginName->empty())
					MenuUi::MonoCell(*entry.pluginName);
				else if (entry.status == FacegenExceptionStatus::kMissingPluginName)
					MenuUi::Error("Missing");
				else
					MenuUi::Muted("-");
				(void)ImGui::TableSetColumnIndex(3);
				if (entry.resolvedFormID)
					MenuUi::MonoCell(MenuUi::Print("0x%08X", *entry.resolvedFormID));
				else
					MenuUi::Muted("-");
				(void)ImGui::TableSetColumnIndex(4);
				DrawStatus(entry.status);
			}
			ImGui::EndTable();
		}
	}

	void DrawFacegenExceptionsPage([[maybe_unused]] void* a_userData) noexcept
	{
		const auto snapshot = GetFacegenExceptionSnapshot();
		const auto facegenEnabled = bPatchesFacegen.GetValue();

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
