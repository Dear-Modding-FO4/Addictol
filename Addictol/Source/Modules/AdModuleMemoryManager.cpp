#include <Modules\AdModuleMemoryManager.h>
#include <AdAllocator.h>
#include <AdUtils.h>
#include <string.h>
#include <stdio.h>
#include <tuple>

#define strcasecmp _stricmp

namespace Addictol
{
	static REX::TOML::Bool<> bPathesMemoryManager{ "Patches", "bMemoryManager", true };
	extern REX::TOML::Str<> bAdditionalAllocatorType;

	template<typename Heap = ProxyHeap>
	class MemoryManager
	{
		MemoryManager(const MemoryManager&) = delete;
		MemoryManager(MemoryManager&&) = delete;
		MemoryManager& operator=(const MemoryManager&) = delete;
		MemoryManager& operator=(MemoryManager&&) = delete;

		MemoryManager() = default;
		~MemoryManager() = default;
	public:
		inline static const std::uint64_t EMPTY_POINTER{ 0 };

		[[nodiscard]] static void* Alloc([[maybe_unused]] MemoryManager* lpSelf, std::size_t nSize,
			std::uint32_t nAlignment, bool bAligned) noexcept
		{
			if (!nSize)
				return (void*)(&EMPTY_POINTER);

			return bAligned ?
				Heap::GetSingleton()->aligned_malloc(nSize, nAlignment) :
				Heap::GetSingleton()->malloc(nSize);
		}

		[[nodiscard]] static void* Realloc([[maybe_unused]] MemoryManager* lpSelf, void* lpBlock, 
			std::size_t nSize, std::uint32_t nAlignment, bool bAligned) noexcept
		{
			if (lpBlock == (const void*)(&EMPTY_POINTER))
				return Alloc(lpSelf, nSize, nAlignment, bAligned);

			return bAligned ?
				Heap::GetSingleton()->aligned_realloc(lpBlock, nSize, nAlignment) :
				Heap::GetSingleton()->realloc(lpBlock, nSize);
		}

		static void Dealloc([[maybe_unused]] MemoryManager* lpSelf, void* lpBlock, bool bAligned) noexcept
		{
			if (lpBlock == (const void*)(&EMPTY_POINTER))
				return;

			if (bAligned)
				Heap::GetSingleton()->aligned_free(lpBlock);
			else
				Heap::GetSingleton()->free(lpBlock);
		}

		[[nodiscard]] static std::size_t Size([[maybe_unused]] MemoryManager* lpSelf, void* lpBlock) noexcept
		{
			if (lpBlock == (const void*)(&EMPTY_POINTER))
				return 0;

			return Heap::GetSingleton()->msize(lpBlock);
		}
	};

	ModuleMemoryManager::ModuleMemoryManager() :
		Module("Module Memory Manager", &bPathesMemoryManager)
	{}

	bool ModuleMemoryManager::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleMemoryManager::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using tuple_t = std::tuple<uint64_t, void*>;

		bool tbb_mode = !strcasecmp(bAdditionalAllocatorType.GetValue().c_str(), "tbb");

		if (RELEX::IsRuntimeOG())
		{
			RELEX::WriteSafe(REL::ID(597736).address(), { 0xC3, 0x90 });
			*(std::uint32_t*)REL::ID(1570354).address() = 2;

			if (tbb_mode)
			{
				const std::array MMPatch
				{
					tuple_t{ 652767,	&MemoryManager<>::Alloc },
					tuple_t{ 1582181,	&MemoryManager<>::Dealloc },
					tuple_t{ 1502917,	&MemoryManager<>::Realloc },
					tuple_t{ 1453698,	&MemoryManager<>::Size },
				};

				for (const auto& [id, func] : MMPatch)
					RELEX::DetourJump(REL::ID(id).address(), (uintptr_t)func);
			}
			else
			{
				const std::array MMPatch
				{
					tuple_t{ 652767,	&MemoryManager<ProxyVoltekHeap>::Alloc },
					tuple_t{ 1582181,	&MemoryManager<ProxyVoltekHeap>::Dealloc },
					tuple_t{ 1502917,	&MemoryManager<ProxyVoltekHeap>::Realloc },
					tuple_t{ 1453698,	&MemoryManager<ProxyVoltekHeap>::Size },
				};

				for (const auto& [id, func] : MMPatch)
					RELEX::DetourJump(REL::ID(id).address(), (uintptr_t)func);
			}
		}
		else
		{
			RELEX::WriteSafe(REL::ID(2267875).address(), { 0xC3, 0x90 });
			*(std::uint32_t*)REL::ID(RELEX::IsRuntimeAE() ? 4807763 : 2688723).address() = 2;

			if (tbb_mode)
			{
				const std::array MMPatch
				{
					tuple_t{ 2267872,	&MemoryManager<>::Alloc },
					tuple_t{ 2267874,	&MemoryManager<>::Dealloc },
					tuple_t{ 2267873,	&MemoryManager<>::Realloc },
					tuple_t{ 2267858,	&MemoryManager<>::Size },
				};

				for (const auto& [id, func] : MMPatch)
					RELEX::DetourJump(REL::ID(id).address(), (uintptr_t)func);
			}
			else
			{
				const std::array MMPatch
				{
					tuple_t{ 2267872,	&MemoryManager<ProxyVoltekHeap>::Alloc },
					tuple_t{ 2267874,	&MemoryManager<ProxyVoltekHeap>::Dealloc },
					tuple_t{ 2267873,	&MemoryManager<ProxyVoltekHeap>::Realloc },
					tuple_t{ 2267858,	&MemoryManager<ProxyVoltekHeap>::Size },
				};

				for (const auto& [id, func] : MMPatch)
					RELEX::DetourJump(REL::ID(id).address(), (uintptr_t)func);
			}
		}

		return true;
	}

	bool ModuleMemoryManager::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}
}