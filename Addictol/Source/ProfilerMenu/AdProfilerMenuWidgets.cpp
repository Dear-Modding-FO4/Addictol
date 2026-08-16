#include <AdImguiTheme.h>
#include <ProfilerMenu/AdProfilerMenuPanels.h>

#include <array>
#include <cstdio>

namespace Addictol
{
	namespace profilerMenuUiDetail
	{
		inline constexpr size_t kBufferCount{ 12 };
		inline constexpr size_t kBufferCapacity{ 96 };

		// Several formatted values are live inside one ImGui call, so the buffers rotate.
		[[nodiscard]] char* NextBuffer() noexcept
		{
			static thread_local std::array<std::array<char, kBufferCapacity>, kBufferCount> buffers{};
			static thread_local size_t next{ 0 };
			auto& buffer = buffers[next];
			next = (next + 1) % kBufferCount;
			return buffer.data();
		}

		[[nodiscard]] std::string_view Print(const char* a_format, auto... a_args) noexcept
		{
			auto* buffer = NextBuffer();
			const auto written = std::snprintf(buffer, kBufferCapacity, a_format, a_args...);
			return std::string_view{
				buffer,
				ClampProfilerMenuFormattedLength(written, kBufferCapacity)
			};
		}
	}

	///////////////////////////////////////////////////////////////////////////////

	ProfilerMenuUi::ScopedFont::ScopedFont(ImFont* a_font) noexcept
	{
		if (!a_font)
			return;
		ImGui::PushFont(a_font, a_font->LegacySize);
		m_pushed = true;
	}

	ProfilerMenuUi::ScopedFont::~ScopedFont() noexcept
	{
		if (m_pushed)
			ImGui::PopFont();
	}

	void ProfilerMenuUi::Heading(std::string_view a_text) noexcept
	{
		const ScopedFont font{ Theme::GetFonts().heading };
		ImGui::TextColored(Theme::colors::kAccent, "%.*s", static_cast<int>(a_text.size()), a_text.data());
	}

	void ProfilerMenuUi::Title(std::string_view a_text) noexcept
	{
		const ScopedFont font{ Theme::GetFonts().title };
		ImGui::TextUnformatted(a_text.data(), a_text.data() + a_text.size());
	}

	void ProfilerMenuUi::Mono(std::string_view a_text) noexcept
	{
		const ScopedFont font{ Theme::GetFonts().heading };
		ImGui::TextUnformatted(a_text.data(), a_text.data() + a_text.size());
	}

	void ProfilerMenuUi::Muted(std::string_view a_text) noexcept
	{
		const ScopedFont font{ Theme::GetFonts().subtext };
		ImGui::TextColored(Theme::colors::kMuted, "%.*s", static_cast<int>(a_text.size()), a_text.data());
	}

	void ProfilerMenuUi::Warn(std::string_view a_text) noexcept
	{
		ImGui::TextColored(Theme::colors::kWarning, "%.*s", static_cast<int>(a_text.size()), a_text.data());
	}

	void ProfilerMenuUi::Error(std::string_view a_text) noexcept
	{
		ImGui::TextColored(Theme::colors::kError, "%.*s", static_cast<int>(a_text.size()), a_text.data());
	}

	void ProfilerMenuUi::LabeledValue(std::string_view a_label, std::string_view a_value) noexcept
	{
		ImGui::TextUnformatted(a_label.data(), a_label.data() + a_label.size());
		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
		Mono(a_value);
	}

	void ProfilerMenuUi::LabeledState(std::string_view a_label, bool a_ok, std::string_view a_value) noexcept
	{
		ImGui::TextUnformatted(a_label.data(), a_label.data() + a_label.size());
		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
		const ScopedFont font{ Theme::GetFonts().heading };
		ImGui::TextColored(
			a_ok ? Theme::colors::kSuccess : Theme::colors::kWarning,
			"%.*s",
			static_cast<int>(a_value.size()),
			a_value.data());
	}

	void ProfilerMenuUi::MonoCell(std::string_view a_text) noexcept
	{
		const ScopedFont font{ Theme::GetFonts().heading };
		ImGui::TextUnformatted(a_text.data(), a_text.data() + a_text.size());
	}

