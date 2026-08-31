#pragma once

#include <DearModdingUI/API.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Addictol::DearModdingUI
{
	using StatusClock = std::chrono::steady_clock;
	inline constexpr auto kTransientStatusLifetime = std::chrono::seconds{ 4 };

	enum class StatusOwnerKind : uint32_t
	{
		kHost,
		kClient
	};

	struct StatusMessage
	{
		uint64_t generation{ 0 };
		StatusOwnerKind ownerKind{ StatusOwnerKind::kHost };
		DMUI_StatusSeverity severity{ DMUI_STATUS_SEVERITY_INFO };
		std::string owner;
		std::string message;
		std::string attributedText;
		StatusClock::time_point createdAt{};
		bool persistent{ false };
	};

	struct StatusTextPresentation
	{
		std::string visible;
		std::string full;
		bool truncated{ false };
	};

	[[nodiscard]] constexpr bool IsValidStatusSeverity(
		DMUI_StatusSeverity a_severity) noexcept
	{
		return a_severity == DMUI_STATUS_SEVERITY_INFO ||
			a_severity == DMUI_STATUS_SEVERITY_SUCCESS ||
			a_severity == DMUI_STATUS_SEVERITY_WARNING ||
			a_severity == DMUI_STATUS_SEVERITY_ERROR;
	}

	[[nodiscard]] constexpr bool IsPersistentStatus(
		DMUI_StatusSeverity a_severity) noexcept
	{
		return a_severity == DMUI_STATUS_SEVERITY_WARNING ||
			a_severity == DMUI_STATUS_SEVERITY_ERROR;
	}

	[[nodiscard]] constexpr DMUI_Result ValidateStatusRequest(
		DMUI_Result a_clientValidation,
		DMUI_StatusSeverity a_severity,
		const char* a_message) noexcept
	{
		if (a_clientValidation != DMUI_RESULT_OK)
			return a_clientValidation;
		if (!IsValidStatusSeverity(a_severity) || !a_message || !a_message[0])
			return DMUI_RESULT_INVALID_ARGUMENT;
		return DMUI_RESULT_OK;
	}

	[[nodiscard]] constexpr size_t Utf8PrefixBoundary(
		std::string_view a_text,
		size_t a_requestedLength) noexcept
	{
		auto length = (std::min)(a_requestedLength, a_text.size());
		while (length > 0 && length < a_text.size() &&
			(static_cast<unsigned char>(a_text[length]) & 0xC0u) == 0x80u)
			--length;
		return length;
	}

	template <class Measure>
	[[nodiscard]] StatusTextPresentation FitStatusText(
		std::string_view a_text,
		float a_availableWidth,
		Measure&& a_measure)
	{
		StatusTextPresentation result{ std::string{ a_text }, std::string{ a_text }, false };
		if (a_text.empty() || a_measure(a_text) <= a_availableWidth)
			return result;

		constexpr std::string_view ellipsis{ "\xE2\x80\xA6" };
		result.truncated = true;
		result.visible.assign(ellipsis);
		if (a_availableWidth <= 0.0f || a_measure(ellipsis) > a_availableWidth)
			return result;

		size_t low = 0;
		size_t high = a_text.size();
		while (low < high)
		{
			const auto midpoint = low + (high - low + 1) / 2;
			const auto prefixLength = Utf8PrefixBoundary(a_text, midpoint);
			std::string candidate{ a_text.substr(0, prefixLength) };
			candidate.append(ellipsis);
			if (a_measure(candidate) <= a_availableWidth)
			{
				low = midpoint;
				result.visible = std::move(candidate);
			}
			else
			{
				high = midpoint - 1;
			}
		}
		return result;
	}

	class StatusModel
	{
	public:
		[[nodiscard]] DMUI_Result Set(
			StatusOwnerKind a_ownerKind,
			std::string_view a_owner,
			DMUI_StatusSeverity a_severity,
			std::string_view a_message,
			StatusClock::time_point a_now = StatusClock::now()) noexcept;
		[[nodiscard]] std::optional<StatusMessage> Snapshot(
			StatusClock::time_point a_now = StatusClock::now()) noexcept;
		[[nodiscard]] bool Dismiss(uint64_t a_generation) noexcept;

	private:
		mutable std::mutex m_mutex;
		std::optional<StatusMessage> m_current;
		uint64_t m_generation{ 0 };
	};
}
