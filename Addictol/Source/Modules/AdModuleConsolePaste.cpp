#include <Modules/AdModuleConsolePaste.h>
#include <AdUtils.h>
#include <AdConsoleSubsystem.h>

#include <RE/B/ButtonEvent.h>
#include <RE/C/CharacterEvent.h>

#include <Windows.h>
#include <atomic>

namespace Addictol
{
	static REX::TOML::Bool<> bConsolePaste{ "Console"sv, "bConsolePaste"sv, true };

	namespace consolePasteDetail
	{
		// DIK scancodes (DirectInput / Bethesda keymap)
		constexpr std::uint32_t DIK_LCONTROL = 0x1D;
		constexpr std::uint32_t DIK_RCONTROL = 0x9D;
		constexpr std::uint32_t DIK_LSHIFT   = 0x2A;
		constexpr std::uint32_t DIK_RSHIFT   = 0x36;
		constexpr std::uint32_t DIK_V        = 0x2F;
		constexpr std::uint32_t DIK_C        = 0x2E;
		constexpr std::uint32_t DIK_X        = 0x2D;
		constexpr std::uint32_t DIK_L        = 0x26;
		constexpr std::uint32_t DIK_LEFT     = 0xCB;
		constexpr std::uint32_t DIK_RIGHT    = 0xCD;

		// Modifier shadows. Atomic because input events can fire from any
		// thread that walks the IMenu vtable; in practice this is the main
		// UI thread, but defensive.
		static std::atomic<bool> ctrlDown{ false };
		static std::atomic<bool> shiftDown{ false };

