#include <Modules\AdModuleMemoryManager.h>
#include <AdAssert.h>
#include <AdAllocator.h>
#include <AdUtils.h>
#include <string.h>
#include <stdio.h>
#include <xbyak\xbyak.h>
#include <tuple>

#if AD_TRACER
#	include <AdMemoryTracer.h>
#endif

#undef MEM_RELEASE

#include <RE\M\MemoryManager.h>
#include <RE\B\BSThreadEvent.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesMemoryManager{ "Patches"sv, "bMemoryManager"sv, true };
	static REX::TOML::Bool<> bAdditionalUseNewRedistributable{ "Additional"sv, "bUseNewRedistributable"sv, true };

	class AutoScrapHeap
	{
		AutoScrapHeap(const AutoScrapHeap&) = delete;
		AutoScrapHeap(AutoScrapHeap&&) = delete;
		AutoScrapHeap& operator=(const AutoScrapHeap&) = delete;
		AutoScrapHeap& operator=(AutoScrapHeap&&) = delete;

		AutoScrapHeap() = default;
		~AutoScrapHeap() = default;

		inline static void CtorLong()
		{
			RELEX::WriteSafeNop(REL::ID(RELEX::IsRuntimeOG() ? 1305199 : 2267866).address() + 0x1D, 0x15);
		}

		static void CtorShort()
		{
			struct Patch :
				Xbyak::CodeGenerator
			{
				Patch()
				{
					mov(qword[rcx], 0);
					mov(rax, rcx);
					ret();
				}
			} p;

			auto Off = RE::ID::MemoryManager::AutoScrapBuffer::ctor.address();

			p.ready();
			AdAssert(p.getSize() <= 0x1C);

			RELEX::WriteSafeNop(Off, 0x1C);
			REL::WriteSafe(Off, p.getCode<uint8_t*>(), p.getSize());
		}

		static void Dtor()
		{
			struct Patch :
				Xbyak::CodeGenerator
			{
				Patch()
				{
					xor_(rax, rax);
					cmp(rbx, rax);
				}
			} p;

			auto Off = RE::ID::MemoryManager::AutoScrapBuffer::dtor.address();
			p.ready();
			AdAssert(p.getSize() <= 0x1D);

			RELEX::WriteSafeNop(Off + 0x9, 0x1D);
			REL::WriteSafe(Off + 0x9, p.getCode<uint8_t*>(), p.getSize());
			RELEX::WriteSafe(Off + 0x26, { 0x74 }); // jnz -> jz
		}
	public:
		static void Install()
		{
			RELEX::WriteSafe(REL::ID(RELEX::IsRuntimeOG() ? 1557709 : 2267868).address(), {0xC3, 0x90, 0x90, 0x90});

			CtorLong();
			CtorShort();
			Dtor();
		}
	};

	template<typename Heap = ProxyVoltekHeap>
	class ScrapHeap
	{
		ScrapHeap(const ScrapHeap&) = delete;
		ScrapHeap(ScrapHeap&&) = delete;
		ScrapHeap& operator=(const ScrapHeap&) = delete;
		ScrapHeap& operator=(ScrapHeap&&) = delete;

		ScrapHeap() = default;
		~ScrapHeap() = default;

		static void WriteStubs()
		{
			// Remove stuff
			constexpr static std::initializer_list<uint8_t> RET_NOP = { 0xC3, 0x90, 0x90, 0x90 };

			if (RELEX::IsRuntimeOG())
			{
				std::array<uint64_t, 7> p
				{
					550677,		// Clean
					111657,		// ClearKeepPages
					975239,		// InsertFreeBlock
					84225,		// RemoveFreeBlock
					1255203,	// SetKeepPages
					912706,		// dtor
					48809,		// ctor
				};

				for (const auto& i : p)
					RELEX::WriteSafe(REL::ID(i).address(), RET_NOP);
			}
			else
			{
				std::array<uint64_t, 7> p
				{
					2267990,	// Clean
					2267989,	// ClearKeepPages
					2267993,	// InsertFreeBlock
					2267994,	// RemoveFreeBlock
					2267988,	// SetKeepPages
					2267982,	// dtor
					2267981,	// ctor
				};

				for (const auto& i : p)
					RELEX::WriteSafe(REL::ID(i).address(), RET_NOP);
			}
		}

		static void WriteHooks()
		{
			RELEX::DetourJump(RE::ID::ScrapHeap::Allocate.address(), (uint64_t)&Allocate);
			RELEX::DetourJump(RE::ID::ScrapHeap::Deallocate.address(), (uint64_t)&Deallocate);
		}
	public:
		inline static const std::uint64_t EMPTY_POINTER{ 0 };

		[[nodiscard]] inline static void* Allocate([[maybe_unused]] ScrapHeap* lpSelf, std::size_t nSize, std::size_t nAlignment) noexcept(true)
		{
			if (!nSize)
				return (void*)(&EMPTY_POINTER);
#if AD_TRACER
			auto ret_addr = _ReturnAddress();
			auto ptr = Heap::GetSingleton()->aligned_malloc(nSize, nAlignment);
			MemoryTracer::GetSingleton()->Add(ptr, nSize, ret_addr);
			return ptr;
#else
			return Heap::GetSingleton()->aligned_malloc(nSize, nAlignment);
#endif
		}

		inline static void Deallocate([[maybe_unused]] ScrapHeap* lpSelf, void* lpBlock) noexcept(true)
		{
			if (lpBlock == (const void*)(&EMPTY_POINTER))
				return;
#if AD_TRACER
			MemoryTracer::GetSingleton()->Remove(lpBlock);
#endif
			Heap::GetSingleton()->aligned_free(lpBlock);
		}

		static void Install()
		{
			WriteStubs();
			WriteHooks();
		}
	};

	template<typename Heap = ProxyVoltekHeap>
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
#if AD_TRACER
			auto ret_addr = _ReturnAddress();
			auto ptr = bAligned ?
				Heap::GetSingleton()->aligned_malloc(nSize, nAlignment) :
				Heap::GetSingleton()->malloc(nSize);
			MemoryTracer::GetSingleton()->Add(ptr, nSize, ret_addr);
			return ptr;
