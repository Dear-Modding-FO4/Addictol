#include <Menu/AdMenuWidgets.h>

#include <array>
#include <atomic>

namespace Addictol
{
	char* MenuUi::NextFormatBuffer() noexcept
	{
		constexpr size_t kBufferCount{ 12 };

		static thread_local std::array<std::array<char, kFormatCapacity>, kBufferCount> buffers{};
		static thread_local size_t next{ 0 };
		auto& buffer = buffers[next];
		next = (next + 1) % kBufferCount;
		return buffer.data();
	}

	MenuUi::ScopedFont::ScopedFont(DMUI_FontRole a_role) noexcept :
		m_guard(Menu::Client(), a_role)
	{}

	std::optional<DMUI_StyleMetrics> MenuUi::StyleMetrics() noexcept
	{
		DMUI_StyleMetrics metrics{};
		metrics.structSize = DMUI_STYLE_METRICS_1_0_SIZE;
		const auto result = ImGui::GetStyleMetrics(metrics);
		if (result == DMUI_RESULT_OK)
			return metrics;

		static std::atomic_flag reported = ATOMIC_FLAG_INIT;
		if (!reported.test_and_set(std::memory_order_relaxed))
		{
			REX::ERROR(
				"Menu: DearModdingUI style metrics unavailable, result {}."sv,
				DMUI_ResultToString(result));
		}
		return std::nullopt;
	}

	void MenuUi::Heading(std::string_view a_text) noexcept
	{
		const ScopedFont font{ DMUI_FONT_ROLE_HEADING };
		ImGui::TextColored(
			dmui::ToImVec4(Menu::ThemeColors().accent),
			"%.*s",
			static_cast<int>(a_text.size()),
			a_text.data());
	}

	void MenuUi::Title(std::string_view a_text) noexcept
	{
		const ScopedFont font{ DMUI_FONT_ROLE_TITLE };
		ImGui::TextUnformatted(a_text.data(), a_text.data() + a_text.size());
	}

	void MenuUi::Mono(std::string_view a_text) noexcept
	{
		const ScopedFont font{ DMUI_FONT_ROLE_BODY };
		ImGui::TextUnformatted(a_text.data(), a_text.data() + a_text.size());
	}

	void MenuUi::Muted(std::string_view a_text) noexcept
	{
		const ScopedFont font{ DMUI_FONT_ROLE_SUBTEXT };
		ImGui::TextColored(
			dmui::ToImVec4(Menu::ThemeColors().muted),
			"%.*s",
			static_cast<int>(a_text.size()),
			a_text.data());
	}

	void MenuUi::Warn(std::string_view a_text) noexcept
	{
		ImGui::TextColored(
			dmui::ToImVec4(Menu::ThemeColors().warning),
			"%.*s",
			static_cast<int>(a_text.size()),
			a_text.data());
	}

	void MenuUi::Error(std::string_view a_text) noexcept
	{
		ImGui::TextColored(
			dmui::ToImVec4(Menu::ThemeColors().error),
			"%.*s",
			static_cast<int>(a_text.size()),
			a_text.data());
	}

	void MenuUi::LabeledValue(std::string_view a_label, std::string_view a_value) noexcept
	{
		const auto style = StyleMetrics();
		if (!style)
			return;
		ImGui::TextUnformatted(a_label.data(), a_label.data() + a_label.size());
		ImGui::SameLine(0.0f, style->itemSpacing.x * 2.0f);
		Mono(a_value);
	}

	void MenuUi::LabeledState(std::string_view a_label, bool a_ok, std::string_view a_value) noexcept
	{
		const auto style = StyleMetrics();
		if (!style)
			return;
		ImGui::TextUnformatted(a_label.data(), a_label.data() + a_label.size());
		ImGui::SameLine(0.0f, style->itemSpacing.x * 2.0f);
		const ScopedFont font{ DMUI_FONT_ROLE_HEADING };
		ImGui::TextColored(
			dmui::ToImVec4(
				a_ok ? Menu::ThemeColors().success : Menu::ThemeColors().warning),
			"%.*s",
			static_cast<int>(a_value.size()),
			a_value.data());
	}

	void MenuUi::MonoCell(std::string_view a_text) noexcept
	{
		const ScopedFont font{ DMUI_FONT_ROLE_BODY };
		ImGui::TextUnformatted(a_text.data(), a_text.data() + a_text.size());
	}

	std::string_view MenuUi::FormatBytes(uint64_t a_bytes) noexcept
	{
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

	std::string_view MenuUi::FormatSignedBytes(int64_t a_bytes) noexcept
	{
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

	std::string_view MenuUi::FormatCount(uint64_t a_count) noexcept
	{
		return Print("%llu", static_cast<unsigned long long>(a_count));
	}

	std::string_view MenuUi::FormatSigned(int64_t a_value) noexcept
	{
		return Print("%lld", static_cast<long long>(a_value));
	}

	std::string_view MenuUi::FormatMs(double a_milliseconds) noexcept
	{
		return Print("%.3f ms", a_milliseconds);
	}

	std::string_view MenuUi::FormatTicks(uint64_t a_ticks, uint64_t a_qpcFrequency) noexcept
	{
		return a_qpcFrequency ?
			Print("%llu (%.3f ms)",
				static_cast<unsigned long long>(a_ticks),
				QpcToMilliseconds(a_ticks, a_qpcFrequency)) :
			Print("%llu", static_cast<unsigned long long>(a_ticks));
	}

	std::string_view MenuUi::FormatRatio(uint64_t a_numerator, uint64_t a_denominator) noexcept
	{
		return a_denominator ?
			Print("%llu / %llu (%.1f%%)",
				static_cast<unsigned long long>(a_numerator),
				static_cast<unsigned long long>(a_denominator),
				(100.0 * static_cast<double>(a_numerator)) / static_cast<double>(a_denominator)) :
			Print("%llu / 0", static_cast<unsigned long long>(a_numerator));
	}

	std::string_view MenuUi::FormatLinesInLastMinute(double a_lines) noexcept
	{
		return Print("%.0f lines / last 60 s", a_lines);
	}

	std::string_view MenuUi::FormatBool(bool a_value) noexcept
	{
		return a_value ? "yes"sv : "no"sv;
	}
}
