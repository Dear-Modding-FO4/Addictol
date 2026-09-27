#pragma once

#include <Telemetry/AdImageRegistry.h>
#include <Telemetry/AdImageSeries.h>

namespace Addictol
{
	class ImageMemory : public ImageSeriesSource<2>
	{
	public:
		ImageMemory() noexcept;
		[[nodiscard]] bool Start() noexcept;
		[[nodiscard]] bool Patch(const Image& a_image) noexcept;
		[[nodiscard]] uint64_t LiveBytes(ImageId a_image) const noexcept;
		[[nodiscard]] std::span<const MetricDescriptor> Schema() const noexcept override;

	private:
		enum class Import : uint8_t
		{
			kMalloc, kCalloc, kRealloc, kFree, kAlignedMalloc, kAlignedRealloc, kAlignedFree,
			kHeapAlloc, kHeapReAlloc, kHeapFree, kVirtualAlloc, kVirtualFree
		};
		struct Slot
		{
			ImageId image{};
			Import import{};
			void* previous{};
		};
		enum RowKind : size_t
		{
			kAlloc,
			kLive
		};
		struct Block
		{
			uintptr_t pointer{};
			uint64_t size{};
			ImageId image{};
			bool region{};
			uint64_t extent{};
		};
		static constexpr size_t kSlotCapacity{ 32768 };
		static constexpr size_t kBlockCapacity{ 65536 };
		static constexpr size_t kStripeCount{ 64 };
		static void OnImage(const Image& a_image, void* a_context) noexcept;
		static uintptr_t Hook(size_t a_slot, uintptr_t a_first, uintptr_t a_second, uintptr_t a_third, uintptr_t a_fourth);
		[[nodiscard]] uintptr_t Invoke(const Slot& a_slot, uintptr_t a_first, uintptr_t a_second, uintptr_t a_third, uintptr_t a_fourth);
		[[nodiscard]] uintptr_t Account(const Slot& a_slot, uintptr_t a_first, uintptr_t a_second, uintptr_t a_third, uintptr_t a_fourth);
		void Insert(Block a_block) noexcept;
		[[nodiscard]] Block Remove(uintptr_t a_pointer, bool a_region, uint64_t a_fullSize = 0) noexcept;
		void Drain(std::span<MetricValue> a_out) noexcept override;

		inline static ImageMemory* s_instance{};
		inline static thread_local bool s_inside{};
		std::mutex m_patchMutex;
		std::array<Slot, kSlotCapacity> m_slots{};
		size_t m_slotCount{};
		std::array<bool, kImageCapacity> m_patched{};
		std::atomic<uint64_t> m_freeCalls{};
		std::array<Block, kBlockCapacity> m_blocks{};
		std::array<std::atomic_flag, kStripeCount> m_locks{};
		std::atomic<uint64_t> m_overflow{};
		std::atomic<uint64_t> m_skipped{};
		std::atomic<bool> m_started{};
	};
}
