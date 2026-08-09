#pragma once

#include <REX/REX.h>

namespace Addictol
{
	class ProfilerFrameHitch :
		public REX::Singleton<ProfilerFrameHitch>
	{
		bool m_installed{ false };

		ProfilerFrameHitch(const ProfilerFrameHitch&) = delete;
		ProfilerFrameHitch& operator=(const ProfilerFrameHitch&) = delete;

	public:
		ProfilerFrameHitch() = default;
		virtual ~ProfilerFrameHitch() = default;

		void Install() noexcept;
		[[nodiscard]] bool IsInstalled() const noexcept { return m_installed; }
	};
}
