#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <DearModdingUI/ImGuiFingerprint.h>

#include <type_traits>

static_assert(std::is_same_v<
	decltype(DMUI_MakeImGuiFingerprint()),
	DMUI_ImGuiFingerprint>);

[[maybe_unused]] const auto g_compiledFingerprint = DMUI_MakeImGuiFingerprint();
