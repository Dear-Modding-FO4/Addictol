#include <Modules/AdModuleMemoryManager.h>
#include <AdAssert.h>
#include <AdAllocator.h>
#include <AdUtils.h>
#include <string.h>
#include <stdio.h>
#include <xbyak/xbyak.h>
#include <tuple>

#if AD_TRACER
#	include <AdMemoryTracer.h>
#endif

#define AD_NO_EMPTYPOINTERS 1

#undef MEM_RELEASE

#include <RE/M/MemoryManager.h>
#include <RE/B/BSThreadEvent.h>

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
			RELEX::WriteSafeNop(REL::ID{ 1305199, 2267866 }.address() + 0x1D, 0x15);
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
			RELEX::WriteSafe(REL::ID{ 1557709, 2267868 }.address(), { 0xC3, 0x90, 0x90, 0x90 });

			CtorLong();
			CtorShort();
			Dtor();
		}
	};

	template<typename Heap>
	class ScrapHeap
	{
		ScrapHeap(const ScrapHeap&) = delete;
		ScrapHeap(ScrapHeap&&) = delete;
		ScrapHeap& operator=(const ScrapHeap&) = delete;
		ScrapHeap& operator=(ScrapHeap&&) = delete;

		ScrapHeap() = default;
		~ScrapHeap() = default;

		static void WriteStubs() noexcept
		{
			// Remove stuff
			constexpr static std::initializer_list<uint8_t> RET_NOP = { 0xC3, 0x90, 0x90, 0x90 };

			std::array<uint64_t, 6> stub
			{
				REL::ID{ 550677,	2267990 }.address(),		// Clean
				REL::ID{ 111657,	2267989 }.address(),		// ClearKeepPages
				REL::ID{ 975239,	2267993 }.address(),		// InsertFreeBlock
				REL::ID{ 84225,		2267994 }.address(),		// RemoveFreeBlock
				REL::ID{ 1255203,	2267988 }.address(),		// SetKeepPages
				REL::ID{ 912706,	2267982 }.address(),		// dtor
			};

			for (const auto& address : stub)
				RELEX::WriteSafe(address, RET_NOP);
		}

		static void WriteHooks() noexcept
		{
			RELEX::DetourJump(RE::ID::ScrapHeap::Allocate.address(), reinterpret_cast<uintptr_t>(&Allocate));
			RELEX::DetourJump(RE::ID::ScrapHeap::Deallocate.address(), reinterpret_cast<uintptr_t>(&Deallocate));
			RELEX::DetourJump(REL::ID{ 48809, 2267981 }.address(), reinterpret_cast<uintptr_t>(&Ctor));
		}
	public:
#if !AD_NO_EMPTYPOINTERS
		inline static const std::uint64_t EMPTY_POINTER{ 0 };
#endif
		[[nodiscard]] inline static RE::ScrapHeap* Ctor(RE::ScrapHeap* a_this)
		{
			std::memset(a_this, 0, sizeof(RE::ScrapHeap));
			emplace_vtable(a_this);
			return a_this;
		}

		[[nodiscard]] inline static void* Allocate([[maybe_unused]] ScrapHeap* a_this, 
			std::size_t a_size, std::size_t a_align) noexcept(true)
		{
#if !AD_NO_EMPTYPOINTERS
			if (!a_size)
				return (void*)(&EMPTY_POINTER);
#endif
#if AD_TRACER
			auto ret_addr = _ReturnAddress();
			auto ptr = Heap::GetSingleton()->aligned_malloc(a_size, a_align);
			MemoryTracer::GetSingleton()->Add(ptr, a_size, ret_addr);
			return ptr;
#else
			return Heap::GetSingleton()->aligned_malloc(a_size, a_align);
#endif
		}

		inline static void Deallocate([[maybe_unused]] ScrapHeap* a_this, void* a_block) noexcept(true)
		{
#if !AD_NO_EMPTYPOINTERS
			if (a_block == (const void*)(&EMPTY_POINTER))
				return;
#endif
#if AD_TRACER
			MemoryTracer::GetSingleton()->Remove(a_block);
#endif
			Heap::GetSingleton()->aligned_free(a_block);
		}

		static void Install()
		{
			WriteStubs();
			WriteHooks();

			/////////////////////////////////////////////////////////////////////
			// Default/Static/File heaps
			/////////////////////////////////////////////////////////////////////

			RELEX::WriteSafe(REL::ID{ 433356, 2228360 }.address(), { 0xC3, 0x90 });
		}
	};

	template<typename Heap>
	class MemoryManager
	{
		MemoryManager(const MemoryManager&) = delete;
		MemoryManager(MemoryManager&&) = delete;
		MemoryManager& operator=(const MemoryManager&) = delete;
		MemoryManager& operator=(MemoryManager&&) = delete;

		MemoryManager() = default;
		~MemoryManager() = default;
	public:
#if !AD_NO_EMPTYPOINTERS
		inline static const uint64_t EMPTY_POINTER{ 0 };
#endif
		[[nodiscard]] static void* Alloc([[maybe_unused]] MemoryManager* a_self, size_t a_size,
			uint32_t a_align, bool a_alignment) noexcept
		{
#if !AD_NO_EMPTYPOINTERS
			if (!a_size)
				return (void*)(&EMPTY_POINTER);
#else
			if (!a_size)
				return nullptr;
#endif
#if AD_TRACER
			auto ret_addr = _ReturnAddress();
			auto ptr = a_alignment ?
				Heap::GetSingleton()->aligned_malloc(a_size, a_align) :
				Heap::GetSingleton()->malloc(a_size);
			MemoryTracer::GetSingleton()->Add(ptr, a_size, ret_addr);
			return ptr;
#else
			return a_alignment ?
				Heap::GetSingleton()->aligned_malloc(a_size, a_align) :
				Heap::GetSingleton()->malloc(a_size);
#endif
		}

		[[nodiscard]] static void* Realloc([[maybe_unused]] MemoryManager* a_self, void* a_block, size_t a_size,
			uint32_t a_align, bool a_alignment) noexcept
		{
#if AD_TRACER
			void* ptr = nullptr;
			auto ret_addr = _ReturnAddress();

#if !AD_NO_EMPTYPOINTERS
			if (a_block == (const void*)(&EMPTY_POINTER))
				ptr = a_alignment ?
					Heap::GetSingleton()->aligned_malloc(a_size, nAlignment) :
					Heap::GetSingleton()->malloc(a_size);
			else
#endif
			{
				MemoryTracer::GetSingleton()->Remove(a_block);

				ptr = a_alignment ?
					Heap::GetSingleton()->aligned_realloc(a_block, a_size, nAlignment) :
					Heap::GetSingleton()->realloc(a_block, a_size);
			}

			MemoryTracer::GetSingleton()->Add(ptr, a_size, ret_addr);
			return ptr;
#else
#if !AD_NO_EMPTYPOINTERS
			if (a_block == (const void*)(&EMPTY_POINTER))
				return Alloc(lpSelf, a_size, nAlignment, a_alignment);
#else
			if (!a_size)
				return nullptr;
#endif
			return a_alignment ?
				Heap::GetSingleton()->aligned_realloc(a_block, a_size, a_align) :
				Heap::GetSingleton()->realloc(a_block, a_size);
#endif
		}

		static void Dealloc([[maybe_unused]] MemoryManager* a_self, void* a_block, bool a_alignment) noexcept
		{
#if !AD_NO_EMPTYPOINTERS
			if (a_block == (const void*)(&EMPTY_POINTER))
				return;
#endif
#if AD_TRACER
			MemoryTracer::GetSingleton()->Remove(a_block);
#endif
			if (a_alignment)
				Heap::GetSingleton()->aligned_free(a_block);
			else
				Heap::GetSingleton()->free(a_block);
		}

		[[nodiscard]] static std::size_t Size([[maybe_unused]] MemoryManager* a_self, void* a_block) noexcept
		{
#if !AD_NO_EMPTYPOINTERS
			if (a_block == (const void*)(&EMPTY_POINTER))
				return 0;
#endif
			return Heap::GetSingleton()->msize(a_block);
		}

		static void Install() noexcept
		{
			/////////////////////////////////////////////////////////////////////
			// Init stub
			/////////////////////////////////////////////////////////////////////

			RELEX::WriteSafe(REL::ID{ 597736, 2267875 }.address(), { 0xC3, 0x90 });
			*(uint32_t*)REL::ID{ 1570354, 2688723, 4807763 }.address() = 2;

			/////////////////////////////////////////////////////////////////////
			// Functions stub
			/////////////////////////////////////////////////////////////////////

			RELEX::DetourJump(RE::ID::MemoryManager::Allocate.address(), (uintptr_t)&MemoryManager::Alloc);
			RELEX::DetourJump(RE::ID::MemoryManager::Deallocate.address(), (uintptr_t)&MemoryManager::Dealloc);
			RELEX::DetourJump(RE::ID::MemoryManager::Reallocate.address(), (uintptr_t)&MemoryManager::Realloc);
			RELEX::DetourJump(RE::ID::MemoryManager::Size.address(), (uintptr_t)&MemoryManager::Size);
			
			/////////////////////////////////////////////////////////////////////
			// Fake register
			/////////////////////////////////////////////////////////////////////

			RE::MemoryManager::GetSingleton().RegisterMemoryManager();
			RE::BSThreadEvent::InitSDM();
		}
	};

	template<typename Heap>
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

		[[nodiscard]] virtual void* blockAlloc(std::int32_t numBytes) noexcept
		{
			return Heap::GetSingleton()->aligned_malloc(numBytes, 16);
		}

		virtual void blockFree(void* p, std::int32_t numBytes) noexcept
		{
			Heap::GetSingleton()->aligned_free(p);
		}

		[[nodiscard]] virtual void* bufAlloc(std::int32_t& reqNumBytesInOut) noexcept
		{
			return blockAlloc(reqNumBytesInOut);
		}

		virtual void bufFree(void* p, std::int32_t numBytes) noexcept
		{
			return blockFree(p, numBytes);
		}

		[[nodiscard]] virtual void* bufRealloc(void* pold, std::int32_t oldNumBytes, std::int32_t& reqNumBytesInOut) noexcept
		{
			void* p = blockAlloc(reqNumBytesInOut);
			if (!p)
				return pold;
			memcpy(p, pold, oldNumBytes);
			blockFree(pold, oldNumBytes);
			return p;
		}

		virtual void blockAllocBatch(void** ptrsOut, std::int32_t numPtrs, std::int32_t blockSize) noexcept
		{
			for (long i = 0; i < numPtrs; i++)
				ptrsOut[i] = blockAlloc(blockSize);
		}

		virtual void blockFreeBatch(void** ptrsIn, std::int32_t numPtrs, std::int32_t blockSize) noexcept
		{
			for (long i = 0; i < numPtrs; i++)
				blockFree(ptrsIn[i], blockSize);
		}

		virtual void getMemoryStatistics(class MemoryStatistics& u) noexcept
		{}

		virtual size_t getAllocatedSize(const void* obj, std::int32_t nbytes) noexcept
		{
			return 0;
		}

		virtual void resetPeakMemoryStatistics() noexcept
		{}

		[[nodiscard]] virtual void* getExtendedInterface() noexcept
		{
			return nullptr;
		}

		static void Install() noexcept
		{
			RELEX::DetourJump(REL::ID{ 760285, 2281069 }.address(), reinterpret_cast<uintptr_t>(&__ctor__));
		}
	};

	ModuleMemoryManager::ModuleMemoryManager() :
		Module("Memory Manager", &bPatchesMemoryManager)
	{}

	bool ModuleMemoryManager::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleMemoryManager::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		AutoScrapHeap::Install();
		MemoryManager<ProxyCurrentHeap>::Install();
		ScrapHeap<ProxyCurrentHeap>::Install();
		bhkThreadMemorySource<ProxyCurrentHeap>::Install();

		/////////////////////////////////////////////////////////////////////
		// Replacement of all functions of the standard allocator
		/////////////////////////////////////////////////////////////////////

		auto base = REX::FModule::GetExecutingModule().GetBaseAddress();

		RELEX::DetourIAT(base, "API-MS-WIN-CRT-HEAP-L1-1-0.DLL", "realloc",			(uintptr_t)&StdStuff<ProxyCurrentHeap>::realloc);
		RELEX::DetourIAT(base, "API-MS-WIN-CRT-HEAP-L1-1-0.DLL", "calloc",			(uintptr_t)&StdStuff<ProxyCurrentHeap>::calloc);
		RELEX::DetourIAT(base, "API-MS-WIN-CRT-HEAP-L1-1-0.DLL", "_aligned_malloc",	(uintptr_t)&StdStuff<ProxyCurrentHeap>::aligned_malloc);
		RELEX::DetourIAT(base, "API-MS-WIN-CRT-HEAP-L1-1-0.DLL", "malloc",			(uintptr_t)&StdStuff<ProxyCurrentHeap>::malloc);
		RELEX::DetourIAT(base, "API-MS-WIN-CRT-HEAP-L1-1-0.DLL", "_aligned_free",	(uintptr_t)&StdStuff<ProxyCurrentHeap>::aligned_free);
		RELEX::DetourIAT(base, "API-MS-WIN-CRT-HEAP-L1-1-0.DLL", "free",			(uintptr_t)&StdStuff<ProxyCurrentHeap>::free);
		RELEX::DetourIAT(base, "API-MS-WIN-CRT-HEAP-L1-1-0.DLL", "_msize",			(uintptr_t)&StdStuff<ProxyCurrentHeap>::msize);

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

		return true;
	}

	bool ModuleMemoryManager::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleMemoryManager::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}