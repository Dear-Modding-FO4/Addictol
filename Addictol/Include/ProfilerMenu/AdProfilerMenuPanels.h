#pragma once

#include <AdProfilerMenuModel.h>

#include <imgui/imgui.h>

#include <cstdint>
#include <string_view>

namespace Addictol
{
	struct ProfilerMenuDrawContext
	{
		uint64_t nowQpc{ 0 };
		uint64_t qpcFrequency{ 0 };
		uint32_t refreshMs{ 0 };
		uint32_t toggleKey{ 0 };
		double lastDrawMs{ 0.0 };
		ProfilerMenuTab activeTab{ ProfilerMenuTab::kOverview };
	};

	using ProfilerMenuPanelDraw = void (*)(ProfilerMenuModel&, const ProfilerMenuDrawContext&) noexcept;

	void DrawProfilerMenuOverview(ProfilerMenuModel& a_model, const ProfilerMenuDrawContext& a_context) noexcept;
	void DrawProfilerMenuFrameHitch(ProfilerMenuModel& a_model, const ProfilerMenuDrawContext& a_context) noexcept;
	void DrawProfilerMenuDecompression(ProfilerMenuModel& a_model, const ProfilerMenuDrawContext& a_context) noexcept;
	void DrawProfilerMenuAllocator(ProfilerMenuModel& a_model, const ProfilerMenuDrawContext& a_context) noexcept;
	void DrawProfilerMenuMemory(ProfilerMenuModel& a_model, const ProfilerMenuDrawContext& a_context) noexcept;
	void DrawProfilerMenuModules(ProfilerMenuModel& a_model, const ProfilerMenuDrawContext& a_context) noexcept;
	void DrawProfilerMenuTextureDecode(ProfilerMenuModel& a_model, const ProfilerMenuDrawContext& a_context) noexcept;

	// Shared drawing vocabulary, so panels stay declarative and consistent.
	namespace ProfilerMenuUi
	{
		inline constexpr ImGuiTableFlags kSortableTableFlags =
			ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg |
			ImGuiTableFlags_BordersInner | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY;
		inline constexpr ImGuiTableFlags kTableFlags =
			ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
			ImGuiTableFlags_BordersInner | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY;

		struct ScopedFont
		{
			explicit ScopedFont(ImFont* a_font) noexcept;
			~ScopedFont() noexcept;

			ScopedFont(const ScopedFont&) = delete;
			ScopedFont& operator=(const ScopedFont&) = delete;

		private:
			bool m_pushed{ false };
		};

		void Heading(std::string_view a_text) noexcept;
		void Title(std::string_view a_text) noexcept;
		void Mono(std::string_view a_text) noexcept;
		void Muted(std::string_view a_text) noexcept;
		void Warn(std::string_view a_text) noexcept;
		void Error(std::string_view a_text) noexcept;
		void LabeledValue(std::string_view a_label, std::string_view a_value) noexcept;
		void LabeledState(std::string_view a_label, bool a_ok, std::string_view a_value) noexcept;
		void MonoCell(std::string_view a_text) noexcept;

		// Formatted values live in a small rotating buffer, so several may be live in one call.
		[[nodiscard]] std::string_view FormatBytes(uint64_t a_bytes) noexcept;
		[[nodiscard]] std::string_view FormatSignedBytes(int64_t a_bytes) noexcept;
		[[nodiscard]] std::string_view FormatCount(uint64_t a_count) noexcept;
		[[nodiscard]] std::string_view FormatSigned(int64_t a_value) noexcept;
		[[nodiscard]] std::string_view FormatMs(double a_milliseconds) noexcept;
		[[nodiscard]] std::string_view FormatTicks(uint64_t a_ticks, uint64_t a_qpcFrequency) noexcept;
		[[nodiscard]] std::string_view FormatRatio(uint64_t a_numerator, uint64_t a_denominator) noexcept;
		[[nodiscard]] std::string_view FormatLinesInLastMinute(double a_lines) noexcept;
		[[nodiscard]] std::string_view FormatBool(bool a_value) noexcept;
		[[nodiscard]] std::string_view FormatCacheAge(
			const ProfilerMenuPanelState& a_state,
			const ProfilerMenuDrawContext& a_context) noexcept;

		void PanelFooter(
			const ProfilerMenuPanelState& a_state,
			const ProfilerMenuDrawContext& a_context) noexcept;
	}
}
