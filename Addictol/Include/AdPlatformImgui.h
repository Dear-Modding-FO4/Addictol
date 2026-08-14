#pragma once

#include <REX/W32.h>

namespace Addictol
{
	struct PlatformImguiContext
	{
		REX::W32::HWND hwnd{ nullptr };
		REX::W32::ID3D11Device* device{ nullptr };
		REX::W32::ID3D11DeviceContext* deviceContext{ nullptr };
		REX::W32::RECT windowRect{};
		void* imguiContext{ nullptr };
	};

	using PlatformImguiDrawEventSink = void(*)(void);

	class PlatformImgui :
		public REX::TSingleton<PlatformImgui>
	{
		bool initMain{ false };
		bool initHooks{ false };
		PlatformImguiContext context{};
		std::unordered_map<std::string, PlatformImguiDrawEventSink> DrawBeforeCursorHandlers{};
		std::unordered_map<std::string, PlatformImguiDrawEventSink> DrawOverlappHandlers{};

		static void RefreshCursor() noexcept;

		static void KillWindow(uint32_t a_unk) noexcept;
		inline static decltype(&KillWindow) KillWindowOrig{ nullptr };
		static void WindowSizeChanged(uint32_t a_unk) noexcept;
		inline static decltype(&WindowSizeChanged) WindowSizeChangedOrig{ nullptr };
		static void UIBeforeCursorEndFrame(void* a_UI) noexcept;
		inline static decltype(&UIBeforeCursorEndFrame) UIBeforeCursorEndFrameOrig{ nullptr };
		static void UIEndFrame() noexcept;
		inline static decltype(&UIEndFrame) UIEndFrameOrig{ nullptr };
		static uint64_t WindowProc(REX::W32::HWND a_hwnd, uint32_t a_msg, uint64_t a_wparam, uint64_t a_lparam) noexcept;
		inline static decltype(&WindowProc) WindowProcOrig{ nullptr };

		PlatformImgui(const PlatformImgui&) = delete;
		PlatformImgui(PlatformImgui&&) = delete;
		PlatformImgui operator=(PlatformImgui&&) = delete;
		PlatformImgui operator=(const PlatformImgui&) = delete;
	public:
		constexpr PlatformImgui() noexcept = default;
		virtual ~PlatformImgui() noexcept;

		[[nodiscard]] virtual bool InitHooks() noexcept;
		[[nodiscard]] virtual bool InitSDM() noexcept;
		virtual void KillHooks() noexcept;
		virtual void KillSDM() noexcept;

		[[nodiscard]] inline virtual const PlatformImguiContext& GetContext() const noexcept { return context; }
	};
}