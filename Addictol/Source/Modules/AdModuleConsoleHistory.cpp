#include <Modules/AdModuleConsoleHistory.h>
#include <AdUtils.h>
#include <AdConsoleHistory.h>
#include <AdConsoleSubsystem.h>

#include <RE/B/ButtonEvent.h>
#include <RE/C/CharacterEvent.h>
#include <RE/C/Console.h>

#include <atomic>
#include <string>

namespace Addictol
{
	static REX::TOML::Bool<> bConsoleHistory{ "Console"sv, "bConsoleHistory"sv, true };

	namespace consoleHistoryDetail
	{
		constexpr std::uint32_t DIK_UP   = 0xC8;
		constexpr std::uint32_t DIK_DOWN = 0xD0;

		// Original ExecuteCommand pointer (post-detour, jumps to engine entrypoint).
		using TExec = void (__fastcall*)(const char*);
		static TExec sOrigExecuteCommand{ nullptr };

		// Recall cursor: -1 means no recall active, else index into history (0=newest).
		static std::atomic<std::int32_t> recallCursor{ -1 };

		// Re-entrancy guard: don't re-process commands that we re-issue ourselves.
		static thread_local bool inDetour{ false };

		static void __fastcall Detour_ExecuteCommand(const char* a_command) noexcept
		{
			if (!a_command) {
				if (sOrigExecuteCommand) sOrigExecuteCommand(a_command);
				return;
			}
			if (inDetour) {
				if (sOrigExecuteCommand) sOrigExecuteCommand(a_command);
				return;
			}

			auto* hist = ConsoleHistory::GetSingleton();
			std::string expanded;
			bool shouldRun = hist->ProcessCommand(std::string_view{ a_command }, expanded);
			if (!shouldRun) {
				// `clear` was handled internally; also clear the on-screen output.
				ConsoleSubsystem::GetSingleton()->ClearOutputText();
				return;
			}

			inDetour = true;
			if (sOrigExecuteCommand) sOrigExecuteCommand(expanded.c_str());
			inDetour = false;
		}

		static bool HandleButton(
			[[maybe_unused]] RE::BSInputEventUser* a_self,
			const RE::ButtonEvent* a_event) noexcept
		{
			if (!a_event) return false;
			const std::uint32_t scan = static_cast<std::uint32_t>(a_event->idCode);
			if (scan != DIK_UP && scan != DIK_DOWN) return false;
			if (!a_event->QJustPressed() && !a_event->QHeldDown()) return false;

			auto* hist = ConsoleHistory::GetSingleton();
			const std::size_t size = hist->Size();
			if (size == 0) return true;  // swallow but no-op

			std::int32_t cur = recallCursor.load(std::memory_order_relaxed);
			std::int32_t next = cur;

			if (scan == DIK_UP) {
				next = (cur < 0) ? 0 : std::min<std::int32_t>(cur + 1, static_cast<std::int32_t>(size) - 1);
			} else {
				if (cur <= 0) {
					next = -1;
				} else {
					next = cur - 1;
				}
			}
			recallCursor.store(next, std::memory_order_relaxed);

			auto* sub = ConsoleSubsystem::GetSingleton();
			if (next < 0) {
				sub->SetInputText("");
				sub->SetInputSelection(0, 0);
			} else {
				std::string entry;
				if (hist->GetEntry(static_cast<std::size_t>(next), entry)) {
					sub->SetInputText(entry);
					std::int32_t end = static_cast<std::int32_t>(entry.size());
					sub->SetInputSelection(end, end);
				}
			}
			return true;
		}

		// Any input-mutating event invalidates the cursor (next Up starts fresh from newest).
		static bool HandleChar(
			[[maybe_unused]] RE::BSInputEventUser* a_self,
			const RE::CharacterEvent* a_event) noexcept
		{
			if (!a_event) return false;
			if (a_event->charCode >= 0x20 || a_event->charCode == 0x08 || a_event->charCode == 0x7F) {
				recallCursor.store(-1, std::memory_order_relaxed);
			}
			return false;
		}

		static void HandleOpenClose(bool a_opening) noexcept
		{
			recallCursor.store(-1, std::memory_order_relaxed);
			if (!a_opening) {
				ConsoleHistory::GetSingleton()->RequestFlush();
			}
		}
	}

	ModuleConsoleHistory::ModuleConsoleHistory() :
		Module("Console History", &bConsoleHistory)
	{}

	bool ModuleConsoleHistory::DoQuery() const noexcept
	{
		return RE::VTABLE::Console[1].address() != 0;
	}

	bool ModuleConsoleHistory::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace consoleHistoryDetail;

		// Init the service (loads aliases + on-disk history + spins worker).
		ConsoleHistory::GetSingleton()->Init();

		// Detour ExecuteCommand entrypoint.
		const auto target = REL::Relocation<std::uintptr_t>{ RE::ID::Console::ExecuteCommand }.address();
		if (!target) {
			REX::ERROR("Console History: ExecuteCommand address resolution failed"sv);
			return false;
		}
		sOrigExecuteCommand = reinterpret_cast<TExec>(
			RELEX::DetourJump(target, reinterpret_cast<std::uintptr_t>(&Detour_ExecuteCommand)));
		if (!sOrigExecuteCommand) {
			REX::ERROR("Console History: DetourJump on ExecuteCommand failed"sv);
			return false;
		}

		// Register Up/Down recall + cursor invalidation + flush-on-close.
		auto* sub = ConsoleSubsystem::GetSingleton();
		sub->AddButtonCallback(&HandleButton);
		sub->AddCharCallback(&HandleChar);
		sub->AddOpenCloseCallback(&HandleOpenClose);

		REX::INFO("Console History: installed (Up/Down recall, alias expansion, persistence)"sv);
		return true;
	}

	bool ModuleConsoleHistory::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleConsoleHistory::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
