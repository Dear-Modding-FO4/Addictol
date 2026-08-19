#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <ranges>
#include <string_view>

namespace Addictol::ImguiPlatform
{
	enum class Runtime : uint32_t
	{
		kOG,
		kNG,
		kAE
	};

	struct TargetId
	{
		uint64_t og{ 0 };
		uint64_t ng{ 0 };
		uint64_t ae{ 0 };

		[[nodiscard]] constexpr uint64_t For(Runtime a_runtime) const noexcept
		{
			return a_runtime == Runtime::kOG ? og : a_runtime == Runtime::kNG ? ng : ae;
		}
	};

	// UIEndFrame is the only engine function this provider patches; every id was round tripped per runtime.
	inline constexpr TargetId kUIEndFrameId{ 137303, 2284763, 2284763 };

	[[nodiscard]] constexpr std::string_view Describe(Runtime a_runtime) noexcept
	{
		return a_runtime == Runtime::kOG ? "OG" : a_runtime == Runtime::kNG ? "NG" : "AE";
	}

	[[nodiscard]] constexpr uint64_t UIEndFrameId(Runtime a_runtime) noexcept
	{
		return kUIEndFrameId.For(a_runtime);
	}

	// Each signature runs into runtime specific bytes because the bare prologue repeats across hundreds of functions.
	inline constexpr std::initializer_list<uint8_t> kUIEndFrameSignatureOG{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48,
		0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x15, 0xAF, 0x0C, 0x6F, 0x04, 0x65, 0x48,
		0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xF1, 0x48, 0x8B, 0x3C, 0xD0, 0xB9, 0xC0, 0x09,
		0x00, 0x00 };
	inline constexpr std::initializer_list<uint8_t> kUIEndFrameSignatureNG{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48,
		0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x54, 0x41,
		0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x15, 0xE4, 0x05, 0x35, 0x02, 0x4C, 0x8B, 0xF9 };
	inline constexpr std::initializer_list<uint8_t> kUIEndFrameSignatureAE{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48,
		0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x54, 0x41,
		0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x15, 0x64, 0x0C, 0x3F, 0x02, 0x4C, 0x8B, 0xF9 };

	[[nodiscard]] constexpr const std::initializer_list<uint8_t>& UIEndFrameSignature(Runtime a_runtime) noexcept
	{
		return a_runtime == Runtime::kOG ? kUIEndFrameSignatureOG :
			a_runtime == Runtime::kNG ? kUIEndFrameSignatureNG :
			kUIEndFrameSignatureAE;
	}

	// A candidate of a different length is a mismatch: partial prologue agreement proves nothing.
	[[nodiscard]] constexpr bool MatchesSignature(
		std::ranges::contiguous_range auto&& a_candidate,
		const std::initializer_list<uint8_t>& a_signature) noexcept
	{
		return a_signature.size() != 0 && std::ranges::equal(a_candidate, a_signature);
	}

	enum class InstallState : uint32_t
	{
		kNotAttempted,
		kRejected,
		kAttempted,
		kInstalled,
		kIndeterminate
	};

	[[nodiscard]] constexpr std::string_view Describe(InstallState a_state) noexcept
	{
		switch (a_state)
		{
		case InstallState::kNotAttempted:
			return "not attempted";
		case InstallState::kRejected:
			return "rejected before any write";
		case InstallState::kAttempted:
			return "write attempted";
		case InstallState::kInstalled:
			return "installed";
		default:
			return "indeterminate";
		}
	}

	// A rejected target is never retried: the executable does not change while the process runs.
	[[nodiscard]] constexpr bool AllowsInstallAttempt(InstallState a_state) noexcept
	{
		return a_state == InstallState::kNotAttempted;
	}

	[[nodiscard]] constexpr bool IsInstalled(InstallState a_state) noexcept
	{
		return a_state == InstallState::kInstalled;
	}

	enum class Registration : uint32_t
	{
		kAccepted,
		kNullSink,
		kInvalidName,
		kDuplicate,
		kFull,
		kClosed
	};

	[[nodiscard]] constexpr std::string_view Describe(Registration a_result) noexcept
	{
		switch (a_result)
		{
		case Registration::kAccepted:
			return "accepted";
		case Registration::kNullSink:
			return "callback is null";
		case Registration::kInvalidName:
			return "name is empty or too long";
		case Registration::kDuplicate:
			return "name is already registered";
		case Registration::kFull:
			return "table is full";
		default:
			return "registration is closed";
		}
	}

