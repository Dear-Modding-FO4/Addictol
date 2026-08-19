// Adapted from Fallout 4 Community Shaders src/Menu/Theme.h and src/Menu/Theme.cpp, GPL-3.0.

#include <AdImguiTheme.h>
#include <AdUtils.h>

#include <REX/REX.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#undef ERROR

namespace Addictol
{
	namespace themeDetail
	{
		namespace palette
		{
			const ImVec4 kText{ 0.90f, 0.94f, 0.90f, 1.00f };
			const ImVec4 kTextDisabled{ 0.52f, 0.58f, 0.52f, 1.00f };
			// The menu sits over a moving scene, so the window stays near opaque for readability.
			const ImVec4 kWindowBg{ 0.043f, 0.055f, 0.043f, 0.93f };
			const ImVec4 kChildBg{ 0.043f, 0.047f, 0.043f, 0.86f };
			const ImVec4 kPopupBg{ 0.078f, 0.082f, 0.078f, 0.98f };
			const ImVec4 kSurface{ 0.078f, 0.090f, 0.078f, 1.00f };
			const ImVec4 kSurfaceRaised{ 0.105f, 0.118f, 0.105f, 1.00f };
			const ImVec4 kSurfaceInactive{ 0.060f, 0.064f, 0.060f, 1.00f };
			const ImVec4 kScrollbarGrab{ 0.20f, 0.23f, 0.20f, 1.00f };
			const ImVec4 kTransparent{ 0.00f, 0.00f, 0.00f, 0.00f };
			const ImVec4 kRowAlternate{ 0.20f, 0.28f, 0.20f, 0.12f };
			const ImVec4 kWindowingHighlight{ 0.90f, 0.94f, 0.90f, 0.70f };
			const ImVec4 kWindowingDim{ 0.04f, 0.05f, 0.04f, 0.22f };
			const ImVec4 kModalDim{ 0.02f, 0.03f, 0.02f, 0.62f };
		}

		inline constexpr float kBodyPointSize{ 18.0f };
		inline constexpr float kSubtextPointSize{ 15.0f };
		inline constexpr float kTitlePointSize{ 26.0f };
		inline constexpr float kHeadingPointSize{ 21.0f };
		inline constexpr std::string_view kFontRoot{ "Data\\F4SE\\Plugins\\Addictol\\Fonts" };

		static Theme::Fonts g_fonts;

		[[nodiscard]] ImVec4 WithAlpha(ImVec4 a_color, float a_alpha) noexcept
		{
			a_color.w = a_alpha;
			return a_color;
		}

		[[nodiscard]] std::filesystem::path ResolveBundledFont(std::string_view a_fileName) noexcept
		{
			// The game directory is authoritative; the working directory is not ours to assume.
			std::filesystem::path path{ AdGetRuntimeDirectory() };
			path /= kFontRoot;
			path /= a_fileName;
			std::error_code ec;
			if (!std::filesystem::exists(path, ec))
				return {};
			return path;
		}

		[[nodiscard]] ImFont* AddFont(
			ImFontAtlas& a_atlas,
			const std::filesystem::path& a_path,
			float a_pointSize) noexcept
		{
			ImFontConfig config{};
			config.OversampleH = 3;
			config.OversampleV = 1;
			config.PixelSnapH = false;
			const auto path = a_path.string();
			return a_atlas.AddFontFromFileTTF(path.c_str(), a_pointSize, &config);
		}
	}

	///////////////////////////////////////////////////////////////////////////////

