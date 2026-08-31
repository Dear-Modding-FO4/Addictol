#include <DearModdingUI/Status.h>

#include <limits>

namespace Addictol::DearModdingUI
{
	DMUI_Result StatusModel::Set(
		StatusOwnerKind a_ownerKind,
		std::string_view a_owner,
		DMUI_StatusSeverity a_severity,
		std::string_view a_message,
		StatusClock::time_point a_now) noexcept
	{
		if (a_owner.empty() ||
			a_message.empty() ||
			!IsValidStatusSeverity(a_severity))
			return DMUI_RESULT_INVALID_ARGUMENT;

		try
		{
			StatusMessage next;
			next.ownerKind = a_ownerKind;
			next.severity = a_severity;
			next.owner.assign(a_owner);
			next.message.assign(a_message);
			next.attributedText.reserve(
				next.owner.size() + next.message.size() + 2);
			next.attributedText.append(next.owner);
			next.attributedText.append(": ");
			next.attributedText.append(next.message);
			next.createdAt = a_now;
			next.persistent = IsPersistentStatus(a_severity);

			const std::scoped_lock lock{ m_mutex };
			if (m_generation == (std::numeric_limits<uint64_t>::max)())
				m_generation = 0;
			next.generation = ++m_generation;
			m_current = std::move(next);
			return DMUI_RESULT_OK;
		}
		catch (...)
		{
			return DMUI_RESULT_RESOURCE_EXHAUSTED;
		}
	}

	std::optional<StatusMessage> StatusModel::Snapshot(
		StatusClock::time_point a_now) noexcept
	{
		try
		{
			const std::scoped_lock lock{ m_mutex };
			if (m_current &&
				!m_current->persistent &&
				a_now >= m_current->createdAt + kTransientStatusLifetime)
				m_current.reset();
			return m_current;
		}
		catch (...)
		{
			return std::nullopt;
		}
	}

	bool StatusModel::Dismiss(uint64_t a_generation) noexcept
	{
		const std::scoped_lock lock{ m_mutex };
		if (!m_current ||
			!m_current->persistent ||
			m_current->generation != a_generation)
			return false;
		m_current.reset();
		return true;
	}
}
