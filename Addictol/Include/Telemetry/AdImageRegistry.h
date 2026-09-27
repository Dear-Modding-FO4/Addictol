#pragma once

#include <REX/REX.h>
#include <Windows.h>
#ifdef ERROR
#	undef ERROR
#endif

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace Addictol
{
	using ImageId = uint16_t;
	inline constexpr size_t kImageCapacity{ 1024 };
	inline constexpr ImageId kInvalidImage{ UINT16_MAX };

	enum class ImageKind : uint8_t
	{
		kGame,
		kSystem,
		kAddictol,
		kOther
	};

	struct Image
	{
		ImageId id{ kInvalidImage };
		ImageKind kind{ ImageKind::kOther };
		uintptr_t base{};
		uintptr_t end{};
		char name[260]{};
		RUNTIME_FUNCTION* functions{};
		size_t functionCount{};
		std::atomic<bool> alive{ false };
	};

	class ImageRegistry
	{
	public:
		using Consumer = void (*)(const Image&, void*) noexcept;

		[[nodiscard]] static ImageRegistry& Get() noexcept;
		[[nodiscard]] bool Start() noexcept;
		[[nodiscard]] const Image* Find(uintptr_t a_address) const noexcept;
		[[nodiscard]] const Image* Resolve(uintptr_t a_base) noexcept;
		[[nodiscard]] RUNTIME_FUNCTION* FunctionEntry(uintptr_t a_address) const noexcept;
		[[nodiscard]] const Image* At(ImageId a_id) const noexcept;
		[[nodiscard]] size_t Count() const noexcept;
		[[nodiscard]] bool Subscribe(Consumer a_consumer, void* a_context) noexcept;
		[[nodiscard]] uint64_t DroppedNotifications() const noexcept;

	private:
		static constexpr size_t kQueueCapacity{ 4096 };
		struct Event
		{
			ULONG reason{};
			uintptr_t base{};
		};
		struct QueueSlot
		{
			std::atomic<size_t> sequence{};
			Event event{};
		};
		struct Snapshot
		{
			size_t count{};
			std::array<ImageId, kImageCapacity> ids{};
		};
		struct Subscription
		{
			Consumer callback{};
			void* context{};
		};

		static void CALLBACK Notify(ULONG a_reason, const void* a_data, void* a_context) noexcept;
		static DWORD WINAPI Worker(void* a_context) noexcept;
		void Reconcile() noexcept;
		void Add(uintptr_t a_base) noexcept;
		void Remove(uintptr_t a_base) noexcept;
		void Publish() noexcept;
		[[nodiscard]] bool Pop(Event& a_event) noexcept;

		std::mutex m_startMutex;
		std::mutex m_updateMutex;
		std::array<Image, kImageCapacity> m_images{};
		std::atomic<size_t> m_count{};
		std::atomic<const Snapshot*> m_snapshot{};
		std::array<QueueSlot, kQueueCapacity> m_queue{};
		std::atomic<size_t> m_write{};
		size_t m_read{};
		std::atomic<uint64_t> m_dropped{};
		std::array<Subscription, 4> m_consumers{};
		size_t m_consumerCount{};
		HANDLE m_event{};
		HANDLE m_ready{};
		void* m_cookie{};
		uintptr_t m_ownBase{};
		wchar_t m_windows[1024]{};
		bool m_started{};
		bool m_startAttempted{};
	};
}
