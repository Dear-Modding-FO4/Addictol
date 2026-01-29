#pragma once

#include <string>
#include <initializer_list>
#include <stdint.h>

std::string AdGetRuntimePath() noexcept;
std::string AdGetRuntimeDirectory() noexcept;

namespace RELEX
{
	class ScopeLock
	{
		bool _locked;
		uint32_t _old;
		uintptr_t _target, _size;

		ScopeLock(const ScopeLock&) = delete;
		ScopeLock operator=(const ScopeLock&) = delete;
	public:
		ScopeLock(uintptr_t a_target, uintptr_t a_size) noexcept;
		ScopeLock(void* a_target, uintptr_t a_size) noexcept;
		virtual ~ScopeLock() noexcept;

		[[nodiscard]] inline virtual bool HasUnlocked() const noexcept(true) { return _locked; }
	};

	void WriteSafe(uintptr_t a_target, std::initializer_list<uint8_t> a_data) noexcept;
	void WriteSafeNop(uintptr_t a_target, size_t a_size) noexcept;
}