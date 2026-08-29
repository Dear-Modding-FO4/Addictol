// Adapted from Fallout 4 Community Shaders src/Menu/ThemeManager.* and Fonts.*, GPL-3.0.

#include <DearModdingUI/Theme.h>
#include <Core/AdUtils.h>

#include <REX/REX.h>

#include <Windows.h>

#include <imgui/backends/imgui_impl_dx11.h>
#include <imgui/backends/imgui_impl_win32.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string_view>

#undef ERROR

namespace Addictol::DearModdingUI::Theme
{
	using namespace std::literals;

	namespace
	{
		inline constexpr float kBaseBodySize{ 20.0f };
		inline constexpr std::string_view kFontRoot{
			"Data\\F4SE\\Plugins\\DearModdingUI\\Fonts"
		};

		Fonts g_fonts;
		float g_dpiScale{ 1.0f };
		float g_scale{ 1.0f };

		[[nodiscard]] ImVec4 WithAlpha(ImVec4 a_color, float a_alpha) noexcept
		{
			a_color.w = a_alpha;
			return a_color;
		}

		[[nodiscard]] std::filesystem::path AssetPath(std::string_view a_relative)
		{
			auto path = std::filesystem::path{ AdGetRuntimeDirectory() };
			path /= kFontRoot;
			path /= a_relative;
			return path;
		}

		[[nodiscard]] ImFont* AddFont(
			ImFontAtlas& a_atlas,
			std::string_view a_relative,
			float a_size) noexcept
		{
			const auto path = AssetPath(a_relative);
			std::error_code error;
			if (!std::filesystem::exists(path, error))
				return nullptr;
			ImFontConfig config{};
			config.OversampleH = 3;
			config.OversampleV = 2;
			config.PixelSnapH = true;
			config.RasterizerDensity = 1.0f;
			const auto file = path.string();
			return a_atlas.AddFontFromFileTTF(file.c_str(), std::round(a_size), &config);
		}

		[[nodiscard]] bool LoadFonts(ImGuiIO& a_io, float a_scale) noexcept
		{
			g_fonts = {};
			auto& atlas = *a_io.Fonts;
			const auto bodySize = kBaseBodySize * a_scale;
			g_fonts.body = AddFont(atlas, "Jost\\Jost-Regular.ttf"sv, bodySize);
			g_fonts.subtext = AddFont(atlas, "Jost\\Jost-Regular.ttf"sv, bodySize * 0.88f);
			g_fonts.title = AddFont(atlas, "Jost\\Jost-SemiBold.ttf"sv, bodySize * 1.45f);
			g_fonts.heading = AddFont(atlas, "Jost\\Jost-SemiBold.ttf"sv, bodySize * 1.12f);
			g_fonts.monospace = AddFont(
				atlas, "JetBrainsMono\\JetBrainsMono-Regular.ttf"sv, bodySize * 0.88f);

			const auto allBundled = g_fonts.body &&
				g_fonts.subtext &&
				g_fonts.title &&
				g_fonts.heading &&
				g_fonts.monospace;
			if (!g_fonts.body)
				g_fonts.body = atlas.AddFontDefault();
			if (!g_fonts.subtext)
				g_fonts.subtext = g_fonts.body;
			if (!g_fonts.title)
				g_fonts.title = g_fonts.body;
			if (!g_fonts.heading)
				g_fonts.heading = g_fonts.body;
			if (!g_fonts.monospace)
				g_fonts.monospace = g_fonts.body;
			a_io.FontDefault = g_fonts.body;
			return allBundled;
		}

