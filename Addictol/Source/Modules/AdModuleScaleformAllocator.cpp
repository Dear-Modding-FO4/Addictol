#include <Modules/AdModuleScaleformAllocator.h>
#include <AdAllocator.h>
#include <AdUtils.h>

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
		inline static uint32_t HEAP_SIZE{ 0 };

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

		static void WriteHooks() noexcept
		{
			Init::func = RELEX::DetourJump(REL::ID{ 303712, 2284709 }.address(), (uintptr_t)&Init::thunk);
		}

		static void WriteSizes() noexcept
		{
			PAGE_SIZE = uAdditionalScaleformPageSize.GetValue();
			HEAP_SIZE = uAdditionalScaleformHeapSize.GetValue();
			PAGE_SIZE = std::min(PAGE_SIZE, (uint32_t)(2 * 1024));
			PAGE_SIZE = (PAGE_SIZE + 7) & ~7;
			HEAP_SIZE = std::min(HEAP_SIZE, (uint32_t)(8 * 1024));
			HEAP_SIZE = (HEAP_SIZE + 7) & ~7;

			REX::INFO("BSScaleformAllocator (Page: {} Kb, Heap: {} Mb)"sv, PAGE_SIZE, HEAP_SIZE);

			PAGE_SIZE *= 1024;
			HEAP_SIZE *= 1024 * 1024;

			// GetPageSize
			REL::WriteSafe(REL::Relocation(REL::ID{ 1310500, 2287456 }, REL::Offset{ 0x1 }).address(), &PAGE_SIZE, 4);
			// Default PageSize
			REL::WriteSafe(REL::Relocation(REL::ID{ 466425, 2287420 }, REL::Offset{ 0x8B, 0xF9, 0xF7 }).address(), &PAGE_SIZE, 4);
			// Default HeapSize
			REL::WriteSafe(REL::Relocation(REL::ID{ 466425, 2287420 }, REL::Offset{ 0x91, 0x104, 0x102 }).address(), &HEAP_SIZE, 4);
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

	bool ModuleScaleformAllocator::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleScaleformAllocator::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		BSScaleformAllocator<ProxyMiHeap>::Install();

		return true;
	}

	bool ModuleScaleformAllocator::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleScaleformAllocator::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}