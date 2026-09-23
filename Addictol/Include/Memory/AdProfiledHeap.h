#pragma once

#include <Memory/AdAllocator.h>
#include <Telemetry/AdOperationProfileTable.h>
#include <type_traits>

namespace Addictol
{
	enum class HeapProfileSite : uint8_t { MemoryManager, Scrap, Havok, CRT, SmallBlock, Scaleform };
	enum class HeapProfileOperation : uint8_t { Allocate, AlignedAllocate, Reallocate, AlignedReallocate, Free, AlignedFree, Size, AlignedSize };
	enum class HeapProfileResult : uint8_t { Requested, Succeeded, Failed, InPlace, Moved };

	namespace HeapProfileDetail
	{
		inline constexpr std::array<std::string_view, 6> kSites{
			"memory_manager", "scrap", "havok", "crt", "small_block", "scaleform"
		};
		struct Operation
		{
			std::string_view name;
			uint32_t sizeClasses;
			uint32_t results;
		};
		inline constexpr std::array kOperations{
			Operation{ "malloc", 4, 3 }, Operation{ "aligned_malloc", 4, 3 },
			Operation{ "realloc", 4, 5 }, Operation{ "aligned_realloc", 4, 5 },
			Operation{ "free", 1, 1 }, Operation{ "aligned_free", 1, 1 },
			Operation{ "msize", 1, 1 }, Operation{ "aligned_msize", 1, 1 }
		};
		inline constexpr std::array<std::string_view, 4> kSizes{ "le64", "le1k", "le64k", "large" };
		inline constexpr std::array<std::string_view, 5> kResults{ "requested", "succeeded", "failed", "in_place", "moved" };
		inline constexpr size_t kDescriptorsPerSite = [] {
			size_t count{ 0 };
			for (const auto& operation : kOperations)
				count += operation.sizeClasses * operation.results;
			return count;
		}();
		inline constexpr auto kNames = [] {
			std::array<OperationProfileNames, kSites.size() * kDescriptorsPerSite> names{};
			size_t index{ 0 };
			for (const auto site : kSites)
			{
				for (const auto& operation : kOperations)
				{
					for (size_t size = 0; size < operation.sizeClasses; ++size)
					{
						OperationProfileName group;
						group.Append("allocator.");
						group.Append(site);
						group.Append(".");
						group.Append(operation.name);
						if (operation.sizeClasses > 1)
						{
							group.Append(".");
							group.Append(kSizes[size]);
						}
						for (size_t result = 0; result < operation.results; ++result)
							names[index++] = MakeOperationProfileNames(group, kResults[result]);
					}
				}
			}
			return names;
		}();
	}

	inline constexpr uint32_t kHeapProfileSamplingPeriod{ 256 };
	inline constexpr size_t kHeapProfileRecordCapacity{ 262144 };
	inline constexpr auto kHeapProfileDescriptors =
		MakeOperationProfileDescriptors(HeapProfileDetail::kNames, kHeapProfileSamplingPeriod);

	[[nodiscard]] constexpr uint32_t HeapProfileDescriptor(
		HeapProfileSite a_site, HeapProfileOperation a_operation, size_t a_size = 0,
		HeapProfileResult a_result = HeapProfileResult::Requested) noexcept
	{
		const auto operation = static_cast<size_t>(a_operation);
		size_t index = static_cast<size_t>(a_site) * HeapProfileDetail::kDescriptorsPerSite;
		for (size_t prior = 0; prior < operation; ++prior)
			index += HeapProfileDetail::kOperations[prior].sizeClasses * HeapProfileDetail::kOperations[prior].results;
		const auto sizeClass = a_size <= 64 ? 0 : a_size <= 1024 ? 1 : a_size <= 65536 ? 2 : 3;
		if (HeapProfileDetail::kOperations[operation].sizeClasses > 1)
			index += sizeClass * HeapProfileDetail::kOperations[operation].results;
		return static_cast<uint32_t>(index + static_cast<size_t>(a_result));
	}

	[[nodiscard]] OperationProfileSource* HeapOperationProfile() noexcept;
	[[nodiscard]] bool PrepareHeapOperationProfile() noexcept;
	[[nodiscard]] bool InitializeHeapOperationProfile(
		TelemetryHub& a_hub, std::string_view a_selectedHeap, std::string_view a_smallBlockHeap,
		std::string_view a_scaleformHeap) noexcept;

