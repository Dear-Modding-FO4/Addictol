#include <DearModdingUI/CursorLoader.h>
#include <DearModdingUI/VisualDecisions.h>

#include <Windows.h>

#include <imgui/imgui.h>

namespace Addictol::DearModdingUI::CursorLoader
{
	namespace
	{
		HWND g_window{ nullptr };
		bool g_owned{ false };

		void RestoreGameCursor() noexcept
		{
			if (!g_window)
				return;
			SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
			SendMessageW(
				g_window,
				WM_SETCURSOR,
				reinterpret_cast<WPARAM>(g_window),
				MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
		}
	}

	void Initialize(void* a_window) noexcept
	{
		g_window = static_cast<HWND>(a_window);
		g_owned = false;
	}

	void PrepareFrame(
		bool a_modalVisible) noexcept
	{
		const auto cursor = DecideCursorPresentation(a_modalVisible);
		ImGui::GetIO().MouseDrawCursor = cursor.drawSoftwareCursor;

		switch (DecideCursorTransition(g_owned, cursor.hideOperatingSystemCursor))
		{
		case CursorOwnershipTransition::kAcquire:
			SetCursor(nullptr);
			g_owned = true;
			break;
		case CursorOwnershipTransition::kRelease:
			g_owned = false;
			RestoreGameCursor();
			break;
		default:
			break;
		}
	}

	bool HandleWindowMessage(uint32_t a_message, uint64_t a_lparam) noexcept
	{
		if (!g_owned ||
			a_message != WM_SETCURSOR ||
			LOWORD(a_lparam) != HTCLIENT)
			return false;
		SetCursor(nullptr);
		return true;
	}

	void Shutdown() noexcept
	{
		if (ImGui::GetCurrentContext())
			ImGui::GetIO().MouseDrawCursor = false;
		if (g_owned)
		{
			g_owned = false;
			RestoreGameCursor();
		}
		g_window = nullptr;
	}
}
