#include <Zlib/AdZlibInstallation.h>
#include <REX/REX.h>
#include <atomic>

namespace Addictol
{
	void LogInvalidOwnedZlibState() noexcept
	{
		static std::atomic_flag warned{};
		if (!warned.test_and_set(std::memory_order_relaxed))
			REX::ERROR("Owned zlib: rejected a tagged state with an invalid owner or decoder identity.");
	}
}
