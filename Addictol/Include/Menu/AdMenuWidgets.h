#pragma once

#include <Core/AdUtils.h>
#include <Menu/AdMenu.h>
#include <Menu/AdMenuTargets.h>

#include <DearModdingUI/ImGuiForward.h>

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>

namespace Addictol::MenuUi
{
	inline constexpr ImGuiTableFlags kSortableTableFlags =
		ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg |
		ImGuiTableFlags_BordersInner | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY;
	inline constexpr ImGuiTableFlags kTableFlags =
		ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
		ImGuiTableFlags_BordersInner | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY;

	struct ScopedFont
	{
		explicit ScopedFont(DMUI_FontRole a_role) noexcept;

		ScopedFont(const ScopedFont&) = delete;
		ScopedFont& operator=(const ScopedFont&) = delete;

	private:
		dmui::FontGuard m_guard;
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
	[[nodiscard]] std::optional<DMUI_StyleMetrics> StyleMetrics() noexcept;

	inline constexpr size_t kFormatCapacity{ 96 };

	// rotate: several values live per ImGui call
	[[nodiscard]] char* NextFormatBuffer() noexcept;

	[[nodiscard]] std::string_view Print(const char* a_format, auto... a_args) noexcept
	{
		auto* buffer = NextFormatBuffer();
		const auto written = std::snprintf(buffer, kFormatCapacity, a_format, a_args...);
		return std::string_view{ buffer, ClampMenuFormattedLength(written, kFormatCapacity) };
	}

	[[nodiscard]] std::string_view FormatBytes(uint64_t a_bytes) noexcept;
	[[nodiscard]] std::string_view FormatSignedBytes(int64_t a_bytes) noexcept;
	[[nodiscard]] std::string_view FormatCount(uint64_t a_count) noexcept;
	[[nodiscard]] std::string_view FormatSigned(int64_t a_value) noexcept;
	[[nodiscard]] std::string_view FormatMs(double a_milliseconds) noexcept;
	[[nodiscard]] std::string_view FormatTicks(uint64_t a_ticks, uint64_t a_qpcFrequency) noexcept;
	[[nodiscard]] std::string_view FormatRatio(uint64_t a_numerator, uint64_t a_denominator) noexcept;
	[[nodiscard]] std::string_view FormatLinesInLastMinute(double a_lines) noexcept;
	[[nodiscard]] std::string_view FormatBool(bool a_value) noexcept;
}