		// Re-sync against the OS to defend against missed key-up while
		// Alt-Tab'd away from the window. Cheap; called on combo trigger.
		static void ResyncModifiers() noexcept
		{
			ctrlDown.store((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0, std::memory_order_relaxed);
			shiftDown.store((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0, std::memory_order_relaxed);
		}

		static std::string ReadClipboardUtf8() noexcept
		{
			if (!OpenClipboard(nullptr)) return {};
			std::string out;
			HANDLE h = GetClipboardData(CF_UNICODETEXT);
			if (h) {
				if (auto* w = static_cast<const wchar_t*>(GlobalLock(h))) {
					int srcLen = static_cast<int>(wcslen(w));
					int needed = WideCharToMultiByte(CP_UTF8, 0, w, srcLen, nullptr, 0, nullptr, nullptr);
					if (needed > 0) {
						out.resize(static_cast<std::size_t>(needed));
						WideCharToMultiByte(CP_UTF8, 0, w, srcLen, out.data(), needed, nullptr, nullptr);
					}
					GlobalUnlock(h);
				}
			}
			CloseClipboard();
			// Strip CR/LF - console is single-line. Also strip tab.
			std::string filtered;
			filtered.reserve(out.size());
			for (char c : out) {
				if (c != '\r' && c != '\n' && c != '\t') filtered.push_back(c);
			}
			return filtered;
		}

		static bool WriteClipboardUtf8(std::string_view a_utf8) noexcept
		{
			if (!OpenClipboard(nullptr)) return false;
			bool ok = false;
			if (EmptyClipboard()) {
				int srcLen = static_cast<int>(a_utf8.size());
				int needed = MultiByteToWideChar(CP_UTF8, 0, a_utf8.data(), srcLen, nullptr, 0);
				if (needed >= 0) {
					HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (needed + 1) * sizeof(wchar_t));
					if (h) {
						if (auto* w = static_cast<wchar_t*>(GlobalLock(h))) {
							if (needed > 0)
								MultiByteToWideChar(CP_UTF8, 0, a_utf8.data(), srcLen, w, needed);
							w[needed] = L'\0';
							GlobalUnlock(h);
							if (SetClipboardData(CF_UNICODETEXT, h) != nullptr) {
								ok = true;
							} else {
								GlobalFree(h);
							}
						} else {
							GlobalFree(h);
						}
					}
				}
			}
			CloseClipboard();
			return ok;
		}

		// Word-jump: walk caret to next boundary in given direction.
		// Boundary = transition between whitespace and non-whitespace.
		// Multi-character whitespace runs collapse to a single boundary.
		static std::int32_t WalkLeft(const std::string& a_text, std::int32_t a_caret) noexcept
		{
			std::int32_t i = std::clamp<std::int32_t>(a_caret, 0, static_cast<std::int32_t>(a_text.size()));
			if (i == 0) return 0;
			while (i > 0 && std::isspace(static_cast<unsigned char>(a_text[i - 1]))) --i;
			while (i > 0 && !std::isspace(static_cast<unsigned char>(a_text[i - 1]))) --i;
			return i;
		}

		static std::int32_t WalkRight(const std::string& a_text, std::int32_t a_caret) noexcept
		{
			const std::int32_t n = static_cast<std::int32_t>(a_text.size());
			std::int32_t i = std::clamp<std::int32_t>(a_caret, 0, n);
			if (i >= n) return n;
			while (i < n && !std::isspace(static_cast<unsigned char>(a_text[i]))) ++i;
			while (i < n &&  std::isspace(static_cast<unsigned char>(a_text[i]))) ++i;
			return i;
		}

		// Action helpers - return true if action ran (so we know to swallow event).
		static bool DoPaste() noexcept
		{
			auto* sub = ConsoleSubsystem::GetSingleton();
			std::string txt;
			if (!sub->GetInputText(txt)) return false;
			std::int32_t a = 0, e = 0;
			sub->GetInputSelection(a, e);
			if (a > e) std::swap(a, e);
			a = std::clamp<std::int32_t>(a, 0, static_cast<std::int32_t>(txt.size()));
			e = std::clamp<std::int32_t>(e, 0, static_cast<std::int32_t>(txt.size()));

			auto clip = ReadClipboardUtf8();
			if (clip.empty()) return true;

			std::string next = txt.substr(0, a) + clip + txt.substr(e);
			sub->SetInputText(next);
			std::int32_t newCaret = a + static_cast<std::int32_t>(clip.size());
			sub->SetInputSelection(newCaret, newCaret);
			return true;
		}

		static bool DoCopy(bool a_cut) noexcept
		{
			auto* sub = ConsoleSubsystem::GetSingleton();
			std::string txt;
			if (!sub->GetInputText(txt)) return false;
			std::int32_t a = 0, e = 0;
			sub->GetInputSelection(a, e);
			if (a > e) std::swap(a, e);
			a = std::clamp<std::int32_t>(a, 0, static_cast<std::int32_t>(txt.size()));
			e = std::clamp<std::int32_t>(e, 0, static_cast<std::int32_t>(txt.size()));
			// If nothing is selected, copy the full input.
			std::string_view sel = (a == e)
				? std::string_view{ txt }
				: std::string_view{ txt }.substr(static_cast<std::size_t>(a), static_cast<std::size_t>(e - a));
			if (sel.empty()) return true;
			WriteClipboardUtf8(sel);
			if (a_cut) {
				if (a == e) {
					sub->SetInputText("");
					sub->SetInputSelection(0, 0);
				} else {
					std::string next = txt.substr(0, a) + txt.substr(e);
					sub->SetInputText(next);
					sub->SetInputSelection(a, a);
				}
			}
			return true;
		}

		static bool DoClear() noexcept
		{
			auto* sub = ConsoleSubsystem::GetSingleton();
			sub->SetInputText("");
			sub->SetInputSelection(0, 0);
			return true;
		}

		static bool DoWordJump(bool a_right, bool a_shiftSelect) noexcept
		{
			auto* sub = ConsoleSubsystem::GetSingleton();
			std::string txt;
			if (!sub->GetInputText(txt)) return false;
			std::int32_t a = 0, e = 0;
			sub->GetInputSelection(a, e);
			std::int32_t newPos = a_right ? WalkRight(txt, e) : WalkLeft(txt, e);
			if (a_shiftSelect) {
				sub->SetInputSelection(a, newPos);
			} else {
				sub->SetInputSelection(newPos, newPos);
			}
			return true;
		}

		static bool HandleChar(
			[[maybe_unused]] RE::BSInputEventUser* a_self,
			const RE::CharacterEvent* a_event) noexcept
		{
			if (!a_event) return false;
			ResyncModifiers();
			if (!ctrlDown.load(std::memory_order_relaxed)) return false;
			switch (a_event->charCode) {
			case 0x16: return DoPaste();          // Ctrl+V
			case 0x03: return DoCopy(false);      // Ctrl+C
			case 0x18: return DoCopy(true);       // Ctrl+X
			case 0x0C: return DoClear();          // Ctrl+L
			default:   return false;
			}
		}

		static bool HandleButton(
			[[maybe_unused]] RE::BSInputEventUser* a_self,
			const RE::ButtonEvent* a_event) noexcept
		{
			if (!a_event) return false;
			const std::uint32_t scan = static_cast<std::uint32_t>(a_event->idCode);

			// Modifier shadow updates (NEVER swallow these - other code paths need them).
			if (scan == DIK_LCONTROL || scan == DIK_RCONTROL) {
				ctrlDown.store(a_event->QPressed(), std::memory_order_relaxed);
				return false;
			}
			if (scan == DIK_LSHIFT || scan == DIK_RSHIFT) {
				shiftDown.store(a_event->QPressed(), std::memory_order_relaxed);
				return false;
			}

			if (!a_event->QJustPressed() && !a_event->QHeldDown()) return false;

			// Ctrl+combo only.
			const bool ctrl = ctrlDown.load(std::memory_order_relaxed)
			               || (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
			if (!ctrl) return false;

			if (a_event->QJustPressed()) {
				switch (scan) {
				case DIK_V: return DoPaste();
				case DIK_C: return DoCopy(false);
				case DIK_X: return DoCopy(true);
				case DIK_L: return DoClear();
				default: break;
				}
			}

			// Word jump - allow held repeat.
			if (scan == DIK_LEFT || scan == DIK_RIGHT) {
				const bool shift = shiftDown.load(std::memory_order_relaxed)
				                || (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
				return DoWordJump(scan == DIK_RIGHT, shift);
			}

			return false;
		}

		static void HandleOpenClose(bool a_opening) noexcept
		{
			if (a_opening) {
				ctrlDown.store(false, std::memory_order_relaxed);
				shiftDown.store(false, std::memory_order_relaxed);
			}
		}
	}

	ModuleConsolePaste::ModuleConsolePaste() :
		Module("Console Paste", &bConsolePaste)
	{}

	bool ModuleConsolePaste::DoQuery() const noexcept
	{
		return RE::VTABLE::Console[1].address() != 0;
	}

	bool ModuleConsolePaste::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		auto* sub = ConsoleSubsystem::GetSingleton();
		sub->AddCharCallback(&consolePasteDetail::HandleChar);
		sub->AddButtonCallback(&consolePasteDetail::HandleButton);
		sub->AddOpenCloseCallback(&consolePasteDetail::HandleOpenClose);
		REX::INFO("Console Paste: callbacks registered (Ctrl+V/C/X/L, Ctrl+Left/Right)"sv);
		return true;
	}

	bool ModuleConsolePaste::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleConsolePaste::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
