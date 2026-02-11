#pragma once

#include <string>
#include <initializer_list>
#include <stdint.h>
#include <F4SE\Impl\PCH.h>

std::string AdGetRuntimePath() noexcept;
std::string AdGetRuntimeDirectory() noexcept;

#define AD_DECLARE_CONSTRUCTOR_HOOK(Class) \
	static Class *__ctor__(void *Instance) \
	{ \
		return new (Instance) Class(); \
	} \
	\
	static Class *__dtor__(Class *Thisptr, unsigned __int8) \
	{ \
		Thisptr->~Class(); \
		return Thisptr; \
	}

namespace RELEX
{
	class ScopeLock
	{
		bool _unlocked;
		uint32_t _old;
		uintptr_t _target, _size;

		ScopeLock(const ScopeLock&) = delete;
		ScopeLock operator=(const ScopeLock&) = delete;
	public:
		ScopeLock(uintptr_t a_target, uintptr_t a_size) noexcept;
		ScopeLock(void* a_target, uintptr_t a_size) noexcept;
		virtual ~ScopeLock() noexcept;

		[[nodiscard]] inline virtual bool HasUnlocked() const noexcept(true) { return _unlocked; }
	};

	void Write(uintptr_t a_target, const std::initializer_list<uint8_t>& a_data) noexcept;
	void WriteNop(uintptr_t a_target, size_t a_size) noexcept;
	void WriteSafe(uintptr_t a_target, const std::initializer_list<uint8_t>& a_data) noexcept;
	void WriteSafeNop(uintptr_t a_target, size_t a_size) noexcept;

	template<typename T>
	inline void WriteT(uintptr_t a_target, T& Data) noexcept
	{
		Write(a_target, reinterpret_cast<uint8_t*>(&Data), sizeof(T));
	}

	template<typename T>
	inline void WriteSafeT(uintptr_t a_target, T& Data) noexcept
	{
		WriteSafe(a_target, reinterpret_cast<uint8_t*>(&Data), sizeof(T));
	}

	[[nodiscard]] bool IsRuntimeOG() noexcept;
	[[nodiscard]] bool IsRuntimeNG() noexcept;
	[[nodiscard]] bool IsRuntimeAE() noexcept;

	uintptr_t DetourJump(uintptr_t a_target, uintptr_t a_function) noexcept;
	uintptr_t DetourCall(uintptr_t a_target, uintptr_t a_function) noexcept;
	uintptr_t DetourVTable(uintptr_t a_target, uintptr_t a_function, uint32_t a_index) noexcept;
	uintptr_t DetourIAT(const char* a_importModule, const char* a_functionName, uintptr_t a_function) noexcept;
	uintptr_t DetourIAT(uintptr_t a_targetModule, const char* a_importModule, const char* a_functionName, uintptr_t a_function) noexcept;
	uintptr_t DetourIATDelayed(const char* a_importModule, const char* a_functionName, uintptr_t a_function) noexcept;
	uintptr_t DetourIATDelayed(uintptr_t a_targetModule, const char* a_importModule, const char* a_functionName, uintptr_t a_function) noexcept;

	// Redirects a class member virtual function (__thiscall) to another
	template<typename T>
	[[nodiscard]] inline static uintptr_t DetourClassVTable(uintptr_t a_target, T a_function, uint32_t a_index) noexcept
	{
		return DetourVTable(a_target, *(uintptr_t*)&a_function, a_index);
	}

	template<typename T>
	[[nodiscard]] static T* GetTSingletonByID(uintptr_t a_id) noexcept
	{
		static REL::Relocation<T**> singleton{ REL::ID{ a_id } };
		return *singleton;
	}

	template<typename T>
	[[nodiscard]] static T* GetTSingletonByID(uintptr_t a_id, uintptr_t a_id_ng, uintptr_t a_id_og) noexcept
	{
		static REL::Relocation<T**> singleton{ IsRuntimeOG() ? REL::ID{ a_id_og } : IsRuntimeAE() ? REL::ID{ a_id } : REL::ID{ a_id_ng } };
		return *singleton;
	}

	template<typename T>
	[[nodiscard]] static T* GetTFunctionByID(uintptr_t a_id) noexcept
	{
		static REL::Relocation<T*> singleton{ REL::ID{ a_id } };
		return singleton.get();
	}

	template<typename T>
	[[nodiscard]] static T* GetTFunctionByID(uintptr_t a_id, uintptr_t a_id_ng, uintptr_t a_id_og) noexcept
	{
		static REL::Relocation<T*> singleton{ IsRuntimeOG() ? REL::ID{ a_id_og } : IsRuntimeAE() ? REL::ID{ a_id } : REL::ID{ a_id_ng } };
		return singleton.get();
	}

	// thread-safe template versions of FastCall()

	template<typename TR>
	__forceinline TR FastCall(size_t a_reloff) noexcept
	{
		return ((TR(__fastcall*)())(a_reloff))();
	}

	template<typename TR, typename T1>
	__forceinline TR FastCall(size_t a_reloff, T1 a1) noexcept
	{
		return ((TR(__fastcall*)(T1))(a_reloff))(a1);
	}

	template<typename TR, typename T1, typename T2>
	__forceinline TR FastCall(size_t a_reloff, T1 a1, T2 a2) noexcept
	{
		return ((TR(__fastcall*)(T1, T2))(a_reloff))(a1, a2);
	}