		void ApplyStyle(ImGuiStyle& a_style, float a_scale) noexcept
		{
			ImGui::StyleColorsDark(&a_style);
			a_style.WindowPadding = { 12.0f, 10.0f };
			a_style.FramePadding = { 9.0f, 5.0f };
			a_style.CellPadding = { 8.0f, 4.0f };
			a_style.ItemSpacing = { 7.0f, 8.0f };
			a_style.ItemInnerSpacing = { 6.0f, 5.0f };
			a_style.IndentSpacing = 18.0f;
			a_style.ScrollbarSize = 12.0f;
			a_style.GrabMinSize = 12.0f;
			a_style.WindowRounding = 12.0f;
			a_style.ChildRounding = 8.0f;
			a_style.FrameRounding = 5.0f;
			a_style.PopupRounding = 6.0f;
			a_style.ScrollbarRounding = 9.0f;
			a_style.GrabRounding = 4.0f;
			a_style.TabRounding = 5.0f;
			a_style.WindowBorderSize = 1.0f;
			a_style.ChildBorderSize = 0.0f;
			a_style.PopupBorderSize = 1.0f;
			a_style.FrameBorderSize = 1.0f;
			a_style.TabBorderSize = 0.0f;
			a_style.DockingSeparatorSize = 2.0f;
			a_style.HoverDelayNormal = 0.15f;

			auto* palette = a_style.Colors;
			palette[ImGuiCol_Text] = { 0.950f, 0.965f, 0.955f, 1.00f };
			palette[ImGuiCol_TextDisabled] = { 0.520f, 0.570f, 0.545f, 1.00f };
			palette[ImGuiCol_WindowBg] = { 0.020f, 0.024f, 0.027f, 0.79f };
			palette[ImGuiCol_ChildBg] = { 0.000f, 0.000f, 0.000f, 0.00f };
			palette[ImGuiCol_PopupBg] = { 0.035f, 0.042f, 0.047f, 0.98f };
			palette[ImGuiCol_Border] = { 0.260f, 0.980f, 0.375f, 0.30f };
			palette[ImGuiCol_BorderShadow] = {};
			palette[ImGuiCol_FrameBg] = { 0.160f, 0.185f, 0.170f, 0.80f };
			palette[ImGuiCol_FrameBgHovered] = { 0.180f, 0.610f, 0.280f, 0.54f };
			palette[ImGuiCol_FrameBgActive] = { 0.180f, 0.610f, 0.280f, 0.76f };
			palette[ImGuiCol_TitleBg] = { 0.018f, 0.022f, 0.025f, 0.92f };
			palette[ImGuiCol_TitleBgActive] = { 0.025f, 0.030f, 0.034f, 0.96f };
			palette[ImGuiCol_TitleBgCollapsed] = { 0.018f, 0.022f, 0.025f, 0.86f };
			palette[ImGuiCol_MenuBarBg] = colors::kSidebar;
			palette[ImGuiCol_ScrollbarBg] = { 0.020f, 0.024f, 0.027f, 0.35f };
			palette[ImGuiCol_ScrollbarGrab] = { 0.330f, 0.360f, 0.345f, 0.58f };
			palette[ImGuiCol_ScrollbarGrabHovered] = colors::kAccentMuted;
			palette[ImGuiCol_ScrollbarGrabActive] = colors::kAccent;
			palette[ImGuiCol_CheckMark] = colors::kAccent;
			palette[ImGuiCol_SliderGrab] = colors::kAccentMuted;
			palette[ImGuiCol_SliderGrabActive] = colors::kAccent;
			palette[ImGuiCol_Button] = { 0.180f, 0.610f, 0.280f, 0.38f };
			palette[ImGuiCol_ButtonHovered] = { 0.180f, 0.610f, 0.280f, 0.62f };
			palette[ImGuiCol_ButtonActive] = { 0.260f, 0.980f, 0.375f, 0.48f };
			palette[ImGuiCol_Header] = { 0.180f, 0.610f, 0.280f, 0.36f };
			palette[ImGuiCol_HeaderHovered] = { 0.180f, 0.610f, 0.280f, 0.56f };
			palette[ImGuiCol_HeaderActive] = { 0.260f, 0.980f, 0.375f, 0.42f };
			palette[ImGuiCol_Separator] = { 0.260f, 0.980f, 0.375f, 0.24f };
			palette[ImGuiCol_SeparatorHovered] = colors::kAccentMuted;
			palette[ImGuiCol_SeparatorActive] = colors::kAccent;
			palette[ImGuiCol_ResizeGrip] = { 0.260f, 0.980f, 0.375f, 0.18f };
			palette[ImGuiCol_ResizeGripHovered] = { 0.260f, 0.980f, 0.375f, 0.52f };
			palette[ImGuiCol_ResizeGripActive] = colors::kAccent;
			palette[ImGuiCol_InputTextCursor] = colors::kAccent;
			palette[ImGuiCol_TabHovered] = { 0.180f, 0.610f, 0.280f, 0.62f };
			palette[ImGuiCol_Tab] = { 0.075f, 0.085f, 0.080f, 0.92f };
			palette[ImGuiCol_TabSelected] = { 0.180f, 0.610f, 0.280f, 0.62f };
			palette[ImGuiCol_TabSelectedOverline] = colors::kAccent;
			palette[ImGuiCol_TabDimmed] = { 0.040f, 0.046f, 0.043f, 0.88f };
			palette[ImGuiCol_TabDimmedSelected] = colors::kAccentDeep;
			palette[ImGuiCol_TabDimmedSelectedOverline] = colors::kAccentMuted;
			palette[ImGuiCol_DockingPreview] = WithAlpha(colors::kAccent, 0.58f);
			palette[ImGuiCol_DockingEmptyBg] = { 0.010f, 0.013f, 0.015f, 0.82f };
			palette[ImGuiCol_PlotLines] = colors::kMuted;
			palette[ImGuiCol_PlotLinesHovered] = colors::kAccent;
			palette[ImGuiCol_PlotHistogram] = colors::kAccentMuted;
			palette[ImGuiCol_PlotHistogramHovered] = colors::kAccent;
			palette[ImGuiCol_TableHeaderBg] = { 0.180f, 0.610f, 0.280f, 0.32f };
			palette[ImGuiCol_TableBorderStrong] = { 0.260f, 0.980f, 0.375f, 0.28f };
			palette[ImGuiCol_TableBorderLight] = { 0.260f, 0.980f, 0.375f, 0.14f };
			palette[ImGuiCol_TableRowBg] = {};
			palette[ImGuiCol_TableRowBgAlt] = { 1.000f, 1.000f, 1.000f, 0.035f };
			palette[ImGuiCol_TextLink] = colors::kAccent;
			palette[ImGuiCol_TextSelectedBg] = { 0.180f, 0.610f, 0.280f, 0.44f };
			palette[ImGuiCol_TreeLines] = { 0.670f, 0.710f, 0.690f, 0.55f };
			palette[ImGuiCol_DragDropTarget] = colors::kWarning;
			palette[ImGuiCol_DragDropTargetBg] = { 1.000f, 0.700f, 0.300f, 0.20f };
			palette[ImGuiCol_UnsavedMarker] = colors::kWarning;
			palette[ImGuiCol_NavCursor] = colors::kAccent;
			palette[ImGuiCol_NavWindowingHighlight] = { 0.950f, 0.965f, 0.955f, 0.65f };
			palette[ImGuiCol_NavWindowingDimBg] = { 0.010f, 0.013f, 0.015f, 0.38f };
			palette[ImGuiCol_ModalWindowDimBg] = { 0.010f, 0.013f, 0.015f, 0.62f };
			a_style.ScaleAllSizes(a_scale);
		}

