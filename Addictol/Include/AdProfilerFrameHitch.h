#pragma once

#include <REX/REX.h>

#include <cstdint>

namespace Addictol
{
	struct FrameTick
	{
		std::uint64_t sequence;
		std::uint64_t endQpc;
		std::uint64_t elapsedQpc;
		double frameMs;
	};

	using FrameTickCallback = void (*)(const FrameTick& a_tick) noexcept;

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
		// Failed installation can leave the hook live; register first and keep callback state valid until exit.
		[[nodiscard]] static bool RegisterFrameTick(FrameTickCallback a_callback) noexcept;
		[[nodiscard]] static bool HasFrameTickSubscribers() noexcept;
		[[nodiscard]] bool IsInstalled() const noexcept { return m_installed; }
	};
}