#else
			return bAligned ?
				Heap::GetSingleton()->aligned_malloc(nSize, nAlignment) :
				Heap::GetSingleton()->malloc(nSize);
#endif
		}

		[[nodiscard]] static void* Realloc([[maybe_unused]] MemoryManager* lpSelf, void* lpBlock, 
			std::size_t nSize, std::uint32_t nAlignment, bool bAligned) noexcept
		{
#if AD_TRACER
			void* ptr = nullptr;
			auto ret_addr = _ReturnAddress();

			if (lpBlock == (const void*)(&EMPTY_POINTER))
				ptr = bAligned ?
					Heap::GetSingleton()->aligned_malloc(nSize, nAlignment) :
					Heap::GetSingleton()->malloc(nSize);
			else
			{
				MemoryTracer::GetSingleton()->Remove(ptr);

				ptr = bAligned ?
					Heap::GetSingleton()->aligned_realloc(lpBlock, nSize, nAlignment) :
					Heap::GetSingleton()->realloc(lpBlock, nSize);
			}

			MemoryTracer::GetSingleton()->Add(ptr, nSize, ret_addr);
			return ptr;
#else
			if (lpBlock == (const void*)(&EMPTY_POINTER))
				return Alloc(lpSelf, nSize, nAlignment, bAligned);

			return bAligned ?
				Heap::GetSingleton()->aligned_realloc(lpBlock, nSize, nAlignment) :
				Heap::GetSingleton()->realloc(lpBlock, nSize);
#endif
		}

		static void Dealloc([[maybe_unused]] MemoryManager* lpSelf, void* lpBlock, bool bAligned) noexcept
		{
			if (lpBlock == (const void*)(&EMPTY_POINTER))
				return;
#if AD_TRACER
			MemoryTracer::GetSingleton()->Remove(lpBlock);
#endif
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

	template<typename Heap = ProxyVoltekHeap>
	class bhkThreadMemorySource
	{
	private:
		char _pad0[0x8];
		CRITICAL_SECTION m_CritSec;
	public:
		AD_DECLARE_CONSTRUCTOR_HOOK(bhkThreadMemorySource);

		bhkThreadMemorySource() noexcept
		{
			InitializeCriticalSection(&m_CritSec);
		}

		virtual ~bhkThreadMemorySource() noexcept
		{
			DeleteCriticalSection(&m_CritSec);
		}

		[[nodiscard]] virtual void* blockAlloc(std::size_t numBytes) noexcept
		{
			return Heap::GetSingleton()->aligned_malloc(numBytes, 16);
		}

		virtual void blockFree(void* p, std::size_t numBytes) noexcept
		{
			Heap::GetSingleton()->aligned_free(p);
		}

		[[nodiscard]] virtual void* bufAlloc(std::size_t& reqNumBytesInOut) noexcept
		{
			return blockAlloc(reqNumBytesInOut);
		}

		virtual void bufFree(void* p, std::size_t numBytes) noexcept
		{
			return blockFree(p, numBytes);
		}

		[[nodiscard]] virtual void* bufRealloc(void* pold, std::size_t oldNumBytes, std::size_t& reqNumBytesInOut) noexcept
		{
			void* p = blockAlloc(reqNumBytesInOut);
			memcpy(p, pold, oldNumBytes);
			blockFree(pold, oldNumBytes);
			return p;
		}

		virtual void blockAllocBatch(void** ptrsOut, std::size_t numPtrs, std::size_t blockSize) noexcept
		{
			for (long i = 0; i < numPtrs; i++)
				ptrsOut[i] = blockAlloc(blockSize);
		}

		virtual void blockFreeBatch(void** ptrsIn, std::size_t numPtrs, std::size_t blockSize) noexcept
		{
			for (long i = 0; i < numPtrs; i++)
				blockFree(ptrsIn[i], blockSize);
		}

		virtual void getMemoryStatistics(class MemoryStatistics& u) noexcept
		{}

		virtual std::size_t getAllocatedSize(const void* obj, std::size_t nbytes) noexcept
		{
			return 0;
		}

		virtual void resetPeakMemoryStatistics() noexcept
		{}

		[[nodiscard]] virtual void* getExtendedInterface() noexcept
		{
			return nullptr;
		}
	};

	ModuleMemoryManager::ModuleMemoryManager() :
		Module("Module Memory Manager", &bPatchesMemoryManager)
	{}

	bool ModuleMemoryManager::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleMemoryManager::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		/////////////////////////////////////////////////////////////////////
		// MemoryManager
		/////////////////////////////////////////////////////////////////////

		using tuple_t = std::tuple<uint64_t, void*>;

		if (RELEX::IsRuntimeOG())
		{
			RELEX::WriteSafe(REL::ID(597736).address(), { 0xC3, 0x90 });
			*(std::uint32_t*)REL::ID(1570354).address() = 2;

			const std::array MMPatch
			{
				tuple_t{ 652767,	&MemoryManager<>::Alloc },		// RE::ID::MemoryManager::Allocate
				tuple_t{ 1582181,	&MemoryManager<>::Dealloc },	// RE::ID::MemoryManager::Deallocate
				tuple_t{ 1502917,	&MemoryManager<>::Realloc },	// RE::ID::MemoryManager::Reallocate
				tuple_t{ 1453698,	&MemoryManager<>::Size },
			};

			for (const auto& [id, func] : MMPatch)
				RELEX::DetourJump(REL::ID(id).address(), (uintptr_t)func);
		}
		else
		{
			RELEX::WriteSafe(REL::ID(2267875).address(), { 0xC3, 0x90 });
			*(std::uint32_t*)REL::ID(RELEX::IsRuntimeAE() ? 4807763 : 2688723).address() = 2;

			const std::array MMPatch
			{
				tuple_t{ 2267872,	&MemoryManager<>::Alloc },		// RE::ID::MemoryManager::Allocate
				tuple_t{ 2267874,	&MemoryManager<>::Dealloc },	// RE::ID::MemoryManager::Deallocate
				tuple_t{ 2267873,	&MemoryManager<>::Realloc },	// RE::ID::MemoryManager::Reallocate
				tuple_t{ 2267858,	&MemoryManager<>::Size },
			};

			for (const auto& [id, func] : MMPatch)
				RELEX::DetourJump(REL::ID(id).address(), (uintptr_t)func);
		}

		/////////////////////////////////////////////////////////////////////
		// Replacement of all functions of the standard allocator
		/////////////////////////////////////////////////////////////////////

		auto base = REL::Module::GetSingleton()->base();

		RELEX::DetourIAT(base, "API-MS-WIN-CRT-HEAP-L1-1-0.DLL", "realloc",			(uintptr_t)&StdStuff<>::realloc);
		RELEX::DetourIAT(base, "API-MS-WIN-CRT-HEAP-L1-1-0.DLL", "calloc",			(uintptr_t)&StdStuff<>::calloc);
		RELEX::DetourIAT(base, "API-MS-WIN-CRT-HEAP-L1-1-0.DLL", "_aligned_malloc",	(uintptr_t)&StdStuff<>::aligned_malloc);
		RELEX::DetourIAT(base, "API-MS-WIN-CRT-HEAP-L1-1-0.DLL", "malloc",			(uintptr_t)&StdStuff<>::malloc);
		RELEX::DetourIAT(base, "API-MS-WIN-CRT-HEAP-L1-1-0.DLL", "_aligned_free",	(uintptr_t)&StdStuff<>::aligned_free);
		RELEX::DetourIAT(base, "API-MS-WIN-CRT-HEAP-L1-1-0.DLL", "free",			(uintptr_t)&StdStuff<>::free);
		RELEX::DetourIAT(base, "API-MS-WIN-CRT-HEAP-L1-1-0.DLL", "_msize",			(uintptr_t)&StdStuff<>::msize);

		/////////////////////////////////////////////////////////////////////
		// Replacing memory manipulation functions with newer and more productive ones
		/////////////////////////////////////////////////////////////////////

		if (bAdditionalUseNewRedistributable.GetValue())
		{
			RELEX::DetourIAT(base, "msvcr110.dll", "memcmp", (uintptr_t)&memcmp);
			RELEX::DetourIAT(base, "msvcr110.dll", "memmove", (uintptr_t)&memmove);
			RELEX::DetourIAT(base, "msvcr110.dll", "memcpy", (uintptr_t)&memcpy);
			RELEX::DetourIAT(base, "msvcr110.dll", "memset", (uintptr_t)&memset);

			if (RELEX::IsRuntimeOG())
			{
				RELEX::DetourIAT(base, "msvcr110.dll", "memmove_s", (uintptr_t)&memmove_s);
				RELEX::DetourIAT(base, "msvcr110.dll", "memcpy_s", (uintptr_t)&memcpy_s);
			}
		}

		/////////////////////////////////////////////////////////////////////
		// AutoScrapHeap & ScrapHeap
		/////////////////////////////////////////////////////////////////////

		AutoScrapHeap::Install();
		ScrapHeap<>::Install();

		RELEX::DetourJump(REL::ID(RELEX::IsRuntimeOG() ? 760285 : 2281069).address(),
			(uintptr_t)&bhkThreadMemorySource<>::__ctor__);

		/////////////////////////////////////////////////////////////////////
		// Default/Static/File heaps
		/////////////////////////////////////////////////////////////////////

		RELEX::WriteSafe(REL::ID(RELEX::IsRuntimeOG() ? 433356 : 2228360).address(), { 0xC3, 0x90 });

		/////////////////////////////////////////////////////////////////////

		RE::MemoryManager::GetSingleton().RegisterMemoryManager();
		RE::BSThreadEvent::InitSDM();

		return true;
	}

	bool ModuleMemoryManager::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleMemoryManager::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}