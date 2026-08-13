#pragma once

#include <string>

#include <REX/REX.h>
#include <F4SE/F4SE.h>

namespace Addictol
{
	using namespace std::literals;

	// Install during preload so the hook precedes F4SE plugin Load dispatch.
	// F4SE processes plugins sequentially on the main thread, so hook state needs no locking.
	class ProfilerDLL :
		public REX::Singleton<ProfilerDLL>
	{
		bool m_installed{ false };

		ProfilerDLL(const ProfilerDLL&) = delete;
		ProfilerDLL& operator=(const ProfilerDLL&) = delete;
	public:
		ProfilerDLL() = default;
		virtual ~ProfilerDLL() = default;

		// A reinterpreted PreLoadInterface* is valid here because only its address is used.
		void Install(const F4SE::LoadInterface* a_f4se) noexcept;

		[[nodiscard]] bool IsInstalled() const noexcept { return m_installed; }

		// Returns an empty string on failure.
		[[nodiscard]] static std::string GetFileVersionString(const char* a_path) noexcept;
	};
}