	std::string_view ProfilerMenuUi::FormatBytes(uint64_t a_bytes) noexcept
	{
		using namespace profilerMenuUiDetail;

		constexpr std::array<std::string_view, 5> units{ "B"sv, "KiB"sv, "MiB"sv, "GiB"sv, "TiB"sv };
		auto value = static_cast<double>(a_bytes);
		size_t unit = 0;
		while (value >= 1024.0 && unit + 1 < units.size())
		{
			value /= 1024.0;
			++unit;
		}
		return unit == 0 ?
			Print("%llu B", static_cast<unsigned long long>(a_bytes)) :
			Print("%.2f %.*s", value, static_cast<int>(units[unit].size()), units[unit].data());
	}

	std::string_view ProfilerMenuUi::FormatSignedBytes(int64_t a_bytes) noexcept
	{
		using namespace profilerMenuUiDetail;

		const auto magnitudeValue = a_bytes < 0 ?
			static_cast<uint64_t>(-(a_bytes + 1)) + 1 :
			static_cast<uint64_t>(a_bytes);
		const auto magnitude = FormatBytes(magnitudeValue);
		return Print(
			"%s%.*s",
			a_bytes < 0 ? "-" : "+",
			static_cast<int>(magnitude.size()),
			magnitude.data());
	}

	std::string_view ProfilerMenuUi::FormatCount(uint64_t a_count) noexcept
	{
		return profilerMenuUiDetail::Print("%llu", static_cast<unsigned long long>(a_count));
	}

	std::string_view ProfilerMenuUi::FormatSigned(int64_t a_value) noexcept
	{
		return profilerMenuUiDetail::Print("%lld", static_cast<long long>(a_value));
	}

	std::string_view ProfilerMenuUi::FormatMs(double a_milliseconds) noexcept
	{
		return profilerMenuUiDetail::Print("%.3f ms", a_milliseconds);
	}

	std::string_view ProfilerMenuUi::FormatTicks(uint64_t a_ticks, uint64_t a_qpcFrequency) noexcept
	{
		using namespace profilerMenuUiDetail;

		return a_qpcFrequency ?
			Print("%llu (%.3f ms)",
				static_cast<unsigned long long>(a_ticks),
				QpcToMilliseconds(a_ticks, a_qpcFrequency)) :
			Print("%llu", static_cast<unsigned long long>(a_ticks));
	}

	std::string_view ProfilerMenuUi::FormatRatio(uint64_t a_numerator, uint64_t a_denominator) noexcept
	{
		using namespace profilerMenuUiDetail;

		return a_denominator ?
			Print("%llu / %llu (%.1f%%)",
				static_cast<unsigned long long>(a_numerator),
				static_cast<unsigned long long>(a_denominator),
				(100.0 * static_cast<double>(a_numerator)) / static_cast<double>(a_denominator)) :
			Print("%llu / 0", static_cast<unsigned long long>(a_numerator));
	}

	std::string_view ProfilerMenuUi::FormatLinesInLastMinute(double a_lines) noexcept
	{
		return profilerMenuUiDetail::Print("%.0f lines / last 60 s", a_lines);
	}

	std::string_view ProfilerMenuUi::FormatBool(bool a_value) noexcept
	{
		return a_value ? "yes"sv : "no"sv;
	}

	std::string_view ProfilerMenuUi::FormatCacheAge(
		const ProfilerMenuPanelState& a_state,
		const ProfilerMenuDrawContext& a_context) noexcept
	{
		if (!a_state.hasData)
			return "no data"sv;

		const auto ticks = a_context.nowQpc > a_state.refreshedAtQpc ?
			a_context.nowQpc - a_state.refreshedAtQpc :
			0;
		return profilerMenuUiDetail::Print(
			"%.0f ms", QpcToMilliseconds(ticks, a_context.qpcFrequency));
	}

	void ProfilerMenuUi::PanelFooter(
		const ProfilerMenuPanelState& a_state,
		const ProfilerMenuDrawContext& a_context) noexcept
	{
		ImGui::Separator();
		const auto age = FormatCacheAge(a_state, a_context);
		Muted(profilerMenuUiDetail::Print(
			"cache age %.*s, refresh %.3f ms, cadence %u ms",
			static_cast<int>(age.size()),
			age.data(),
			QpcToMilliseconds(a_state.refreshTicks, a_context.qpcFrequency),
			a_context.refreshMs));
	}
}