		[[nodiscard]] ImFont* FontForRole(FontRole a_role) noexcept
		{
			switch (a_role)
			{
			case FontRole::kTitle:
				return g_fonts.title;
			case FontRole::kHeading:
				return g_fonts.heading;
			case FontRole::kSubtext:
				return g_fonts.subtext;
			case FontRole::kMonospace:
				return g_fonts.monospace;
			default:
				return g_fonts.body;
			}
		}

		[[nodiscard]] bool BuildEmergencyAtlas(ImGuiIO& a_io) noexcept
		{
			a_io.Fonts->Clear();
			g_fonts = {};
			g_fonts.body = a_io.Fonts->AddFontDefault();
			g_fonts.title = g_fonts.body;
			g_fonts.heading = g_fonts.body;
			g_fonts.subtext = g_fonts.body;
			g_fonts.monospace = g_fonts.body;
			a_io.FontDefault = g_fonts.body;
			return g_fonts.body && a_io.Fonts->Build();
		}
	}

	void Initialize(void* a_window) noexcept
	{
		g_dpiScale = a_window ?
			ImGui_ImplWin32_GetDpiScaleForHwnd(static_cast<HWND>(a_window)) :
			1.0f;
		g_scale = ResolveUiScale(g_dpiScale, 0);
		auto& io = ImGui::GetIO();
		io.ConfigDockingWithShift = true;
		io.ConfigInputTrickleEventQueue = false;
		ApplyStyle(ImGui::GetStyle(), g_scale);
		const auto bundled = LoadFonts(io, g_scale);
		if (!bundled)
			REX::WARN("DearModdingUI: bundled font roles are incomplete; using safe fallbacks"sv);
		if (!io.Fonts->Build() && !BuildEmergencyAtlas(io))
			REX::ERROR("DearModdingUI: no usable font atlas could be prepared"sv);
	}

	bool PrepareFrame(uint32_t a_backBufferHeight) noexcept
	{
		const auto desiredScale = ResolveUiScale(g_dpiScale, a_backBufferHeight);
		if (std::abs(desiredScale - g_scale) < 0.05f)
			return true;
		auto* context = ImGui::GetCurrentContext();
		if (!context || context->WithinFrameScope)
			return false;

		auto& io = ImGui::GetIO();
		ImGui_ImplDX11_InvalidateDeviceObjects();
		io.Fonts->Clear();
		ApplyStyle(ImGui::GetStyle(), desiredScale);
		const auto bundled = LoadFonts(io, desiredScale);
		const auto built = io.Fonts->Build() && ImGui_ImplDX11_CreateDeviceObjects();
		if (!built)
		{
			ImGui_ImplDX11_InvalidateDeviceObjects();
			if (!BuildEmergencyAtlas(io) ||
				!ImGui_ImplDX11_CreateDeviceObjects())
			{
				REX::ERROR("DearModdingUI: font atlas rebuild failed"sv);
				return false;
			}
		}
		g_scale = desiredScale;
		REX::INFO("DearModdingUI: visuals scaled to {:.2f}x for {}p"sv,
			g_scale, a_backBufferHeight);
		if (!bundled)
			REX::WARN("DearModdingUI: scaled atlas uses fallback font roles"sv);
		return true;
	}

	const Fonts& GetFonts() noexcept
	{
		return g_fonts;
	}

	float Scale() noexcept
	{
		return g_scale;
	}

	FontGuard::FontGuard(FontRole a_role) noexcept
	{
		auto* font = FontForRole(a_role);
		if (!font)
			return;
		ImGui::PushFont(font, font->LegacySize);
		m_pushed = true;
	}

	FontGuard::~FontGuard() noexcept
	{
		if (m_pushed)
			ImGui::PopFont();
	}
}
