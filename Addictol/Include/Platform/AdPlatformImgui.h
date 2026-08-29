#pragma once

#include <Platform/AdImguiPlatformTargets.h>

#include <string_view>

struct IDXGISwapChain;

namespace Addictol
{
	using PlatformImguiDrawSink = void (*)() noexcept;
	using PlatformImguiToggleSink = bool (*)(uint32_t a_virtualKey) noexcept;
	using PlatformImguiSetupSink = void (*)(void* a_window) noexcept;

	// The IAT, vtable, and window hooks stay installed because process-exit teardown order is unsafe.
	namespace PlatformImgui
	{
		// Sinks are permanent and must register from a load-stage module install.
		[[nodiscard]] bool RegisterDrawSink(std::string_view a_name, PlatformImguiDrawSink a_sink) noexcept;
		// A toggle sink returns true to consume that press, its repeats, and its matching release.
		[[nodiscard]] bool RegisterToggleSink(std::string_view a_name, PlatformImguiToggleSink a_sink) noexcept;
		// Setup sinks configure the fresh context on the render thread, before any backend or font upload.
		[[nodiscard]] bool RegisterSetupSink(std::string_view a_name, PlatformImguiSetupSink a_sink) noexcept;

		// Clients call this after load-stage registration, before rendering starts.
		[[nodiscard]] bool InstallHooks() noexcept;

		// Called at kGameLoaded, after the render window exists.
		[[nodiscard]] bool InitializeWindow() noexcept;

		// Internal handoff point for a final or proxy game swapchain.
		[[nodiscard]] bool AttachSwapChain(IDXGISwapChain* a_swapChain) noexcept;

		void SetDrawingEnabled(bool a_enabled) noexcept;

		[[nodiscard]] bool IsDrawingEnabled() noexcept;
		[[nodiscard]] bool IsReady() noexcept;
		[[nodiscard]] bool QueryVideoMemory(uint64_t& a_used, uint64_t& a_budget) noexcept;
		[[nodiscard]] ImguiPlatform::InstallState GetInstallState() noexcept;
		[[nodiscard]] std::string GetConfigurePath() noexcept;
	}
}