	template<class Heap, HeapProfileSite Site>
	class ProfiledHeap
	{
	public:
		[[nodiscard]] static ProfiledHeap* GetSingleton() noexcept
		{
			static ProfiledHeap heap;
			return &heap;
		}

		[[nodiscard]] void* malloc(size_t a_size) const noexcept
		{
			return Allocate<HeapProfileOperation::Allocate>(nullptr, a_size, [&] { return Heap::GetSingleton()->malloc(a_size); });
		}
		[[nodiscard]] void* aligned_malloc(size_t a_size, size_t a_alignment) const noexcept
		{
			return Allocate<HeapProfileOperation::AlignedAllocate>(nullptr, a_size, [&] { return Heap::GetSingleton()->aligned_malloc(a_size, a_alignment); });
		}
		[[nodiscard]] void* realloc(void* a_block, size_t a_size) const noexcept
		{
			return Allocate<HeapProfileOperation::Reallocate>(a_block, a_size, [&] { return Heap::GetSingleton()->realloc(a_block, a_size); });
		}
		[[nodiscard]] void* aligned_realloc(void* a_block, size_t a_size, size_t a_alignment) const noexcept
		{
			return Allocate<HeapProfileOperation::AlignedReallocate>(a_block, a_size, [&] { return Heap::GetSingleton()->aligned_realloc(a_block, a_size, a_alignment); });
		}
		void free(void* a_block) const noexcept
		{
			Observe<HeapProfileOperation::Free>([&] { Heap::GetSingleton()->free(a_block); });
		}
		void aligned_free(void* a_block) const noexcept
		{
			Observe<HeapProfileOperation::AlignedFree>([&] { Heap::GetSingleton()->aligned_free(a_block); });
		}
		[[nodiscard]] size_t msize(void* a_block) const noexcept
		{
			return Observe<HeapProfileOperation::Size>([&] { return Heap::GetSingleton()->msize(a_block); });
		}

	private:
		template<HeapProfileOperation Operation, class F>
		[[nodiscard]] static void* Allocate(void* a_block, size_t a_size, F&& a_call) noexcept
		{
			OperationProfileConsumer<true> profile{ HeapOperationProfile() };
			const auto descriptor = HeapProfileDescriptor(Site, Operation, a_size);
			auto token = profile.Begin(descriptor);
			auto* result = a_call();
			if (token)
			{
				auto classification = HeapProfileResult::Succeeded;
				if (!result && a_size)
					classification = HeapProfileResult::Failed;
				else if constexpr (Operation == HeapProfileOperation::Reallocate || Operation == HeapProfileOperation::AlignedReallocate)
				{
					if (result)
						classification = result == a_block ? HeapProfileResult::InPlace : HeapProfileResult::Moved;
				}
				profile.End(std::move(token), a_size, descriptor + static_cast<uint32_t>(classification));
			}
			return result;
		}

		template<HeapProfileOperation Operation, class F>
		static decltype(auto) Observe(F&& a_call) noexcept
		{
			OperationProfileConsumer<true> profile{ HeapOperationProfile() };
			auto token = profile.Begin(HeapProfileDescriptor(Site, Operation));
			if constexpr (std::is_void_v<decltype(a_call())>)
			{
				a_call();
				profile.End(std::move(token));
			}
			else
			{
				auto result = a_call();
				profile.End(std::move(token));
				return result;
			}
		}
	};

	template<bool Enabled, class Heap, HeapProfileSite Site>
	using SelectedProfiledHeap = std::conditional_t<Enabled, ProfiledHeap<Heap, Site>, Heap>;

	template<class Heap, HeapProfileSite Site, class Installer>
	[[nodiscard]] bool InstallSelectedProfiledHeap(bool a_enabled, Installer&& a_installer)
	{
		static_assert(std::is_same_v<SelectedProfiledHeap<false, Heap, Site>, Heap>);
		return InstallSelectedOperationProfileConsumer(a_enabled, [&]<bool Enabled> {
			return a_installer.template operator()<SelectedProfiledHeap<Enabled, Heap, Site>>();
		});
	}

	template<class Heap, HeapProfileSite Site, class Installer>
	[[nodiscard]] bool InstallSelectedProfiledHeap(Installer&& a_installer)
	{
		return InstallSelectedProfiledHeap<Heap, Site>(
			PrepareHeapOperationProfile(), std::forward<Installer>(a_installer));
	}
}
