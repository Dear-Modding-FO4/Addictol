#include <Modules/AdModuleScaleformAllocator.h>
#include <Memory/AdAllocator.h>
#include <Core/AdUtils.h>

#include <Scaleform/S/SysAlloc.h>
#include <Scaleform/M/MemoryHeap.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesScaleformAllocator{ "Patches"sv, "bScaleformAllocator"sv, true };
	static REX::TOML::U32<> uAdditionalScaleformPageSize{ "Additional"sv, "uScaleformPageSize"sv, 64ul };
	static REX::TOML::U32<> uAdditionalScaleformHeapSize{ "Additional"sv, "uScaleformHeapSize"sv, 2048ul };

	template<typename Heap>
	class BSScaleformAllocator final : public Scaleform::SysAlloc
	{
	public:
		inline static uint32_t PAGE_SIZE{ 0 };
		inline static uint64_t HEAP_SIZE{ 0 };

		[[nodiscard]] static BSScaleformAllocator* GetSingleton()
		{
			static BSScaleformAllocator singleton;
			return std::addressof(singleton);
		}

		struct Init
		{
			static void thunk(const Scaleform::MemoryHeap::HeapDesc& a_rootHeapDesc, Scaleform::SysAllocBase*)
			{
				func(a_rootHeapDesc, BSScaleformAllocator::GetSingleton());
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct SysAllocMapper
		{
			// this function accepts 64-bit numbers, however, Bethesda initializes with 32-bit numbers.
			static void thunk(SysAllocMapper* a_this, const Scaleform::SysAllocBase* a_allocator,
				[[maybe_unused]] uint64_t a_heapSize, [[maybe_unused]] uint64_t a_pageSize, bool a_unk) noexcept
			{
				func(a_this, a_allocator, HEAP_SIZE, PAGE_SIZE, a_unk);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void WriteHooks() noexcept
		{
			// vmm can't handle it
#if 0
			Init::func = RELEX::DetourJump(REL::ID{ 303712, 2284709 }.address(), (uintptr_t)&Init::thunk);
#endif
			SysAllocMapper::func = RELEX::DetourJump(REL::ID{ 347541, 2295435 }.address(), (uintptr_t)&SysAllocMapper::thunk);
		}

		static void WriteSizes() noexcept
		{
			PAGE_SIZE = uAdditionalScaleformPageSize.GetValue();
			HEAP_SIZE = uAdditionalScaleformHeapSize.GetValue();
			PAGE_SIZE = std::min(PAGE_SIZE, static_cast<uint32_t>(2048));
			PAGE_SIZE = (PAGE_SIZE + 7) & ~7;
			HEAP_SIZE = std::min(static_cast<uint64_t>(HEAP_SIZE), static_cast<uint64_t>(8192));
			HEAP_SIZE = (HEAP_SIZE + 7) & ~7;
			PAGE_SIZE = std::max(PAGE_SIZE, static_cast<uint32_t>(64));		// min value 64kb
			HEAP_SIZE = std::max(HEAP_SIZE, static_cast<uint64_t>(2048));	// min value 2gb

			REX::INFO("BSScaleformAllocator (Page: {} Kb, Heap: {} Mb)"sv, PAGE_SIZE, HEAP_SIZE);

			PAGE_SIZE *= 1024ul;
			HEAP_SIZE *= 1024ull * 1024ull;

			// GetPageSize
			REL::WriteSafe(REL::Relocation(REL::ID{ 1310500, 2287456 }, REL::Offset{ 0x1 }).address(), &PAGE_SIZE, 4);
		}
		
		static void Install() noexcept
		{
			WriteHooks();
			WriteSizes();
		}
	protected:
		void* Alloc(std::size_t a_size, std::size_t a_align) override
		{
			return a_size > 0 ?
				Heap::GetSingleton()->aligned_malloc(a_size, a_align) :
				nullptr;
		}

		void Free(void* a_block, [[maybe_unused]] std::size_t, [[maybe_unused]] std::size_t) override
		{
			Heap::GetSingleton()->aligned_free(a_block);
		}

		void* Realloc(void* a_block, [[maybe_unused]] std::size_t, std::size_t a_size, std::size_t a_align) override
		{
			return Heap::GetSingleton()->aligned_realloc(a_block, a_size, a_align);
		}
	private:
		BSScaleformAllocator() = default;
		~BSScaleformAllocator() = default;

		BSScaleformAllocator(const BSScaleformAllocator&) = delete;
		BSScaleformAllocator(BSScaleformAllocator&&) = delete;	
		BSScaleformAllocator& operator=(const BSScaleformAllocator&) = delete;
		BSScaleformAllocator& operator=(BSScaleformAllocator&&) = delete;
	};

	ModuleScaleformAllocator::ModuleScaleformAllocator() :
		Module("Scaleform Allocator", &bPatchesScaleformAllocator)
	{}

	bool ModuleScaleformAllocator::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		BSScaleformAllocator<ProxyCurrentHeap>::Install();

		return true;
	}

}