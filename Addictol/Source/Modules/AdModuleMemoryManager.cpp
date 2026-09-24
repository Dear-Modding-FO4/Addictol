#include <Modules/AdModuleMemoryManager.h>
#include <Telemetry/AdAllocatorPoolTelemetry.h>
#include <Memory/AdProfiledHeap.h>
#include <Core/AdAssert.h>
#include <Memory/AdAllocator.h>
#include <Core/AdDetourBatch.h>
#include <Core/AdUtils.h>
#include <string.h>
#include <stdio.h>
#include <xbyak/xbyak.h>
#include <algorithm>
#include <tuple>

#if AD_TRACER
#	include <AdMemoryTracer.h>
#endif

#define AD_NO_EMPTYPOINTERS 1

#undef MEM_RELEASE
#undef ERROR

#include <RE/M/MemoryManager.h>
#include <RE/B/BSThreadEvent.h>

namespace Addictol
{
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
			if (!a_size)
			{
				Dealloc(a_self, a_block, a_alignment);
#if !AD_NO_EMPTYPOINTERS
				return (void*)(&EMPTY_POINTER);
#else
				return nullptr;
#endif
			}
#if AD_TRACER
			void* ptr = nullptr;
			auto ret_addr = _ReturnAddress();

#if !AD_NO_EMPTYPOINTERS
			if (a_block == (const void*)(&EMPTY_POINTER))
				ptr = a_alignment ?
					Heap::GetSingleton()->aligned_malloc(a_size, a_align) :
					Heap::GetSingleton()->malloc(a_size);
			else
#endif
			{
				MemoryTracer::GetSingleton()->Remove(a_block);

				ptr = a_alignment ?
					Heap::GetSingleton()->aligned_realloc(a_block, a_size, a_align) :
					Heap::GetSingleton()->realloc(a_block, a_size);
			}

			MemoryTracer::GetSingleton()->Add(ptr, a_size, ret_addr);
			return ptr;
#else
#if !AD_NO_EMPTYPOINTERS
			if (a_block == (const void*)(&EMPTY_POINTER))
				return Alloc(a_self, a_size, a_align, a_alignment);
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
			if (p)
			{
				const auto copyBytes = std::min(oldNumBytes, reqNumBytesInOut);
				if (copyBytes > 0)
					memcpy(p, pold, static_cast<size_t>(copyBytes));
			}
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

	// Times the game's own allocators without replacing them; Havok's private pool stays unobserved.
	class StockHeapObserver
	{
		enum Target : size_t { kAllocate, kDeallocate, kReallocate, kSize, kScrapAllocate, kScrapDeallocate, kCount };

		using Prologue = std::array<uint8_t, 16>;
		struct Entry
		{
			REL::VariantID id;
			Prologue og;
			Prologue ngae;
			void* replacement;
		};

		inline static std::array<RELEX::DetourTarget, kCount> s_targets{};

		struct Crt
		{
			const char* name;
			uintptr_t replacement;
			uintptr_t* original;
		};
		inline static uintptr_t s_malloc{}, s_calloc{}, s_realloc{}, s_alignedMalloc{}, s_free{}, s_alignedFree{}, s_msize{};

		template<Target T, class R, class... Args>
		[[nodiscard]] static R Original(Args... a_args) noexcept
		{
			return reinterpret_cast<R (*)(Args...)>(s_targets[T].original)(a_args...);
		}

		template<class R, class... Args>
		[[nodiscard]] static R Call(uintptr_t a_function, Args... a_args) noexcept
		{
			return reinterpret_cast<R (*)(Args...)>(a_function)(a_args...);
		}

		static void* Allocate(void* a_self, size_t a_size, uint32_t a_align, bool a_aligned) noexcept
		{
			const auto call = [&] { return Original<kAllocate, void*>(a_self, a_size, a_align, a_aligned); };
			return a_aligned ?
				ProfileHeapAllocation<HeapProfileSite::MemoryManager, HeapProfileOperation::AlignedAllocate>(nullptr, a_size, call) :
				ProfileHeapAllocation<HeapProfileSite::MemoryManager, HeapProfileOperation::Allocate>(nullptr, a_size, call);
		}

