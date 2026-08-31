#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Addictol::ImguiPlatform
{
	inline constexpr uint32_t kPresentSlot = 8;
	inline constexpr uint32_t kResizeBuffersSlot = 13;
	inline constexpr uint32_t kPresentTestFlag = 0x00000001;
	inline constexpr uint32_t kWindowNcDestroyMessage = 0x0082;

	struct AttachmentIdentity
	{
		uintptr_t swapChain{ 0 };
		uintptr_t device{ 0 };
		uintptr_t context{ 0 };
		uintptr_t window{ 0 };

		[[nodiscard]] constexpr bool Valid() const noexcept
		{
			return swapChain && device && context && window;
		}
	};

	enum class AttachmentSource : uint32_t
	{
		kDiscovery,
		kExplicit
	};

	enum class AttachmentDecision : uint32_t
	{
		kReject,
		kKeepCurrent,
		kAttach
	};

	enum class AttachmentLifecycle : uint32_t
	{
		kVacant,
		kActive,
		kRetired
	};

	[[nodiscard]] constexpr AttachmentDecision DecideAttachment(
		const AttachmentIdentity& a_current,
		const AttachmentIdentity& a_candidate,
		AttachmentSource a_source,
		AttachmentLifecycle a_lifecycle) noexcept
	{
		if (!a_candidate.Valid())
			return AttachmentDecision::kReject;
		if (a_lifecycle == AttachmentLifecycle::kRetired)
			return AttachmentDecision::kAttach;
		if (a_current.swapChain == a_candidate.swapChain &&
			a_current.device == a_candidate.device &&
			a_current.context == a_candidate.context &&
			a_current.window == a_candidate.window &&
			a_lifecycle == AttachmentLifecycle::kActive)
			return AttachmentDecision::kKeepCurrent;
		if (a_current.swapChain == a_candidate.swapChain &&
			a_lifecycle == AttachmentLifecycle::kActive)
			return AttachmentDecision::kAttach;
		if (a_lifecycle != AttachmentLifecycle::kActive ||
			!a_current.Valid() ||
			a_source == AttachmentSource::kExplicit)
			return AttachmentDecision::kAttach;
		return AttachmentDecision::kKeepCurrent;
	}

	inline constexpr uint32_t kDxgiErrorDeviceRemoved = 0x887A0005u;
	inline constexpr uint32_t kDxgiErrorDeviceHung = 0x887A0006u;
	inline constexpr uint32_t kDxgiErrorDeviceReset = 0x887A0007u;
	inline constexpr uint32_t kDxgiErrorDriverInternal = 0x887A0020u;

	[[nodiscard]] constexpr bool IsDefinitiveSwapChainLoss(uint32_t a_result) noexcept
	{
		return a_result == kDxgiErrorDeviceRemoved ||
			a_result == kDxgiErrorDeviceHung ||
			a_result == kDxgiErrorDeviceReset ||
			a_result == kDxgiErrorDriverInternal;
	}

	[[nodiscard]] constexpr bool ReusesHookAssociation(
		AttachmentLifecycle a_lifecycle,
		uintptr_t a_liveVtable,
		uintptr_t a_associatedVtable) noexcept
	{
		return a_lifecycle != AttachmentLifecycle::kRetired ||
			(a_liveVtable && a_liveVtable == a_associatedVtable);
	}

	enum class HookDispatchMatch : uint32_t
	{
		kNone,
		kSwapChain,
		kVtable
	};

	[[nodiscard]] constexpr HookDispatchMatch MatchHookDispatch(
		uintptr_t a_swapChain,
		uintptr_t a_liveVtable,
		uintptr_t a_associatedSwapChain,
		uintptr_t a_patchedVtable) noexcept
	{
		if (a_associatedSwapChain && a_swapChain == a_associatedSwapChain)
			return HookDispatchMatch::kSwapChain;
		if (a_patchedVtable && a_liveVtable == a_patchedVtable)
			return HookDispatchMatch::kVtable;
		return HookDispatchMatch::kNone;
	}

	[[nodiscard]] constexpr bool ObservesDisplayedFrame(
		uint32_t a_presentFlags,
		bool a_presentSucceeded) noexcept
	{
		return a_presentSucceeded && (a_presentFlags & kPresentTestFlag) == 0;
	}

	[[nodiscard]] constexpr bool ShouldInitializeHost(
		bool a_hasClients,
		bool a_windowReady) noexcept
	{
		return a_hasClients && a_windowReady;
	}

	[[nodiscard]] constexpr bool ShouldRenderHostFrame(
		bool a_modalVisible,
		bool a_overlayDemanded) noexcept
	{
		return a_modalVisible || a_overlayDemanded;
	}

	[[nodiscard]] constexpr bool ShouldSuppressGameInput(bool a_modalVisible) noexcept
	{
		return a_modalVisible;
	}

	[[nodiscard]] constexpr bool RequiresBackendReset(
		const AttachmentIdentity& a_current,
		const AttachmentIdentity& a_candidate,
		bool a_backendInitialized) noexcept
	{
		return a_backendInitialized &&
			(a_current.device != a_candidate.device ||
				a_current.context != a_candidate.context ||
				a_current.window != a_candidate.window);
	}

	struct BackBufferIdentity
	{
		uintptr_t resource{ 0 };
		uint32_t width{ 0 };
		uint32_t height{ 0 };

		[[nodiscard]] constexpr bool Valid() const noexcept
		{
			return resource && width && height;
		}
	};

	struct MousePosition
	{
		float x{ 0.0f };
		float y{ 0.0f };
	};

	[[nodiscard]] constexpr MousePosition MapClientToBackBuffer(
		MousePosition a_position,
		uint32_t a_clientWidth,
		uint32_t a_clientHeight,
		uint32_t a_backBufferWidth,
		uint32_t a_backBufferHeight) noexcept
	{
		if (!a_clientWidth ||
			!a_clientHeight ||
			!a_backBufferWidth ||
			!a_backBufferHeight ||
			(a_clientWidth == a_backBufferWidth &&
				a_clientHeight == a_backBufferHeight) ||
			!(a_position.x >= 0.0f &&
				a_position.y >= 0.0f &&
				a_position.x < static_cast<float>(a_clientWidth) &&
				a_position.y < static_cast<float>(a_clientHeight)))
			return a_position;

		return {
			a_position.x * static_cast<float>(a_backBufferWidth) /
				static_cast<float>(a_clientWidth),
			a_position.y * static_cast<float>(a_backBufferHeight) /
				static_cast<float>(a_clientHeight)
		};
	}

	enum class BackBufferDecision : uint32_t
	{
		kSkip,
		kKeep,
		kRecreate
	};

	[[nodiscard]] constexpr BackBufferDecision DecideBackBuffer(
		const BackBufferIdentity& a_current,
		const BackBufferIdentity& a_candidate,
		bool a_hasRenderTarget) noexcept
	{
		if (!a_candidate.Valid())
			return BackBufferDecision::kSkip;
		if (a_hasRenderTarget &&
			a_current.resource == a_candidate.resource &&
			a_current.width == a_candidate.width &&
			a_current.height == a_candidate.height)
			return BackBufferDecision::kKeep;
		return BackBufferDecision::kRecreate;
	}

	enum class InstallState : uint32_t
	{
		kNotAttempted,
		kAttempted,
		kInstalled,
		kRejected
	};

	// Installation is attempted once so repeated calls cannot disturb an existing IAT chain.
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
	inline constexpr uint32_t kKeyUpMessage = 0x0101;
	inline constexpr uint32_t kSysKeyDownMessage = 0x0104;
	inline constexpr uint32_t kSysKeyUpMessage = 0x0105;
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

	// Fresh WM_KEYDOWN and WM_SYSKEYDOWN messages both dispatch, while auto repeat does not.
	[[nodiscard]] constexpr bool DispatchesToggleSinks(uint32_t a_message, uint64_t a_lparam) noexcept
	{
		return (a_message == kKeyDownMessage || a_message == kSysKeyDownMessage) && !IsKeyRepeat(a_lparam);
	}

	enum class ToggleMessageDecision : uint32_t
	{
		kForward,
		kDispatch,
		kConsume,
		kConsumeAndRelease
	};

	[[nodiscard]] constexpr ToggleMessageDecision DecideToggleMessage(
		uint32_t a_message,
		uint64_t a_lparam,
		bool a_pressConsumed) noexcept
	{
		if (a_message == kKeyDownMessage || a_message == kSysKeyDownMessage)
		{
			if (!IsKeyRepeat(a_lparam))
				return ToggleMessageDecision::kDispatch;
			return a_pressConsumed ?
				ToggleMessageDecision::kConsume :
				ToggleMessageDecision::kForward;
		}
		if ((a_message == kKeyUpMessage || a_message == kSysKeyUpMessage) &&
			a_pressConsumed)
			return ToggleMessageDecision::kConsumeAndRelease;
		return ToggleMessageDecision::kForward;
	}

	[[nodiscard]] constexpr bool HandlesWindowMessage(
		bool a_activeWindow,
		bool a_drawingRequested,
		bool a_backendReady,
		bool a_hasContext) noexcept
	{
		return a_activeWindow &&
			a_drawingRequested &&
			a_backendReady &&
			a_hasContext;
	}

	[[nodiscard]] constexpr bool RetiresWindowHook(uint32_t a_message) noexcept
	{
		return a_message == kWindowNcDestroyMessage;
	}
}
