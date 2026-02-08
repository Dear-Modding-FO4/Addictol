#pragma once

#include <stdint.h>
#include <REX\REX.h>
#include <concurrent_unordered_map.h>

namespace Addictol
{
	class MemoryTracer:
		public REX::Singleton<MemoryTracer>
	{
		struct Info
		{
			size_t size;
			const void* addrCall;
		};

		REX::W32::CRITICAL_SECTION s;
		concurrency::concurrent_unordered_map<uintptr_t, Info> data;

		MemoryTracer(const MemoryTracer&) = delete;
		MemoryTracer& operator=(const MemoryTracer&) = delete;
	public:
		MemoryTracer();
		virtual ~MemoryTracer();

		virtual void Add(const void* a_ptr, size_t a_size, const void* a_addrCall) noexcept;
		virtual void Remove(const void* a_ptr) noexcept;
		virtual void Dump() noexcept;
	};
}