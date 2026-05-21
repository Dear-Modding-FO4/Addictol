#pragma once

#include <string_view>
#include <string>

namespace RE
{
	class TESForm;
	class TESObjectREFR;
}


namespace Addictol
{
	using namespace std::literals;

	bool ExecuteCommand(std::string_view a_command, RE::TESObjectREFR* a_targetRef, bool a_silent);

	// easy + formatted way to log form details like formid, editorid (if loaded), and plugin:
	//	{FormID: 0x123456, EditorID: "ExampleForm", Plugin: "Example.esp"}
	std::string GetFormInfo(RE::TESForm* a_form);
}