	void Theme::ApplyDarkTheme(ImGuiStyle& a_style) noexcept
	{
		using namespace themeDetail;

		ImGui::StyleColorsDark(&a_style);

		a_style.WindowRounding = 6.0f;
		a_style.ChildRounding = 4.0f;
		a_style.FrameRounding = 4.0f;
		a_style.PopupRounding = 4.0f;
		a_style.ScrollbarRounding = 8.0f;
		a_style.GrabRounding = 3.0f;
		a_style.TabRounding = 4.0f;

		a_style.WindowBorderSize = 1.0f;
		a_style.FrameBorderSize = 0.0f;
		a_style.PopupBorderSize = 1.0f;
		a_style.ChildBorderSize = 1.0f;

		a_style.WindowPadding = ImVec2(10.0f, 8.0f);
		a_style.FramePadding = ImVec2(8.0f, 4.0f);
		a_style.ItemSpacing = ImVec2(8.0f, 5.0f);
		a_style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
		a_style.IndentSpacing = 18.0f;
		a_style.ScrollbarSize = 12.0f;
		a_style.GrabMinSize = 12.0f;

		auto* colors = a_style.Colors;
		colors[ImGuiCol_Text] = palette::kText;
		colors[ImGuiCol_TextDisabled] = palette::kTextDisabled;

		colors[ImGuiCol_WindowBg] = palette::kWindowBg;
		colors[ImGuiCol_ChildBg] = palette::kChildBg;
		colors[ImGuiCol_PopupBg] = palette::kPopupBg;

		colors[ImGuiCol_Border] = WithAlpha(Theme::colors::kAccentDark, 0.45f);
		colors[ImGuiCol_BorderShadow] = palette::kTransparent;

		colors[ImGuiCol_FrameBg] = palette::kSurfaceRaised;
		colors[ImGuiCol_FrameBgHovered] = Theme::colors::kAccentDeep;
		colors[ImGuiCol_FrameBgActive] = Theme::colors::kAccentDark;

		colors[ImGuiCol_TitleBg] = palette::kSurfaceInactive;
		colors[ImGuiCol_TitleBgActive] = Theme::colors::kAccentDeep;
		colors[ImGuiCol_TitleBgCollapsed] = WithAlpha(palette::kSurfaceInactive, 0.78f);

		colors[ImGuiCol_MenuBarBg] = palette::kSurface;

		colors[ImGuiCol_ScrollbarBg] = WithAlpha(palette::kWindowBg, 0.55f);
		colors[ImGuiCol_ScrollbarGrab] = palette::kScrollbarGrab;
		colors[ImGuiCol_ScrollbarGrabHovered] = Theme::colors::kAccentDark;
		colors[ImGuiCol_ScrollbarGrabActive] = Theme::colors::kAccentMedium;

		colors[ImGuiCol_CheckMark] = Theme::colors::kAccent;
		colors[ImGuiCol_SliderGrab] = Theme::colors::kAccentMedium;
		colors[ImGuiCol_SliderGrabActive] = Theme::colors::kAccent;

		colors[ImGuiCol_Button] = palette::kSurfaceRaised;
		colors[ImGuiCol_ButtonHovered] = Theme::colors::kAccentDeep;
		colors[ImGuiCol_ButtonActive] = Theme::colors::kAccentDark;

		colors[ImGuiCol_Header] = WithAlpha(Theme::colors::kAccentDeep, 0.72f);
		colors[ImGuiCol_HeaderHovered] = Theme::colors::kAccentDark;
		colors[ImGuiCol_HeaderActive] = Theme::colors::kAccentMedium;

		colors[ImGuiCol_Separator] = WithAlpha(Theme::colors::kAccentDark, 0.42f);
		colors[ImGuiCol_SeparatorHovered] = WithAlpha(Theme::colors::kAccentMedium, 0.82f);
		colors[ImGuiCol_SeparatorActive] = Theme::colors::kAccent;

		colors[ImGuiCol_ResizeGrip] = WithAlpha(palette::kScrollbarGrab, 0.40f);
		colors[ImGuiCol_ResizeGripHovered] = WithAlpha(Theme::colors::kAccentMedium, 0.72f);
		colors[ImGuiCol_ResizeGripActive] = Theme::colors::kAccent;
		colors[ImGuiCol_InputTextCursor] = Theme::colors::kAccent;

		colors[ImGuiCol_Tab] = palette::kSurface;
		colors[ImGuiCol_TabHovered] = Theme::colors::kAccentMedium;
		colors[ImGuiCol_TabSelected] = Theme::colors::kAccentDark;
		colors[ImGuiCol_TabSelectedOverline] = Theme::colors::kAccent;
		colors[ImGuiCol_TabDimmed] = palette::kSurfaceInactive;
		colors[ImGuiCol_TabDimmedSelected] = WithAlpha(Theme::colors::kAccentDeep, 0.72f);
		colors[ImGuiCol_TabDimmedSelectedOverline] = Theme::colors::kAccentDark;
		// Docking colors are deliberately absent: the vendored ImGui has no docking branch.

		colors[ImGuiCol_PlotLines] = Theme::colors::kMuted;
		colors[ImGuiCol_PlotLinesHovered] = Theme::colors::kAccent;
		colors[ImGuiCol_PlotHistogram] = Theme::colors::kAccent;
		colors[ImGuiCol_PlotHistogramHovered] = Theme::colors::kAccent;

		colors[ImGuiCol_TableHeaderBg] = WithAlpha(Theme::colors::kAccentDeep, 0.72f);
		colors[ImGuiCol_TableBorderStrong] = WithAlpha(Theme::colors::kAccentDark, 0.72f);
		colors[ImGuiCol_TableBorderLight] = WithAlpha(Theme::colors::kAccentDark, 0.42f);
		colors[ImGuiCol_TableRowBg] = palette::kTransparent;
		colors[ImGuiCol_TableRowBgAlt] = palette::kRowAlternate;

		colors[ImGuiCol_TextLink] = Theme::colors::kAccent;
		colors[ImGuiCol_TextSelectedBg] = WithAlpha(Theme::colors::kAccentMedium, 0.45f);
		colors[ImGuiCol_TreeLines] = WithAlpha(Theme::colors::kAccentDark, 0.52f);
		colors[ImGuiCol_DragDropTarget] = WithAlpha(Theme::colors::kAccent, 0.90f);
		colors[ImGuiCol_DragDropTargetBg] = WithAlpha(Theme::colors::kAccentDeep, 0.32f);
		colors[ImGuiCol_UnsavedMarker] = Theme::colors::kWarning;
		colors[ImGuiCol_NavCursor] = Theme::colors::kAccent;
		colors[ImGuiCol_NavWindowingHighlight] = palette::kWindowingHighlight;
		colors[ImGuiCol_NavWindowingDimBg] = palette::kWindowingDim;
		colors[ImGuiCol_ModalWindowDimBg] = palette::kModalDim;
	}

