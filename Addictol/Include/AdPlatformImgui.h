#pragma once

#include <AdImguiPlatformTargets.h>

#include <string_view>

namespace Addictol
{
	using PlatformImguiDrawSink = void (*)() noexcept;
	using PlatformImguiToggleSink = void (*)(uint32_t a_virtualKey) noexcept;

	// One engine detour plus a window subclass, both permanent: teardown order at process exit makes removal unsafe.
	namespace PlatformImgui
	{
		// Sinks are permanent and must register from a load-stage module install.
		[[nodiscard]] bool RegisterDrawSink(std::string_view a_name, PlatformImguiDrawSink a_sink) noexcept;
		// Toggle sinks are global and receive fresh key presses even while ImGui captures keyboard input.
		[[nodiscard]] bool RegisterToggleSink(std::string_view a_name, PlatformImguiToggleSink a_sink) noexcept;

		// Clients call this after load-stage registration, before rendering starts.
		[[nodiscard]] bool InstallHooks() noexcept;

		// Called at kGameLoaded, after the render window and device exist.
		[[nodiscard]] bool InitializeWindow() noexcept;

		void SetDrawingEnabled(bool a_enabled) noexcept;

		[[nodiscard]] bool IsDrawingEnabled() noexcept;
		[[nodiscard]] bool IsReady() noexcept;
		[[nodiscard]] ImguiPlatform::InstallState GetInstallState() noexcept;
	}
}