		static void* Reallocate(void* a_self, void* a_block, size_t a_size, uint32_t a_align, bool a_aligned) noexcept
		{
			const auto call = [&] { return Original<kReallocate, void*>(a_self, a_block, a_size, a_align, a_aligned); };
			return a_aligned ?
				ProfileHeapAllocation<HeapProfileSite::MemoryManager, HeapProfileOperation::AlignedReallocate>(a_block, a_size, call) :
				ProfileHeapAllocation<HeapProfileSite::MemoryManager, HeapProfileOperation::Reallocate>(a_block, a_size, call);
		}

		static void Deallocate(void* a_self, void* a_block, bool a_aligned) noexcept
		{
			const auto call = [&] { Original<kDeallocate, void>(a_self, a_block, a_aligned); };
			if (a_aligned)
				ProfileHeapCall<HeapProfileSite::MemoryManager, HeapProfileOperation::AlignedFree>(call);
			else
				ProfileHeapCall<HeapProfileSite::MemoryManager, HeapProfileOperation::Free>(call);
		}

		static size_t Size(void* a_self, void* a_block) noexcept
		{
			return ProfileHeapCall<HeapProfileSite::MemoryManager, HeapProfileOperation::Size>([&] {
				return Original<kSize, size_t>(a_self, a_block);
			});
		}

		static void* ScrapAllocate(void* a_self, size_t a_size, size_t a_align) noexcept
		{
			return ProfileHeapAllocation<HeapProfileSite::Scrap, HeapProfileOperation::AlignedAllocate>(nullptr, a_size, [&] {
				return Original<kScrapAllocate, void*>(a_self, a_size, a_align);
			});
		}

		static void ScrapDeallocate(void* a_self, void* a_block) noexcept
		{
			ProfileHeapCall<HeapProfileSite::Scrap, HeapProfileOperation::AlignedFree>([&] {
				Original<kScrapDeallocate, void>(a_self, a_block);
			});
		}

		static void* CrtMalloc(size_t a_size) noexcept
		{
			return ProfileHeapAllocation<HeapProfileSite::CRT, HeapProfileOperation::Allocate>(nullptr, a_size, [&] {
				return Call<void*>(s_malloc, a_size);
			});
		}

		static void* CrtCalloc(size_t a_count, size_t a_size) noexcept
		{
			const auto total = a_count && a_size > SIZE_MAX / a_count ? SIZE_MAX : a_count * a_size;
			return ProfileHeapAllocation<HeapProfileSite::CRT, HeapProfileOperation::Allocate>(nullptr, total, [&] {
				return Call<void*>(s_calloc, a_count, a_size);
			});
		}

		static void* CrtRealloc(void* a_block, size_t a_size) noexcept
		{
			return ProfileHeapAllocation<HeapProfileSite::CRT, HeapProfileOperation::Reallocate>(a_block, a_size, [&] {
				return Call<void*>(s_realloc, a_block, a_size);
			});
		}

		static void* CrtAlignedMalloc(size_t a_size, size_t a_alignment) noexcept
		{
			return ProfileHeapAllocation<HeapProfileSite::CRT, HeapProfileOperation::AlignedAllocate>(nullptr, a_size, [&] {
				return Call<void*>(s_alignedMalloc, a_size, a_alignment);
			});
		}

		static void CrtFree(void* a_block) noexcept
		{
			ProfileHeapCall<HeapProfileSite::CRT, HeapProfileOperation::Free>([&] { Call<void>(s_free, a_block); });
		}

		static void CrtAlignedFree(void* a_block) noexcept
		{
			ProfileHeapCall<HeapProfileSite::CRT, HeapProfileOperation::AlignedFree>([&] { Call<void>(s_alignedFree, a_block); });
		}

		static size_t CrtMsize(void* a_block) noexcept
		{
			return ProfileHeapCall<HeapProfileSite::CRT, HeapProfileOperation::Size>([&] { return Call<size_t>(s_msize, a_block); });
		}

