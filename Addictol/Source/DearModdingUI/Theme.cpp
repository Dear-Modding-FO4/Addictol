#include <DearModdingUI/Theme.h>
#include <DearModdingUI/HostSettings.h>
#include <DearModdingUI/IconGlyphs.h>
#include <Core/AdUtils.h>

#include <REX/REX.h>

#include <imgui/backends/imgui_impl_dx11.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>

namespace Addictol::DearModdingUI::Theme
{
	using namespace std::literals;

	namespace
	{
		inline constexpr std::string_view kFontRoot{
			"Data\\F4SE\\Plugins\\DearModdingUI\\Fonts"
		};
		inline constexpr std::string_view kIconFontFile{
			"Phosphor\\Phosphor-Fill.ttf"
		};
		inline constexpr ImWchar kIconGlyphRanges[]{
			static_cast<ImWchar>(PhosphorGlyph::kFirstPrivateUse),
			static_cast<ImWchar>(PhosphorGlyph::kLastPrivateUse),
			0
		};

		struct FontLoadResult
		{
			bool roles{ false };
			bool icons{ false };
		};

		struct LoadedFont
		{
			std::string_view file;
			float size{ 0.0f };
			ImFont* font{ nullptr };
		};

		Fonts g_fonts;
		float g_baseFontSize{ 0.0f };

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
			config.RasterizerMultiply = 1.1f;
			const auto file = path.string();
			return a_atlas.AddFontFromFileTTF(file.c_str(), a_size, &config);
		}

		[[nodiscard]] bool MergeIconFont(
			ImFontAtlas& a_atlas,
			ImFont* a_destination,
			float a_size) noexcept
		{
			if (!a_destination)
				return false;
			const auto path = AssetPath(kIconFontFile);
			std::error_code error;
			if (!std::filesystem::exists(path, error))
				return false;

			ImFontConfig config{};
			config.MergeMode = true;
			config.PixelSnapH = true;
			config.GlyphOffset.y = a_size * kIconDefaults.baselineOffsetRatio;
			config.GlyphMinAdvanceX = a_size;
			config.GlyphMaxAdvanceX = a_size;
			config.DstFont = a_destination;
			const auto file = path.string();
			return a_atlas.AddFontFromFileTTF(
				file.c_str(),
				a_size,
				&config,
				kIconGlyphRanges) != nullptr;
		}

		[[nodiscard]] ImFont* LoadRoleFont(
			ImFontAtlas& a_atlas,
			FontRole a_role,
			uint32_t a_backBufferHeight,
			std::array<LoadedFont, static_cast<size_t>(FontRole::kCount)>& a_loaded,
			size_t a_loadedCount) noexcept
		{
			const auto index = static_cast<size_t>(a_role);
			const auto& role = kFontRoleDefaults[index];
			const auto size = ResolveRoleFontSize(a_role, a_backBufferHeight);
			for (size_t cached = 0; cached < a_loadedCount; ++cached)
			{
				if (a_loaded[cached].file == role.file &&
					a_loaded[cached].size == size)
					return a_loaded[cached].font;
			}
			return AddFont(a_atlas, role.file, size);
		}

		[[nodiscard]] FontLoadResult LoadFonts(
			ImGuiIO& a_io,
			uint32_t a_backBufferHeight) noexcept
		{
			g_fonts = {};
			auto& atlas = *a_io.Fonts;
			std::array<LoadedFont, static_cast<size_t>(FontRole::kCount)> loaded{};
			size_t loadedCount = 0;

			auto load = [&](FontRole a_role) {
				const auto index = static_cast<size_t>(a_role);
				auto* font = LoadRoleFont(
					atlas, a_role, a_backBufferHeight, loaded, loadedCount);
				loaded[loadedCount++] = {
					kFontRoleDefaults[index].file,
					ResolveRoleFontSize(a_role, a_backBufferHeight),
					font
				};
				return font;
			};

			g_fonts.body = load(FontRole::kBody);
			g_fonts.title = load(FontRole::kTitle);
			g_fonts.heading = load(FontRole::kHeading);
			g_fonts.subheading = load(FontRole::kSubheading);
			g_fonts.subtext = load(FontRole::kSubtext);

			const auto allBundled = g_fonts.body &&
				g_fonts.title &&
				g_fonts.heading &&
				g_fonts.subheading &&
				g_fonts.subtext;
			if (!g_fonts.body)
				g_fonts.body = atlas.AddFontDefault();
			if (!g_fonts.title)
				g_fonts.title = g_fonts.body;
			if (!g_fonts.heading)
				g_fonts.heading = g_fonts.body;
			if (!g_fonts.subheading)
				g_fonts.subheading = g_fonts.body;
			if (!g_fonts.subtext)
				g_fonts.subtext = g_fonts.body;
			a_io.FontDefault = g_fonts.body;
			return {
				allBundled,
				MergeIconFont(atlas, g_fonts.body, g_fonts.body->LegacySize)
			};
		}

