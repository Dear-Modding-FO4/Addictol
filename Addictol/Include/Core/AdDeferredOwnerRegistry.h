#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace Addictol
{
	template <class Key, class Owner>
	class DeferredOwnerRegistry
	{
	public:
		using Task = std::function<void()>;

		template <class Schedule>
		void Retain(Key a_key, Owner a_owner, Schedule&& a_schedule)
		{
			std::optional<Owner> displaced;
			{
				std::scoped_lock lock{ m_mutex };
				const auto [position, inserted] =
					m_retained.try_emplace(a_key, std::move(a_owner));
				if (!inserted)
				{
					displaced.emplace(std::move(position->second));
					position->second = std::move(a_owner);
				}
			}

			Queue(std::move(displaced), std::forward<Schedule>(a_schedule));
		}

		template <class Original, class Schedule>
		auto RetireAfter(Key a_key, Original&& a_original, Schedule&& a_schedule)
			-> std::invoke_result_t<Original>
		{
			auto owner = Extract(a_key);
			if constexpr (std::is_void_v<std::invoke_result_t<Original>>)
			{
				std::invoke(std::forward<Original>(a_original));
				Queue(std::move(owner), std::forward<Schedule>(a_schedule));
			}
			else
			{
				auto result = std::invoke(std::forward<Original>(a_original));
				Queue(std::move(owner), std::forward<Schedule>(a_schedule));
				return result;
			}
		}

		[[nodiscard]] size_t RetainedCount() const
		{
			std::scoped_lock lock{ m_mutex };
			return m_retained.size();
		}

		[[nodiscard]] size_t QueuedCount() const
		{
			std::scoped_lock lock{ m_mutex };
			return m_queued.size();
		}

	private:
		[[nodiscard]] std::optional<Owner> Extract(Key a_key)
		{
			std::scoped_lock lock{ m_mutex };
			const auto position = m_retained.find(a_key);
			if (position == m_retained.end())
				return std::nullopt;

			std::optional<Owner> owner{ std::move(position->second) };
			m_retained.erase(position);
			return owner;
		}

		template <class Schedule>
		void Queue(std::optional<Owner> a_owner, Schedule&& a_schedule)
		{
			if (!a_owner)
				return;

			uint64_t token = 0;
			{
				std::scoped_lock lock{ m_mutex };
				token = ++m_nextToken;
				m_queued.emplace(token, std::move(*a_owner));
			}

			std::invoke(
				std::forward<Schedule>(a_schedule),
				Task{ [this, token]() { Complete(token); } });
		}

		void Complete(uint64_t a_token)
		{
			std::optional<Owner> owner;
			{
				std::scoped_lock lock{ m_mutex };
				const auto position = m_queued.find(a_token);
				if (position == m_queued.end())
					return;

				owner.emplace(std::move(position->second));
				m_queued.erase(position);
			}
		}

		mutable std::mutex m_mutex;
		std::unordered_map<Key, Owner> m_retained;
		std::unordered_map<uint64_t, Owner> m_queued;
		uint64_t m_nextToken{};
	};
}