	const Theme::Fonts& Theme::GetFonts() noexcept
	{
		return themeDetail::g_fonts;
	}

	bool Theme::LoadFonts(ImGuiIO& a_io, float a_dpiScale) noexcept
	{
		using namespace themeDetail;

		const auto scale = a_dpiScale > 0.0f ? a_dpiScale : 1.0f;
		g_fonts = {};
		auto loadedAllBundled = true;

		const auto interPath = ResolveBundledFont("Inter-Regular.ttf"sv);
		if (!interPath.empty())
		{
			g_fonts.body = AddFont(*a_io.Fonts, interPath, kBodyPointSize * scale);
			g_fonts.subtext = AddFont(*a_io.Fonts, interPath, kSubtextPointSize * scale);
			if (!g_fonts.body || !g_fonts.subtext)
			{
				loadedAllBundled = false;
				REX::WARN("Menu: Inter font roles failed to load from \"{}\""sv, interPath.string());
			}
		}
		else
		{
			loadedAllBundled = false;
			REX::WARN("Menu: bundled font not found: {}\\Inter-Regular.ttf"sv, kFontRoot);
		}

		const auto monoBoldPath = ResolveBundledFont("JetBrainsMono-Bold.ttf"sv);
		if (!monoBoldPath.empty())
		{
			g_fonts.title = AddFont(*a_io.Fonts, monoBoldPath, kTitlePointSize * scale);
			if (!g_fonts.title)
			{
				loadedAllBundled = false;
				REX::WARN("Menu: JetBrains Mono title font failed to load from \"{}\""sv, monoBoldPath.string());
			}
		}
		else
		{
			loadedAllBundled = false;
			REX::WARN("Menu: bundled font not found: {}\\JetBrainsMono-Bold.ttf"sv, kFontRoot);
		}

		const auto monoRegularPath = ResolveBundledFont("JetBrainsMono-Regular.ttf"sv);
		if (!monoRegularPath.empty())
		{
			g_fonts.heading = AddFont(*a_io.Fonts, monoRegularPath, kHeadingPointSize * scale);
			if (!g_fonts.heading)
			{
				loadedAllBundled = false;
				REX::WARN("Menu: JetBrains Mono heading font failed to load from \"{}\""sv, monoRegularPath.string());
			}
		}
		else
		{
			loadedAllBundled = false;
			REX::WARN("Menu: bundled font not found: {}\\JetBrainsMono-Regular.ttf"sv, kFontRoot);
		}

		ImFont* fallback = nullptr;
		const auto useFallback = [&](ImFont*& a_font) noexcept {
			if (a_font)
				return;
			if (!fallback)
				fallback = a_io.Fonts->AddFontDefault();
			a_font = fallback;
		};
		useFallback(g_fonts.body);
		useFallback(g_fonts.subtext);
		useFallback(g_fonts.title);
		useFallback(g_fonts.heading);

		a_io.FontDefault = g_fonts.body;
		if (loadedAllBundled)
		{
			REX::INFO(
				"Menu: font roles loaded at {:.2f}x: Inter body {:.0f}pt, subtext {:.0f}pt; JetBrains Mono title {:.0f}pt, heading {:.0f}pt"sv,
				scale,
				kBodyPointSize * scale,
				kSubtextPointSize * scale,
				kTitlePointSize * scale,
				kHeadingPointSize * scale);
		}
		else
		{
			REX::WARN("Menu: the built-in ImGui font covers the missing roles"sv);
		}
		return loadedAllBundled;
	}

	void Theme::Apply(ImGuiIO& a_io, ImGuiStyle& a_style, float a_dpiScale) noexcept
	{
		const auto scale = a_dpiScale > 0.0f ? a_dpiScale : 1.0f;
		ApplyDarkTheme(a_style);
		// Sizes and fonts each take the scale exactly once; no global font factor is set.
		a_style.ScaleAllSizes(scale);
		(void)LoadFonts(a_io, scale);
	}
}
