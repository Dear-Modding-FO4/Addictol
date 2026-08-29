#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Addictol::DearModdingUI
{
	[[nodiscard]] inline float ResolveUiScale(
		float a_dpiScale,
		uint32_t a_backBufferHeight) noexcept
	{
		const auto dpi = std::isfinite(a_dpiScale) && a_dpiScale > 0.0f ?
			a_dpiScale :
			1.0f;
		const auto resolution = a_backBufferHeight ?
			static_cast<float>(a_backBufferHeight) / 1080.0f :
			1.0f;
		return std::clamp(std::max(dpi, resolution), 0.85f, 2.5f);
	}
}
