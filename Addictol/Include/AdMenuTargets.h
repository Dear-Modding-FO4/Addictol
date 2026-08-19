#pragma once

#include "AdLogControl.h"
#include "AdRegistration.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Addictol
{
	using namespace std::literals;

	inline constexpr std::array kMenuLogLevels{
		LogControl::Level::kTrace,
		LogControl::Level::kDebug,
		LogControl::Level::kInfo,
		LogControl::Level::kWarn,
		LogControl::Level::kError,
		LogControl::Level::kCritical,
		LogControl::Level::kOff
	};
	static_assert(
		kMenuLogLevels.size() ==
		static_cast<size_t>(LogControl::Level::kOff) + 1);

	struct MenuToggleKey
	{
		uint32_t virtualKey{ 0 };
		bool recognized{ false };
	};

	inline constexpr uint32_t kMenuDefaultToggleKey{ 0x7A };

	struct MenuKeyName
	{
		std::string_view name;
		uint32_t virtualKey;
	};

	// the game owns every other key
	inline constexpr std::array kMenuToggleKeys{
		MenuKeyName{ "F1"sv, 0x70 },
		MenuKeyName{ "F2"sv, 0x71 },
		MenuKeyName{ "F3"sv, 0x72 },
		MenuKeyName{ "F4"sv, 0x73 },
		MenuKeyName{ "F5"sv, 0x74 },
		MenuKeyName{ "F6"sv, 0x75 },
		MenuKeyName{ "F7"sv, 0x76 },
		MenuKeyName{ "F8"sv, 0x77 },
		MenuKeyName{ "F9"sv, 0x78 },
		MenuKeyName{ "F10"sv, 0x79 },
		MenuKeyName{ "F11"sv, 0x7A },
		MenuKeyName{ "F12"sv, 0x7B },
		MenuKeyName{ "Home"sv, 0x24 },
		MenuKeyName{ "End"sv, 0x23 },
		MenuKeyName{ "Insert"sv, 0x2D },
		MenuKeyName{ "Delete"sv, 0x2E }
	};

	[[nodiscard]] constexpr char AsciiUpper(char a_character) noexcept
	{
		return a_character >= 'a' && a_character <= 'z' ?
			static_cast<char>(a_character - ('a' - 'A')) :
			a_character;
	}

	[[nodiscard]] constexpr bool EqualsIgnoringCase(std::string_view a_left, std::string_view a_right) noexcept
	{
		if (a_left.size() != a_right.size())
			return false;
		for (size_t index = 0; index < a_left.size(); ++index)
		{
			if (AsciiUpper(a_left[index]) != AsciiUpper(a_right[index]))
				return false;
		}
		return true;
	}

	// a typo must not silently disable the toggle
	[[nodiscard]] constexpr MenuToggleKey ParseMenuToggleKey(std::string_view a_name) noexcept
	{
		for (const auto& key : kMenuToggleKeys)
		{
			if (EqualsIgnoringCase(a_name, key.name))
				return { key.virtualKey, true };
		}
		return { kMenuDefaultToggleKey, false };
	}

	[[nodiscard]] constexpr std::string_view MenuToggleKeyName(uint32_t a_virtualKey) noexcept
	{
		for (const auto& key : kMenuToggleKeys)
		{
			if (key.virtualKey == a_virtualKey)
				return key.name;
		}
		return "Unknown"sv;
	}

	inline constexpr uint32_t kMenuMinRefreshMs{ 100 };
	inline constexpr uint32_t kMenuMaxRefreshMs{ 2000 };

	[[nodiscard]] constexpr uint32_t ClampMenuRefreshMs(uint32_t a_refreshMs) noexcept
	{
		if (a_refreshMs < kMenuMinRefreshMs)
			return kMenuMinRefreshMs;
		if (a_refreshMs > kMenuMaxRefreshMs)
			return kMenuMaxRefreshMs;
		return a_refreshMs;
	}

	[[nodiscard]] constexpr bool ShouldRefreshPanel(
		bool a_hasData,
		uint64_t a_nowQpc,
		uint64_t a_refreshedAtQpc,
		uint64_t a_qpcFrequency,
		uint32_t a_refreshMs) noexcept
	{
		if (!a_hasData || !a_qpcFrequency)
			return true;
		if (a_nowQpc <= a_refreshedAtQpc)
			return false;

		const auto elapsedMs =
			((a_nowQpc - a_refreshedAtQpc) * 1000ull) / a_qpcFrequency;
		return elapsedMs >= ClampMenuRefreshMs(a_refreshMs);
	}

	[[nodiscard]] constexpr size_t ClampMenuFormattedLength(
		int a_written,
		size_t a_capacity) noexcept
	{
		if (a_written <= 0 || !a_capacity)
			return 0;

		const auto written = static_cast<size_t>(a_written);
		return written < a_capacity ? written : a_capacity - 1;
	}

	struct MenuToggleDecision
	{
		bool matched{ false };
		bool open{ false };
	};

	// failed backend disables drawing
	[[nodiscard]] constexpr MenuToggleDecision DecideMenuToggle(
		uint32_t a_virtualKey,
		uint32_t a_toggleKey,
		bool a_open,
		bool a_drawingEnabled) noexcept
	{
		if (a_virtualKey != a_toggleKey)
			return { false, a_open };
		return { true, !(a_open && a_drawingEnabled) };
	}

	using MenuPanelDraw = void (*)(const void*) noexcept;

	inline constexpr size_t kMenuPanelCapacity = 16;

	// the pure layer must not name config types
	template <class Gate>
	class MenuPanelTable
	{
	public:
		struct Entry
		{
			char name[kNameCapacity]{};
			MenuPanelDraw draw{ nullptr };
			const Gate* gate{ nullptr };
			const void* context{ nullptr };
		};

		Registration Add(
			std::string_view a_name,
			MenuPanelDraw a_draw,
			const Gate* a_gate,
			const void* a_context = nullptr) noexcept
		{
			if (!m_open.load(std::memory_order_acquire))
				return Registration::kClosed;
			if (!a_draw)
				return Registration::kNullCallback;
			if (!ValidRegistrationName(a_name))
				return Registration::kInvalidName;

			const auto count = m_count.load(std::memory_order_relaxed);
			for (size_t index = 0; index < count; ++index)
			{
				if (Name(index) == a_name)
					return Registration::kDuplicate;
			}
			if (count == kMenuPanelCapacity)
				return Registration::kFull;

			auto& entry = m_entries[count];
			CopyRegistrationName(entry.name, a_name);
			entry.draw = a_draw;
			entry.gate = a_gate;
			entry.context = a_context;
			m_count.store(count + 1, std::memory_order_release);
			return Registration::kAccepted;
		}

		void Close() noexcept
		{
			m_open.store(false, std::memory_order_release);
		}

		[[nodiscard]] bool IsOpen() const noexcept
		{
			return m_open.load(std::memory_order_acquire);
		}

		[[nodiscard]] size_t Size() const noexcept
		{
			return m_count.load(std::memory_order_acquire);
		}

		[[nodiscard]] bool Empty() const noexcept
		{
			return Size() == 0;
		}

		[[nodiscard]] static constexpr size_t MaxSize() noexcept
		{
			return kMenuPanelCapacity;
		}

		[[nodiscard]] const Entry& At(size_t a_index) const noexcept
		{
			static constexpr Entry empty{};
			return a_index < Size() ? m_entries[a_index] : empty;
		}

	// ImGui labels a tab from const char*
		[[nodiscard]] const char* NameData(size_t a_index) const noexcept
		{
			return At(a_index).name;
		}

		[[nodiscard]] std::string_view Name(size_t a_index) const noexcept
		{
			return std::string_view{ NameData(a_index) };
		}

	private:
		std::array<Entry, kMenuPanelCapacity> m_entries{};
		std::atomic<size_t> m_count{ 0 };
		std::atomic<bool> m_open{ true };
	};

	template <class Gate>
	[[nodiscard]] bool PanelVisible(const Gate* a_gate) noexcept
	{
		return !a_gate || a_gate->GetValue();
	}

	template <class Gate, class Activate, class End>
	void DrawPanels(const MenuPanelTable<Gate>& a_panels, Activate&& a_activate, End&& a_end) noexcept
	{
		for (size_t index = 0, count = a_panels.Size(); index < count; ++index)
		{
			const auto& panel = a_panels.At(index);
			if (!PanelVisible(panel.gate))
				continue;
			if (!a_activate(a_panels.NameData(index)))
				continue;

			panel.draw(panel.context);
			a_end();
		}
	}

	// log control pins last
	template <class Gate, class RequestClient>
	[[nodiscard]] Registration FinalizeMenuPanels(
		MenuPanelTable<Gate>& a_panels,
		std::string_view a_logControlName,
		MenuPanelDraw a_logControlDraw,
		bool a_menuRequested,
		RequestClient&& a_requestClient) noexcept
	{
		const auto result = a_panels.Add(a_logControlName, a_logControlDraw, nullptr);
		a_panels.Close();
		if (a_menuRequested && result == Registration::kAccepted)
			a_requestClient();
		return result;
	}
}