	public:
		[[nodiscard]] static bool Install(uintptr_t a_base) noexcept
		{
			// Retail prologues; NG and AE are byte-identical here.
			static const std::array<Entry, kCount> entries{
				Entry{ RE::ID::MemoryManager::Allocate,
					{ 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57 },
					{ 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57 },
					reinterpret_cast<void*>(&Allocate) },
				Entry{ RE::ID::MemoryManager::Deallocate,
					{ 0x48, 0x85, 0xD2, 0x0F, 0x84, 0x0A, 0x01, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89 },
					{ 0x48, 0x85, 0xD2, 0x0F, 0x84, 0x0A, 0x01, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89 },
					reinterpret_cast<void*>(&Deallocate) },
				Entry{ RE::ID::MemoryManager::Reallocate,
					{ 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41 },
					{ 0x40, 0x53, 0x55, 0x57, 0x41, 0x54, 0x48, 0x83, 0xEC, 0x28, 0x33, 0xED, 0x41, 0x8B, 0xC1, 0x4D },
					reinterpret_cast<void*>(&Reallocate) },
				Entry{ RE::ID::MemoryManager::Size,
					{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x48, 0x8B, 0x89 },
					{ 0x40, 0x53, 0x56, 0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0xF2, 0x48, 0x8B, 0x89 },
					reinterpret_cast<void*>(&Size) },
				Entry{ RE::ID::ScrapHeap::Allocate,
					{ 0x4C, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x48, 0x89, 0x4C, 0x24, 0x08, 0x53 },
					{ 0x4C, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54 },
					reinterpret_cast<void*>(&ScrapAllocate) },
				Entry{ RE::ID::ScrapHeap::Deallocate,
					{ 0x48, 0x85, 0xD2, 0x0F, 0x84, 0x39, 0x01, 0x00, 0x00, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B },
					{ 0x48, 0x85, 0xD2, 0x0F, 0x84, 0x34, 0x01, 0x00, 0x00, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B },
					reinterpret_cast<void*>(&ScrapDeallocate) }
			};

			const auto og = RELEX::IsRuntimeOG();
			for (size_t index = 0; index < kCount; ++index)
			{
				const auto& entry = entries[index];
				const auto& prologue = og ? entry.og : entry.ngae;
				s_targets[index] = { reinterpret_cast<void*>(entry.id.address()), entry.replacement, prologue };
			}

			// Resolve CRT originals before the IAT is live so a concurrent call never sees a null target.
			const auto crt = REX::W32::GetModuleHandleA("API-MS-WIN-CRT-HEAP-L1-1-0.DLL");
			const std::array imports{
				Crt{ "malloc", reinterpret_cast<uintptr_t>(&CrtMalloc), &s_malloc },
				Crt{ "calloc", reinterpret_cast<uintptr_t>(&CrtCalloc), &s_calloc },
				Crt{ "realloc", reinterpret_cast<uintptr_t>(&CrtRealloc), &s_realloc },
				Crt{ "_aligned_malloc", reinterpret_cast<uintptr_t>(&CrtAlignedMalloc), &s_alignedMalloc },
				Crt{ "free", reinterpret_cast<uintptr_t>(&CrtFree), &s_free },
				Crt{ "_aligned_free", reinterpret_cast<uintptr_t>(&CrtAlignedFree), &s_alignedFree },
				Crt{ "_msize", reinterpret_cast<uintptr_t>(&CrtMsize), &s_msize }
			};
			for (const auto& import : imports)
			{
				*import.original = crt ? reinterpret_cast<uintptr_t>(REX::W32::GetProcAddress(crt, import.name)) : 0;
				if (!*import.original)
				{
					REX::ERROR("Stock allocator profiling: {} is not exported; engine allocators left unobserved."sv, import.name);
					return false;
				}
			}

			const auto result = RELEX::DetourBatch(s_targets);
			if (!result)
			{
				REX::ERROR("Stock allocator profiling: Detours error {} at target {}; engine allocators left unobserved."sv,
					result.error, result.target);
				return false;
			}
			for (const auto& import : imports)
			{
				if (const auto previous = RELEX::DetourIAT(a_base, "API-MS-WIN-CRT-HEAP-L1-1-0.DLL", import.name, import.replacement))
					*import.original = previous;
			}
			REX::INFO("Stock allocator profiling: observing the memory manager, scrap heap, and CRT heap imports."sv);
			return true;
		}
	};

