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

	[[nodiscard]] bool IsRuntimeOG() noexcept;
	[[nodiscard]] bool IsRuntimeNG() noexcept;
	[[nodiscard]] bool IsRuntimeAE() noexcept;

	// thread-safe template versions of XCFastCall()

	template<typename TR>
	__forceinline TR FastCall(size_t reloff) noexcept
	{
		return ((TR(__fastcall*)())(reloff))();
	}

	template<typename TR, typename T1>
	__forceinline TR FastCall(size_t reloff, T1 a1) noexcept
	{
		return ((TR(__fastcall*)(T1))(reloff))(a1);
	}

	template<typename TR, typename T1, typename T2>
	__forceinline TR FastCall(size_t reloff, T1 a1, T2 a2) noexcept
	{
		return ((TR(__fastcall*)(T1, T2))(reloff))(a1, a2);
	}

	template<typename TR, typename T1, typename T2, typename T3>
	__forceinline TR FastCall(size_t reloff, T1 a1, T2 a2, T3 a3) noexcept
	{
		return ((TR(__fastcall*)(T1, T2, T3))(reloff))(a1, a2, a3);
	}

	template<typename TR, typename T1, typename T2, typename T3, typename T4>
	__forceinline TR FastCall(size_t reloff, T1 a1, T2 a2, T3 a3, T4 a4) noexcept
	{
		return ((TR(__fastcall*)(T1, T2, T3, T4))(reloff))(a1, a2, a3, a4);
	}

	template<typename TR, typename T1, typename T2, typename T3, typename T4, typename T5>
	__forceinline TR FastCall(size_t reloff, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5) noexcept
	{
		return ((TR(__fastcall*)(T1, T2, T3, T4, T5))(reloff))(a1, a2, a3, a4, a5);
	}

	template<typename TR, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
	__forceinline TR FastCall(size_t reloff, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6) noexcept
	{
		return ((TR(__fastcall*)(T1, T2, T3, T4, T5, T6))(reloff))(a1, a2, a3, a4, a5, a6);
	}

	template<typename TR, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7>
	__forceinline TR FastCall(size_t reloff, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7) noexcept
	{
		return ((TR(__fastcall*)(T1, T2, T3, T4, T5, T6, T7))(reloff))(a1, a2, a3, a4, a5, a6, a7);
	}

	template<typename TR, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8>
	__forceinline TR FastCall(size_t reloff, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8) noexcept
	{
		return ((TR(__fastcall*)(T1, T2, T3, T4, T5, T6, T7, T8))(reloff))(a1, a2, a3, a4, a5, a6, a7, a8);
	}

	// thread-safe template versions of XCThisVirtualCall()

	template<typename TR>
	__forceinline TR ThisVirtualCall(size_t reloff, const void* ths) {
		return (*(TR(__fastcall**)(const void*))(*(__int64*)ths + reloff))(ths);
	}

	template<typename TR, typename T1>
	__forceinline TR ThisVirtualCall(size_t reloff, const void* ths, T1 a1) {
		return (*(TR(__fastcall**)(const void*, T1))(*(__int64*)ths + reloff))(ths, a1);
	}

	template<typename TR, typename T1, typename T2>
	__forceinline TR ThisVirtualCall(size_t reloff, const void* ths, T1 a1, T2 a2) {
		return (*(TR(__fastcall**)(const void*, T1, T2))(*(__int64*)ths + reloff))(ths, a1, a2);
	}

	template<typename TR, typename T1, typename T2, typename T3>
	__forceinline TR ThisVirtualCall(size_t reloff, const void* ths, T1 a1, T2 a2, T3 a3) {
		return (*(TR(__fastcall**)(const void*, T1, T2, T3))(*(__int64*)ths + reloff))(ths, a1, a2, a3);
	}

	template<typename TR, typename T1, typename T2, typename T3, typename T4>
	__forceinline TR ThisVirtualCall(size_t reloff, const void* ths, T1 a1, T2 a2, T3 a3, T4 a4) {
		return (*(TR(__fastcall**)(const void*, T1, T2, T3, T4))(*(__int64*)ths + reloff))(ths, a1, a2, a3, a4);
	}

	template<typename TR, typename T1, typename T2, typename T3, typename T4, typename T5>
	__forceinline TR ThisVirtualCall(size_t reloff, const void* ths, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5) {
		return (*(TR(__fastcall**)(const void*, T1, T2, T3, T4, T5))(*(__int64*)ths + reloff))(ths, a1, a2, a3, a4, a4);
	}
}