	inline constexpr size_t kSinkCapacity = 8;
	inline constexpr size_t kSinkNameCapacity = 32;

	// Fixed capacity and name storage keep the draw path allocation free and the names owned.
	template <class Sink, size_t Capacity = kSinkCapacity>
	class SinkTable
	{
	public:
		Registration Add(std::string_view a_name, Sink a_sink) noexcept
		{
			if (!m_open.load(std::memory_order_acquire))
				return Registration::kClosed;
			if (!a_sink)
				return Registration::kNullSink;
			if (a_name.empty() || a_name.size() >= kSinkNameCapacity)
				return Registration::kInvalidName;

			const auto count = m_count.load(std::memory_order_relaxed);
			for (size_t index = 0; index < count; ++index)
			{
				if (Name(index) == a_name)
					return Registration::kDuplicate;
			}
			if (count == Capacity)
				return Registration::kFull;

			auto& entry = m_entries[count];
			a_name.copy(entry.name.data(), a_name.size());
			entry.name[a_name.size()] = '\0';
			entry.sink = a_sink;
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
			return Capacity;
		}

		[[nodiscard]] Sink At(size_t a_index) const noexcept
		{
			return a_index < Size() ? m_entries[a_index].sink : nullptr;
		}

		[[nodiscard]] std::string_view Name(size_t a_index) const noexcept
		{
			return a_index < Capacity ? std::string_view{ m_entries[a_index].name.data() } : std::string_view{};
		}

	private:
		struct Entry
		{
			std::array<char, kSinkNameCapacity> name{};
			Sink sink{ nullptr };
		};

		std::array<Entry, Capacity> m_entries{};
		std::atomic<size_t> m_count{ 0 };
		std::atomic<bool> m_open{ true };
	};

	enum class MessageClass : uint32_t
	{
		kOther,
		kMouse,
		kKeyboard
	};

	// Win32 message numbers, spelled out so the pure input logic stays testable without Windows headers.
	inline constexpr uint32_t kKeyboardMessageFirst = 0x0100;
	inline constexpr uint32_t kKeyboardMessageLast = 0x0109;
	inline constexpr uint32_t kMouseMessageFirst = 0x0200;
	inline constexpr uint32_t kMouseMessageLast = 0x020E;
	inline constexpr uint32_t kKeyDownMessage = 0x0100;
	inline constexpr uint32_t kSysKeyDownMessage = 0x0104;
	inline constexpr uint64_t kKeyRepeatBit = uint64_t{ 1 } << 30;

	[[nodiscard]] constexpr MessageClass ClassifyMessage(uint32_t a_message) noexcept
	{
		if (a_message >= kMouseMessageFirst && a_message <= kMouseMessageLast)
			return MessageClass::kMouse;
		if (a_message >= kKeyboardMessageFirst && a_message <= kKeyboardMessageLast)
			return MessageClass::kKeyboard;
		return MessageClass::kOther;
	}

	// Everything the menu does not capture still reaches the game, including focus and system messages.
	[[nodiscard]] constexpr bool SwallowsMessage(
		MessageClass a_class,
		bool a_wantCaptureMouse,
		bool a_wantCaptureKeyboard) noexcept
	{
		switch (a_class)
		{
		case MessageClass::kMouse:
			return a_wantCaptureMouse;
		case MessageClass::kKeyboard:
			return a_wantCaptureKeyboard;
		default:
			return false;
		}
	}

	[[nodiscard]] constexpr bool IsKeyRepeat(uint64_t a_lparam) noexcept
	{
		return (a_lparam & kKeyRepeatBit) != 0;
	}

	// Auto repeat must not retrigger a toggle that is held down.
	// Windows posts bare F10 and every Alt combination as WM_SYSKEYDOWN, so both keydown forms must toggle.
	[[nodiscard]] constexpr bool DispatchesToggleSinks(uint32_t a_message, uint64_t a_lparam) noexcept
	{
		return (a_message == kKeyDownMessage || a_message == kSysKeyDownMessage) && !IsKeyRepeat(a_lparam);
	}
}
