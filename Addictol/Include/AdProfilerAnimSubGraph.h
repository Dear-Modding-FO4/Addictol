#pragma once

#include <F4SE/F4SE.h>
#include <REX/REX.h>

namespace Addictol
{
	class ProfilerAnimSubGraph :
		public REX::Singleton<ProfilerAnimSubGraph>
	{
		bool m_installed{ false };

		ProfilerAnimSubGraph(const ProfilerAnimSubGraph&) = delete;
		ProfilerAnimSubGraph& operator=(const ProfilerAnimSubGraph&) = delete;

	public:
		ProfilerAnimSubGraph() = default;
		virtual ~ProfilerAnimSubGraph() = default;

		void Install() noexcept;
		void HandleMessage(F4SE::MessagingInterface::Message* a_msg) noexcept;
		[[nodiscard]] bool IsInstalled() const noexcept { return m_installed; }
	};
}