	ModuleMemoryManager::ModuleMemoryManager() :		Module("Memory Manager", &bPatchesMemoryManager)
	{}

	bool ModuleMemoryManager::DoQuery() const noexcept
	{
		return ResolveHeapSelection(sAdditionalAllocator.GetValue());
	}

	bool ModuleMemoryManager::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		auto base = REX::FModule::GetExecutingModule().GetBaseAddress();

		REX::INFO("Memory allocator backend: {}"sv, HeapKindName(GetSelectedHeapKind()));

		if (GetSelectedHeapKind() == HeapKind::Stock)
		{
			if (PrepareHeapOperationProfile())
				(void)StockHeapObserver::Install(base);
		}
		else
		{
			InstallReplacementHeap(base);
			m_active.store(true, std::memory_order_relaxed);
		}

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

	void ModuleMemoryManager::InstallReplacementHeap(uintptr_t a_base) noexcept
	{
		AutoScrapHeap::Install();

		VisitSelectedHeap([base = a_base]<typename Heap>() {
			(void)InstallSelectedProfiledHeap<Heap, HeapProfileSite::MemoryManager>([]<class Selected>() {
				MemoryManager<Selected>::Install();
				return true;
			});
			(void)InstallSelectedProfiledHeap<Heap, HeapProfileSite::Scrap>([]<class Selected>() {
				ScrapHeap<Selected>::Install();
				return true;
			});
			(void)InstallSelectedProfiledHeap<Heap, HeapProfileSite::Havok>([]<class Selected>() {
				bhkThreadMemorySource<Selected>::Install();
				return true;
			});
			(void)InstallSelectedProfiledHeap<Heap, HeapProfileSite::CRT>([base]<class Selected>() {
				RELEX::DetourIAT(base, "API-MS-WIN-CRT-HEAP-L1-1-0.DLL", "realloc",			(uintptr_t)&StdStuff<Selected>::realloc);
				RELEX::DetourIAT(base, "API-MS-WIN-CRT-HEAP-L1-1-0.DLL", "calloc",			(uintptr_t)&StdStuff<Selected>::calloc);
				RELEX::DetourIAT(base, "API-MS-WIN-CRT-HEAP-L1-1-0.DLL", "_aligned_malloc",	(uintptr_t)&StdStuff<Selected>::aligned_malloc);
				RELEX::DetourIAT(base, "API-MS-WIN-CRT-HEAP-L1-1-0.DLL", "malloc",			(uintptr_t)&StdStuff<Selected>::malloc);
				RELEX::DetourIAT(base, "API-MS-WIN-CRT-HEAP-L1-1-0.DLL", "_aligned_free",	(uintptr_t)&StdStuff<Selected>::aligned_free);
				RELEX::DetourIAT(base, "API-MS-WIN-CRT-HEAP-L1-1-0.DLL", "free",			(uintptr_t)&StdStuff<Selected>::free);
				RELEX::DetourIAT(base, "API-MS-WIN-CRT-HEAP-L1-1-0.DLL", "_msize",			(uintptr_t)&StdStuff<Selected>::msize);
				return true;
			});
		});
	}


	std::span<const MetricDescriptor> ModuleMemoryManager::Schema() const noexcept
	{
		return AllocatorPoolTelemetry::Schema();
	}

	void ModuleMemoryManager::Drain(std::span<MetricValue> a_out) noexcept
	{
		HeapStatistics stats{};
		if (m_active.load(std::memory_order_relaxed))
			stats = VisitSelectedHeap([]<class Heap>() { return Heap::GetSingleton()->Statistics(); });
		AllocatorPoolTelemetry::Populate(a_out, stats);
	}

	bool ModuleMemoryManager::HasProcessDefender() noexcept
	{
		return true;
	}
}