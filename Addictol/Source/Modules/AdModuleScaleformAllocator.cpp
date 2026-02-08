#include <Modules\AdModuleScaleformAllocator.h>
#include <AdAllocator.h>
#include <AdUtils.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesScaleformAllocator{ "Patches", "bScaleformAllocator", true };
	static REX::TOML::U32<> uAdditionalScaleformPageSize{ "Additional", "uScaleformPageSize", 256ul };
	static REX::TOML::U32<> uAdditionalScaleformHeapSize{ "Additional", "uScaleformHeapSize", 512ul };

	class BSScaleformSysMemMapper
	{
	public:
		inline static uint32_t PAGE_SIZE;
		inline static uint32_t HEAP_SIZE;

		static uint32_t GetPageSize(BSScaleformSysMemMapper* _this) noexcept
		{
			return PAGE_SIZE;
		}

		static void* Init(BSScaleformSysMemMapper* _this, size_t size) noexcept
		{
			return REX::W32::VirtualAlloc(nullptr, size, REX::W32::MEM_RESERVE, REX::W32::PAGE_READWRITE);
		}

		static bool Release(BSScaleformSysMemMapper* _this, void* address) noexcept
		{
			return REX::W32::VirtualFree(address, HEAP_SIZE, REX::W32::MEM_RELEASE);
		}

		static void* Alloc(BSScaleformSysMemMapper* _this, void* address, size_t size) noexcept
		{
			return REX::W32::VirtualAlloc(address, size, REX::W32::MEM_COMMIT, REX::W32::PAGE_READWRITE);
		}

		static bool Dealloc(BSScaleformSysMemMapper* _this, void* address, size_t size) noexcept
		{
			return REX::W32::VirtualFree(address, size, REX::W32::MEM_DECOMMIT);
		}
	};

	ModuleScaleformAllocator::ModuleScaleformAllocator() :
		Module("Module Scaleform Allocator", &bPatchesScaleformAllocator)
	{}

	bool ModuleScaleformAllocator::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleScaleformAllocator::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		BSScaleformSysMemMapper::PAGE_SIZE = uAdditionalScaleformPageSize.GetValue();
		BSScaleformSysMemMapper::HEAP_SIZE = uAdditionalScaleformHeapSize.GetValue();
		BSScaleformSysMemMapper::PAGE_SIZE = std::min(BSScaleformSysMemMapper::PAGE_SIZE, (uint32_t)(2 * 1024));
		BSScaleformSysMemMapper::PAGE_SIZE = (BSScaleformSysMemMapper::PAGE_SIZE + 7) & ~7;
		BSScaleformSysMemMapper::HEAP_SIZE = std::min(BSScaleformSysMemMapper::HEAP_SIZE, (uint32_t)(2 * 1024));
		BSScaleformSysMemMapper::HEAP_SIZE = (BSScaleformSysMemMapper::HEAP_SIZE + 7) & ~7;

		REX::INFO("BSScaleformSysMemMapper (Page: {} Kb, Heap: {} Mb)",
			BSScaleformSysMemMapper::PAGE_SIZE, BSScaleformSysMemMapper::HEAP_SIZE);

		BSScaleformSysMemMapper::PAGE_SIZE *= 1024;
		BSScaleformSysMemMapper::HEAP_SIZE *= 1024 * 1024;

		auto vtable = reinterpret_cast<uintptr_t*>(REL::ID(40537).address());
		if (!vtable)
			return false;

		RELEX::ScopeLock lock(vtable, 0x40);
		vtable[0] = (std::uintptr_t)BSScaleformSysMemMapper::GetPageSize;
		vtable[1] = (std::uintptr_t)BSScaleformSysMemMapper::Init;
		vtable[2] = (std::uintptr_t)BSScaleformSysMemMapper::Release;
		vtable[3] = (std::uintptr_t)BSScaleformSysMemMapper::Alloc;
		vtable[4] = (std::uintptr_t)BSScaleformSysMemMapper::Dealloc;

		if (RELEX::IsRuntimeOG())
		{
			auto offset = REL::ID(466425).address();

			REL::WriteSafe(offset + 0x8B, reinterpret_cast<uint8_t*>(&BSScaleformSysMemMapper::PAGE_SIZE), 4);
			REL::WriteSafe(offset + 0x91, reinterpret_cast<uint8_t*>(&BSScaleformSysMemMapper::HEAP_SIZE), 4);
		}
		else if (RELEX::IsRuntimeNG())
		{
			auto offset = REL::ID(2287420).address();

			REL::WriteSafe(offset + 0xF9,  reinterpret_cast<uint8_t*>(&BSScaleformSysMemMapper::PAGE_SIZE), 4);
			REL::WriteSafe(offset + 0x104, reinterpret_cast<uint8_t*>(&BSScaleformSysMemMapper::HEAP_SIZE), 4);
		}
		else
		{
			auto offset = REL::ID(2287420).address();

			REL::WriteSafe(offset + 0xF7, reinterpret_cast<uint8_t*>(&BSScaleformSysMemMapper::PAGE_SIZE), 4);
			REL::WriteSafe(offset + 0x102, reinterpret_cast<uint8_t*>(&BSScaleformSysMemMapper::HEAP_SIZE), 4);
		}

		return true;
	}

	bool ModuleScaleformAllocator::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}
}