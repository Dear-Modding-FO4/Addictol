#include <Modules/AdModuleConsoleAutocomplete.h>
#include <AdUtils.h>
#include <AdConsoleSubsystem.h>

#include <RE/B/ButtonEvent.h>
#include <RE/C/CharacterEvent.h>
#include <RE/S/SCRIPT_FUNCTION.h>

#include <mutex>
#include <string>
#include <vector>

namespace Addictol
{
	static REX::TOML::Bool<> bConsoleAutocomplete{ "Console"sv, "bConsoleAutocomplete"sv, true };

	namespace consoleAutoDetail
	{
		constexpr std::uint32_t DIK_TAB    = 0x0F;
		constexpr std::uint32_t DIK_LSHIFT = 0x2A;
		constexpr std::uint32_t DIK_RSHIFT = 0x36;

		// First Tab seeds prefix + candidate list; each subsequent Tab cycles; any other input invalidates.
		static std::mutex             stateMutex;
		static bool                   cycling{ false };
		static std::string            prefix;
		static std::vector<std::string> candidates;
		static std::size_t            index{ 0 };

		static void Invalidate() noexcept
		{
			std::lock_guard lk{ stateMutex };
			cycling = false;
			prefix.clear();
			candidates.clear();
			index = 0;
		}

		static bool BuildCandidates(std::string_view a_prefix, std::vector<std::string>& a_out) noexcept
		{
			auto funcs = RE::SCRIPT_FUNCTION::GetConsoleFunctions();
			a_out.reserve(64);
			for (const auto& f : funcs) {
				const char* fn = f.functionName;
				const char* sn = f.shortName;
				if (!fn || !*fn) continue;
				const auto pLen = static_cast<int>(a_prefix.size());
				bool match = (_strnicmp(fn, a_prefix.data(), pLen) == 0)
				          || (sn && *sn && _strnicmp(sn, a_prefix.data(), pLen) == 0);
				if (!match) continue;
				bool dup = false;
				for (const auto& s : a_out) {
					if (_stricmp(s.c_str(), fn) == 0) { dup = true; break; }
				}
				if (!dup) a_out.emplace_back(fn);
			}
			return !a_out.empty();
		}

		// Returns [start, end) of the non-space run ending at the caret.
		static std::pair<std::int32_t, std::int32_t> CurrentToken(
			const std::string& a_text,
			std::int32_t a_caret) noexcept
		{
			std::int32_t end = std::clamp<std::int32_t>(a_caret, 0, static_cast<std::int32_t>(a_text.size()));
			std::int32_t start = end;
			while (start > 0 && !std::isspace(static_cast<unsigned char>(a_text[start - 1]))) --start;
			return { start, end };
		}

		static bool HandleTab() noexcept
		{
			auto* sub = ConsoleSubsystem::GetSingleton();
			std::string txt;
			if (!sub->GetInputText(txt)) return false;
			std::int32_t a = 0, e = 0;
			sub->GetInputSelection(a, e);

			std::lock_guard lk{ stateMutex };
			if (!cycling) {
				auto [tokStart, tokEnd] = CurrentToken(txt, e);
				std::string seed = txt.substr(static_cast<std::size_t>(tokStart),
				                              static_cast<std::size_t>(tokEnd - tokStart));
				if (seed.empty()) return true;
				prefix = seed;
				candidates.clear();
				if (!BuildCandidates(prefix, candidates)) {
					cycling = false;
					return true;
				}
				cycling = true;
				index = 0;
			} else {
				if (candidates.empty()) { cycling = false; return true; }
				index = (index + 1) % candidates.size();
			}

			auto [tokStart, tokEnd] = CurrentToken(txt, e);
			const std::string& replacement = candidates[index];
			std::string next = txt.substr(0, static_cast<std::size_t>(tokStart))
			                 + replacement
			                 + txt.substr(static_cast<std::size_t>(tokEnd));
			sub->SetInputText(next);
			std::int32_t newCaret = tokStart + static_cast<std::int32_t>(replacement.size());
			sub->SetInputSelection(newCaret, newCaret);
			return true;
		}

		static bool HandleChar(
			[[maybe_unused]] RE::BSInputEventUser* a_self,
			const RE::CharacterEvent* a_event) noexcept
		{
			if (!a_event) return false;
			// Swallow Tab so \t never lands in the buffer; any other printable invalidates the cycle.
			if (a_event->charCode == 0x09) return true;
			if (a_event->charCode >= 0x20) {
				Invalidate();
			}
			return false;
		}

		static bool HandleButton(
			[[maybe_unused]] RE::BSInputEventUser* a_self,
			const RE::ButtonEvent* a_event) noexcept
		{
			if (!a_event) return false;
			const std::uint32_t scan = static_cast<std::uint32_t>(a_event->idCode);

			if (scan == DIK_TAB) {
				if (a_event->QJustPressed()) return HandleTab();
				return a_event->QHeldDown() || a_event->QPressed();  // swallow up too
			}

			// Shift is part of the same cycle (Shift+Tab still cycles forward per scope-gate).
			if (scan == DIK_LSHIFT || scan == DIK_RSHIFT) return false;

			// Anything else invalidates.
			if (a_event->QJustPressed()) {
				Invalidate();
			}
			return false;
		}

		static void HandleOpenClose(bool a_opening) noexcept
		{
			if (!a_opening) Invalidate();
		}
	}

	ModuleConsoleAutocomplete::ModuleConsoleAutocomplete() :
		Module("Console Autocomplete", &bConsoleAutocomplete)
	{}

	bool ModuleConsoleAutocomplete::DoQuery() const noexcept
	{
		if (RE::VTABLE::Console[1].address() == 0) return false;
		auto funcs = RE::SCRIPT_FUNCTION::GetConsoleFunctions();
		return !funcs.empty();
	}

	bool ModuleConsoleAutocomplete::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		auto* sub = ConsoleSubsystem::GetSingleton();
		sub->AddCharCallback(&consoleAutoDetail::HandleChar);
		sub->AddButtonCallback(&consoleAutoDetail::HandleButton);
		sub->AddOpenCloseCallback(&consoleAutoDetail::HandleOpenClose);
		REX::INFO("Console Autocomplete: callbacks registered (Tab cycle, {} commands)"sv,
			RE::SCRIPT_FUNCTION::GetConsoleFunctions().size());
		return true;
	}

	bool ModuleConsoleAutocomplete::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleConsoleAutocomplete::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
