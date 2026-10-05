#pragma once

#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Addictol
{
	// Non-owning, so owner teardown can detach dependents that are still alive.
	template <class Owner, class Dependent>
	class DependentRegistry
	{
	public:
		void Link(Owner a_owner, Dependent a_dependent)
		{
			std::scoped_lock lock{ m_mutex };
			UnlinkLocked(a_dependent);
			m_owners.insert_or_assign(a_dependent, a_owner);
			m_dependents[a_owner].push_back(a_dependent);
		}

		void Unlink(Dependent a_dependent)
		{
			std::scoped_lock lock{ m_mutex };
			UnlinkLocked(a_dependent);
		}

		// Runs under the lock so a concurrent Unlink cannot let a dependent be freed mid-detach.
		template <class Detach>
		void Release(Owner a_owner, Detach&& a_detach)
		{
			std::scoped_lock lock{ m_mutex };
			const auto position = m_dependents.find(a_owner);
			if (position == m_dependents.end())
				return;

			for (const auto dependent : position->second)
			{
				m_owners.erase(dependent);
				a_detach(dependent);
			}
			m_dependents.erase(position);
		}

		[[nodiscard]] size_t LinkedCount() const
		{
			std::scoped_lock lock{ m_mutex };
			return m_owners.size();
		}

	private:
		void UnlinkLocked(Dependent a_dependent)
		{
			const auto owner = m_owners.find(a_dependent);
			if (owner == m_owners.end())
				return;

			const auto dependents = m_dependents.find(owner->second);
			if (dependents != m_dependents.end())
			{
				std::erase(dependents->second, a_dependent);
				if (dependents->second.empty())
					m_dependents.erase(dependents);
			}
			m_owners.erase(owner);
		}

		mutable std::mutex m_mutex;
		std::unordered_map<Dependent, Owner> m_owners;
		std::unordered_map<Owner, std::vector<Dependent>> m_dependents;
	};
}
