#include <DearModdingUI/Theme.h>
#include <DearModdingUI/FontCatalog.h>
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
			bool requestedBody{ false };
		};

		struct LoadedFont
		{
			std::string file;
			float size{ 0.0f };
			ImFont* font{ nullptr };
		};

		Fonts g_fonts;
		float g_baseFontSize{ 0.0f };
		std::vector<FontCatalog::FontFamily> g_fontFamilies;
		std::vector<std::string> g_fontFamilyNames;
		std::string g_fontRequestFamily;
		std::string g_effectiveBodyFontFamily;

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

		void RefreshFontFamilies()
		{
			g_fontFamilies = FontCatalog::Enumerate(AssetPath({}));
			g_fontFamilyNames.clear();
			g_fontFamilyNames.reserve(g_fontFamilies.size());
			for (const auto& family : g_fontFamilies)
				g_fontFamilyNames.push_back(family.name);
		}

		[[nodiscard]] const FontCatalog::FontFamily* ResolveFamily(
			std::string_view a_requested) noexcept
		{
			return FontCatalog::Resolve(
				a_requested,
				g_fontFamilies,
				kDefaultBodyFontFamily);
		}

		[[nodiscard]] bool MergeIconFonts(
			ImFontAtlas& a_atlas,
			const Fonts& a_fonts) noexcept
		{
			const ImFont* candidates[]{
				a_fonts.body,
				a_fonts.title,
				a_fonts.heading,
				a_fonts.subheading,
				a_fonts.subtext
			};
			std::array<const ImFont*, static_cast<size_t>(FontRole::kCount)> merged{};
			size_t mergedCount = 0;
			for (auto* candidate : candidates)
			{
				if (!candidate)
					continue;
				const auto duplicate = std::ranges::find(
					merged.begin(),
					merged.begin() + static_cast<ptrdiff_t>(mergedCount),
					candidate);
				if (duplicate != merged.begin() + static_cast<ptrdiff_t>(mergedCount))
					continue;
				if (!MergeIconFont(
						a_atlas,
						const_cast<ImFont*>(candidate),
						candidate->LegacySize))
					return false;
				merged[mergedCount++] = candidate;
			}
			return mergedCount > 0;
		}

		[[nodiscard]] FontLoadResult LoadFonts(
			ImGuiIO& a_io,
			uint32_t a_backBufferHeight,
			float a_userScale,
			const FontCatalog::FontFamily* a_family) noexcept
		{
			g_fonts = {};
			auto& atlas = *a_io.Fonts;
			std::array<LoadedFont, static_cast<size_t>(FontRole::kCount) + 1> loaded{};
			size_t loadedCount = 0;

			auto loadFile = [&](std::string_view a_file, float a_size) {
				for (size_t cached = 0; cached < loadedCount; ++cached)
				{
					if (loaded[cached].file == a_file &&
						loaded[cached].size == a_size)
						return loaded[cached].font;
				}
				auto* font = AddFont(atlas, a_file, a_size);
				loaded[loadedCount++] = {
					std::string{ a_file },
					a_size,
					font
				};
				return font;
			};

			const auto bodySize = ResolveRoleFontSize(
				FontRole::kBody, a_backBufferHeight, a_userScale);
			const auto requestedFile = a_family ?
				std::string_view{ a_family->regularFile } :
				kFontRoleDefaults[static_cast<size_t>(FontRole::kBody)].file;
			g_fonts.body = loadFile(requestedFile, bodySize);
			const auto requestedBodyLoaded = g_fonts.body != nullptr;
			g_effectiveBodyFontFamily = a_family ?
				a_family->name :
				std::string{ kDefaultBodyFontFamily };
			const auto& defaultBody =
				kFontRoleDefaults[static_cast<size_t>(FontRole::kBody)];
			if (!g_fonts.body && requestedFile != defaultBody.file)
			{
				g_fonts.body = loadFile(defaultBody.file, bodySize);
				g_effectiveBodyFontFamily = kDefaultBodyFontFamily;
			}

			const auto loadRole = [&](FontRole a_role) {
				const auto index = static_cast<size_t>(a_role);
				return loadFile(
					kFontRoleDefaults[index].file,
					ResolveRoleFontSize(
						a_role,
						a_backBufferHeight,
						a_userScale));
			};
			g_fonts.title = loadRole(FontRole::kTitle);
			g_fonts.heading = loadRole(FontRole::kHeading);
			g_fonts.subheading = loadRole(FontRole::kSubheading);
			g_fonts.subtext = loadRole(FontRole::kSubtext);

			const auto allBundled = g_fonts.body &&
				g_fonts.title &&
				g_fonts.heading &&
				g_fonts.subheading &&
				g_fonts.subtext;
			if (!g_fonts.body)
			{
				g_fonts.body = atlas.AddFontDefault();
				g_effectiveBodyFontFamily = "Built-in fallback";
			}
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
				MergeIconFonts(atlas, g_fonts),
				requestedBodyLoaded
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
			g_effectiveBodyFontFamily = "Built-in fallback";
			a_io.FontDefault = g_fonts.body;
			return g_fonts.body && a_io.Fonts->Build();
		}
	}

	namespace colors
	{
		ImVec4 Accent() noexcept
		{
			return HostAccentToImVec4(HostSettings::Current().accentColor);
		}

		ImVec4 AccentMuted() noexcept
		{
			auto accent = Accent();
			accent.w = 0.39f;
			return accent;
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

		const auto settings = HostSettings::Current();
		const auto palette = MakeEffectivePalette(
			HostAccentToImVec4(settings.accentColor),
			settings.windowBackgroundOpacity);
		for (size_t index = 0; index < palette.size(); ++index)
			style.Colors[index] = palette[index];
		ImGui::GetStyle() = style;
	}

	void Initialize([[maybe_unused]] void* a_window) noexcept
	{
		auto& io = ImGui::GetIO();
		io.ConfigDockingWithShift = true;
		io.ConfigInputTrickleEventQueue = false;
		RefreshFontFamilies();
		const auto settings = HostSettings::Current();
		const auto* family = ResolveFamily(settings.bodyFontFamily);
		const auto loaded = LoadFonts(
			io,
			static_cast<uint32_t>(kDefaultScreenHeight),
			settings.uiScale,
			family);
		if (!loaded.roles)
			REX::WARN("DearModdingUI: bundled font roles are incomplete; using safe fallbacks"sv);
		if (!loaded.icons)
			REX::WARN("DearModdingUI: Phosphor icon font is unavailable; using text-only labels"sv);
		if (!io.Fonts->Build() && !BuildEmergencyAtlas(io))
			REX::ERROR("DearModdingUI: no usable font atlas could be prepared"sv);
		g_baseFontSize =
			ResolveFontSize(static_cast<uint32_t>(kDefaultScreenHeight)) *
			settings.uiScale;
		g_fontRequestFamily = family ?
			family->name :
			std::string{ kDefaultBodyFontFamily };
		ApplyStyle();
		if (!loaded.requestedBody)
			REX::WARN("DearModdingUI: requested body font failed; using Jost"sv);
	}

	bool PrepareFrame(uint32_t a_backBufferHeight) noexcept
	{
		const auto settings = HostSettings::Current();
		const auto* family = ResolveFamily(settings.bodyFontFamily);
		const auto requestedFamily = family ?
			std::string_view{ family->name } :
			kDefaultBodyFontFamily;
		const auto desiredFontSize =
			ResolveFontSize(a_backBufferHeight) * settings.uiScale;
		if (std::abs(desiredFontSize - g_baseFontSize) <
				0.01f &&
			requestedFamily == g_fontRequestFamily)
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
		const auto loaded = LoadFonts(
			io,
			a_backBufferHeight,
			settings.uiScale,
			family);
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
		g_fontRequestFamily = requestedFamily;
		ApplyStyle();
		REX::INFO("DearModdingUI: typography resolved to {:.0f}px at {}p"sv,
			desiredFontSize, a_backBufferHeight);
		if (!loaded.roles)
			REX::WARN("DearModdingUI: scaled atlas uses fallback font roles"sv);
		if (!loaded.icons)
			REX::WARN("DearModdingUI: scaled atlas uses text-only labels"sv);
		if (!loaded.requestedBody)
			REX::WARN("DearModdingUI: requested body font failed; using Jost"sv);
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
		const auto settings = HostSettings::Current();
		return ResolveIconTint(
			settings.iconColorMode,
			HostAccentToImVec4(settings.accentColor),
			kFullPalette[ImGuiCol_Text]);
	}

	const std::vector<std::string>& AvailableBodyFontFamilies() noexcept
	{
		return g_fontFamilyNames;
	}

	std::string_view ResolveBodyFontFamily(
		std::string_view a_requested) noexcept
	{
		if (const auto* family = ResolveFamily(a_requested))
			return family->name;
		return kDefaultBodyFontFamily;
	}

	std::string_view EffectiveBodyFontFamily() noexcept
	{
		return g_effectiveBodyFontFamily;
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