	template<typename TR, typename T1, typename T2, typename T3>
	__forceinline TR FastCall(size_t a_reloff, T1 a1, T2 a2, T3 a3) noexcept
	{
		return ((TR(__fastcall*)(T1, T2, T3))(a_reloff))(a1, a2, a3);
	}

	template<typename TR, typename T1, typename T2, typename T3, typename T4>
	__forceinline TR FastCall(size_t a_reloff, T1 a1, T2 a2, T3 a3, T4 a4) noexcept
	{
		return ((TR(__fastcall*)(T1, T2, T3, T4))(a_reloff))(a1, a2, a3, a4);
	}

	template<typename TR, typename T1, typename T2, typename T3, typename T4, typename T5>
	__forceinline TR FastCall(size_t a_reloff, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5) noexcept
	{
		return ((TR(__fastcall*)(T1, T2, T3, T4, T5))(a_reloff))(a1, a2, a3, a4, a5);
	}

	template<typename TR, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
	__forceinline TR FastCall(size_t a_reloff, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6) noexcept
	{
		return ((TR(__fastcall*)(T1, T2, T3, T4, T5, T6))(a_reloff))(a1, a2, a3, a4, a5, a6);
	}

	template<typename TR, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7>
	__forceinline TR FastCall(size_t a_reloff, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7) noexcept
	{
		return ((TR(__fastcall*)(T1, T2, T3, T4, T5, T6, T7))(a_reloff))(a1, a2, a3, a4, a5, a6, a7);
	}

	template<typename TR, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8>
	__forceinline TR FastCall(size_t a_reloff, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6, T7 a7, T8 a8) noexcept
	{
		return ((TR(__fastcall*)(T1, T2, T3, T4, T5, T6, T7, T8))(a_reloff))(a1, a2, a3, a4, a5, a6, a7, a8);
	}

	// thread-safe template versions of ThisVirtualCall()

	template<typename TR>
	__forceinline TR ThisVirtualCall(size_t a_reloff, const void* a_this) {
		return (*(TR(__fastcall**)(const void*))(*(__int64*)a_this + a_reloff))(a_this);
	}

	template<typename TR, typename T1>
	__forceinline TR ThisVirtualCall(size_t a_reloff, const void* a_this, T1 a1) {
		return (*(TR(__fastcall**)(const void*, T1))(*(__int64*)a_this + a_reloff))(a_this, a1);
	}

	template<typename TR, typename T1, typename T2>
	__forceinline TR ThisVirtualCall(size_t a_reloff, const void* a_this, T1 a1, T2 a2) {
		return (*(TR(__fastcall**)(const void*, T1, T2))(*(__int64*)a_this + a_reloff))(a_this, a1, a2);
	}

	template<typename TR, typename T1, typename T2, typename T3>
	__forceinline TR ThisVirtualCall(size_t a_reloff, const void* a_this, T1 a1, T2 a2, T3 a3) {
		return (*(TR(__fastcall**)(const void*, T1, T2, T3))(*(__int64*)a_this + a_reloff))(a_this, a1, a2, a3);
	}

	template<typename TR, typename T1, typename T2, typename T3, typename T4>
	__forceinline TR ThisVirtualCall(size_t a_reloff, const void* a_this, T1 a1, T2 a2, T3 a3, T4 a4) {
		return (*(TR(__fastcall**)(const void*, T1, T2, T3, T4))(*(__int64*)a_this + a_reloff))(a_this, a1, a2, a3, a4);
	}

	template<typename TR, typename T1, typename T2, typename T3, typename T4, typename T5>
	__forceinline TR ThisVirtualCall(size_t a_reloff, const void* a_this, T1 a1, T2 a2, T3 a3, T4 a4, T5 a5) {
		return (*(TR(__fastcall**)(const void*, T1, T2, T3, T4, T5))(*(__int64*)a_this + a_reloff))(a_this, a1, a2, a3, a4, a4);
	}
}

namespace Addictol
{
	[[nodiscard]] uint32_t Extend16(uint32_t a_in) noexcept;
	[[nodiscard]] uint32_t Extend8(uint32_t a_in) noexcept;
	[[nodiscard]] uint16_t Swap16(uint16_t a_in) noexcept;
	[[nodiscard]] uint32_t Swap32(uint32_t a_in) noexcept;
	[[nodiscard]] uint64_t Swap64(uint64_t a_in) noexcept;
	void SwapFloat(float* a_in) noexcept;
	void SwapDouble(double* in) noexcept;
	[[nodiscard]] bool IsBigEndian() noexcept;
	[[nodiscard]] bool IsLittleEndian() noexcept;
	std::string& Trim(std::string& String) noexcept;

	bool WriteINISettingInt(const wchar_t* a_INIFile, const wchar_t* a_optionName, long a_value) noexcept;
	bool WriteINISettingFloat(const wchar_t* a_INIFile, const wchar_t* a_optionName, float a_value) noexcept;
	bool WriteINISettingString(const wchar_t* a_INIFile, const wchar_t* a_optionName, const wchar_t* a_value) noexcept;

	[[nodiscard]] std::string WideToSysChar(const std::wstring& s) noexcept;
	[[nodiscard]] std::wstring SysCharToWide(const std::string& s) noexcept;

	[[nodiscard]] const char* GetSaveFolderName() noexcept;
}