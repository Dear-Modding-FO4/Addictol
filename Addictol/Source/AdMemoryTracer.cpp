#include <AdMemoryTracer.h>
#include <REX\REX\LOG.h>
#include <REL\Module.h>

namespace Addictol
{
#if AD_TRACER
	MemoryTracer::MemoryTracer()
	{
		REX::W32::InitializeCriticalSection(&s);
	}

	MemoryTracer::~MemoryTracer()
	{
		REX::W32::DeleteCriticalSection(&s);
	}

	void MemoryTracer::Add(const void* a_ptr, size_t a_size, const void* a_addrCall) noexcept
	{
		REX::W32::EnterCriticalSection(&s);
		data.insert({ (uintptr_t)a_ptr, { a_size, (const void*)((uintptr_t)a_addrCall - REL::Module::GetSingleton()->base()) } });
		REX::W32::LeaveCriticalSection(&s);
	}

	void MemoryTracer::Remove(const void* a_ptr) noexcept
	{
		REX::W32::EnterCriticalSection(&s);
		data.unsafe_erase((uintptr_t)a_ptr);
		REX::W32::LeaveCriticalSection(&s);
	}

	void MemoryTracer::Dump() noexcept
	{
		REX::W32::EnterCriticalSection(&s);
		for (auto& i : data)
			REX::INFO("[DBG] ptr {:016X} size {} called {:016X}"sv, static_cast<uintptr_t>(i.first),
				static_cast<size_t>(i.second.size), static_cast<uintptr_t>((uintptr_t)i.second.addrCall));
		REX::W32::LeaveCriticalSection(&s);
	}
#endif
}