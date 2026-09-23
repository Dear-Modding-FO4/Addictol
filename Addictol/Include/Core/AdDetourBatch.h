#pragma once

#include <cstdint>
#include <span>

namespace RELEX
{
	struct DetourTarget
	{
		void* original{};
		void* replacement{};
		std::span<const uint8_t> expected;
	};

	struct DetourBatchResult
	{
		uint32_t error{};
		size_t target{};
		explicit operator bool() const noexcept { return error == 0; }
	};

	DetourBatchResult DetourBatch(std::span<DetourTarget> a_targets) noexcept;
}