		[[nodiscard]] ImFont* FontForRole(FontRole a_role) noexcept
		{
			switch (a_role)
			{
			case FontRole::kTitle:
				return g_fonts.title;
			case FontRole::kHeading:
				return g_fonts.heading;
			case FontRole::kSubheading:
				return g_fonts.subheading;
			case FontRole::kSubtext:
				return g_fonts.subtext;
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
			g_fonts.subheading = g_fonts.body;
			g_fonts.subtext = g_fonts.body;
			a_io.FontDefault = g_fonts.body;
			return g_fonts.body && a_io.Fonts->Build();
		}
	}

	void ApplyStyle() noexcept
	{
		const auto baseStyle = MakeBaseStyle();
		auto style = baseStyle;
		const auto* font = ImGui::GetIO().FontDefault;
		const auto bodySize = font ? font->LegacySize : kBaselineFontSize;
		const auto scaleFactor = ResolveStyleScale(bodySize);
		style.ScaleAllSizes(scaleFactor);

		const auto scaleBorder = [scaleFactor](float a_value) {
			if (a_value <= 0.0f)
				return 0.0f;
			return ImMax(1.0f, ImTrunc(a_value * scaleFactor));
		};
		style.WindowBorderSize = scaleBorder(kStyleDefaults.windowBorderSize);
		style.ChildBorderSize = scaleBorder(kStyleDefaults.childBorderSize);
		style.PopupBorderSize = scaleBorder(baseStyle.PopupBorderSize);
		style.FrameBorderSize = scaleBorder(kStyleDefaults.frameBorderSize);
		style.TabBorderSize = scaleBorder(baseStyle.TabBorderSize);
		style.TabBarBorderSize = scaleBorder(baseStyle.TabBarBorderSize);
		style.SeparatorTextBorderSize =
			scaleBorder(baseStyle.SeparatorTextBorderSize);
		style.DockingSeparatorSize = scaleBorder(baseStyle.DockingSeparatorSize);
		style.MouseCursorScale = ImMax(1.0f, baseStyle.MouseCursorScale);
		style.HoverDelayNormal = kTooltipHoverDelay;
		style.FontScaleMain = std::exp2(kDefaultGlobalScale);

		const auto palette = MakeEffectivePalette();
		for (size_t index = 0; index < palette.size(); ++index)
			style.Colors[index] = palette[index];
		ImGui::GetStyle() = style;
	}

	void Initialize([[maybe_unused]] void* a_window) noexcept
	{
		auto& io = ImGui::GetIO();
		io.ConfigDockingWithShift = true;
		io.ConfigInputTrickleEventQueue = false;
		const auto loaded = LoadFonts(io, static_cast<uint32_t>(kDefaultScreenHeight));
		if (!loaded.roles)
			REX::WARN("DearModdingUI: bundled font roles are incomplete; using safe fallbacks"sv);
		if (!loaded.icons)
			REX::WARN("DearModdingUI: Phosphor icon font is unavailable; using text-only labels"sv);
		if (!io.Fonts->Build() && !BuildEmergencyAtlas(io))
			REX::ERROR("DearModdingUI: no usable font atlas could be prepared"sv);
		g_baseFontSize = ResolveFontSize(static_cast<uint32_t>(kDefaultScreenHeight));
		ApplyStyle();
	}

	bool PrepareFrame(uint32_t a_backBufferHeight) noexcept
	{
		const auto desiredFontSize = ResolveFontSize(a_backBufferHeight);
		if (std::abs(desiredFontSize - g_baseFontSize) <
			0.01f)
		{
			ApplyStyle();
			return true;
		}

		auto* context = ImGui::GetCurrentContext();
		if (!context || context->WithinFrameScope)
			return false;

		auto& io = ImGui::GetIO();
		ImGui_ImplDX11_InvalidateDeviceObjects();
		io.Fonts->Clear();
		const auto loaded = LoadFonts(io, a_backBufferHeight);
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

		g_baseFontSize = desiredFontSize;
		ApplyStyle();
		REX::INFO("DearModdingUI: typography resolved to {:.0f}px at {}p"sv,
			desiredFontSize, a_backBufferHeight);
		if (!loaded.roles)
			REX::WARN("DearModdingUI: scaled atlas uses fallback font roles"sv);
		if (!loaded.icons)
			REX::WARN("DearModdingUI: scaled atlas uses text-only labels"sv);
		return true;
	}

	const Fonts& GetFonts() noexcept
	{
		return g_fonts;
	}

	float Scale() noexcept
	{
		return g_fonts.body ?
			g_fonts.body->LegacySize / kBaselineFontSize :
			1.0f;
	}

	float SearchScale() noexcept
	{
		return g_fonts.body ?
			g_fonts.body->LegacySize / kSearchBaselineFontSize :
			kBaselineFontSize / kSearchBaselineFontSize;
	}

	ImVec4 IconTint() noexcept
	{
		return ResolveIconTint(
			HostSettings::Current().iconColorMode,
			colors::kAccent,
			kFullPalette[ImGuiCol_Text]);
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
