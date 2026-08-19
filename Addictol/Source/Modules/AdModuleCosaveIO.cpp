#include <Modules/AdModuleCosaveIO.h>
#include <Core/AdUtils.h>

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <unordered_map>

namespace Addictol
{
	static REX::TOML::Bool<> bCosaveIO{ "Fixes"sv, "bCosaveIO"sv, true };

	namespace cosaveIODetail
	{
		constexpr std::uint64_t kMaxCosaveSize = 512ull * 1024ull * 1024ull;

		using CreateFileA_t = decltype(&::CreateFileA);
		using CreateFileW_t = decltype(&::CreateFileW);
		using ReadFile_t = decltype(&::ReadFile);
		using SetFilePointerEx_t = decltype(&::SetFilePointerEx);
		using CloseHandle_t = decltype(&::CloseHandle);

		static std::atomic<CreateFileA_t> s_createFileA{ nullptr };
		static std::atomic<CreateFileW_t> s_createFileW{ nullptr };
		static std::atomic<ReadFile_t> s_readFile{ nullptr };
		static std::atomic<SetFilePointerEx_t> s_setFilePointerEx{ nullptr };
		static std::atomic<CloseHandle_t> s_closeHandle{ nullptr };
		static std::atomic_bool s_enabled{ false };
		static std::atomic_bool s_installAttempted{ false };

		struct BufferedReadFile
		{
			std::unique_ptr<std::byte[]> buffer;
			std::uint64_t size{ 0 };
			std::uint64_t cursor{ 0 };
		};

		static std::mutex s_filesLock;
		static std::unordered_map<HANDLE, BufferedReadFile> s_files;

		enum class OffsetResult
		{
			kSuccess,
			kNegative,
			kOverflow
		};

		template <class T>
		static T WaitForOriginal(const std::atomic<T>& a_original) noexcept
		{
			T original = nullptr;
			while (!(original = a_original.load(std::memory_order_acquire)))
				YieldProcessor();
			return original;
		}

		static bool HasCosaveExtension(const char* a_path) noexcept
		{
			if (!a_path)
				return false;

			constexpr char suffix[] = ".f4se";
			const auto length = std::strlen(a_path);
			return length >= std::size(suffix) - 1 &&
				::_stricmp(a_path + length - (std::size(suffix) - 1), suffix) == 0;
		}

		static bool HasCosaveExtension(const wchar_t* a_path) noexcept
		{
			if (!a_path)
				return false;

			constexpr wchar_t suffix[] = L".f4se";
			const auto length = std::wcslen(a_path);
			return length >= std::size(suffix) - 1 &&
				::_wcsicmp(a_path + length - (std::size(suffix) - 1), suffix) == 0;
		}

		static bool IsReadOnlyOpen(DWORD a_desiredAccess, DWORD a_creationDisposition) noexcept
		{
			// clearing GENERIC_WRITE alone is not enough, the specific rights grant writes on their own
			constexpr DWORD writeRights = GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA;

			return (a_desiredAccess & GENERIC_READ) != 0 &&
				(a_desiredAccess & writeRights) == 0 &&
				a_creationDisposition == OPEN_EXISTING;
		}

		static bool ResetFilePointer(HANDLE a_file) noexcept
		{
			LARGE_INTEGER zero{};
			return WaitForOriginal(s_setFilePointerEx)(a_file, zero, nullptr, FILE_BEGIN) != FALSE;
		}

		static bool TryBufferReadFile(HANDLE a_file, BufferedReadFile& a_bufferedFile) noexcept
		{
			LARGE_INTEGER zero{};
			LARGE_INTEGER end{};
			if (!WaitForOriginal(s_setFilePointerEx)(a_file, zero, &end, FILE_END))
			{
				ResetFilePointer(a_file);
				return false;
			}

			if (!ResetFilePointer(a_file))
				return false;

			if (end.QuadPart <= 0 || static_cast<std::uint64_t>(end.QuadPart) > kMaxCosaveSize)
				return false;

			const auto size = static_cast<std::uint64_t>(end.QuadPart);
			std::unique_ptr<std::byte[]> buffer{ new (std::nothrow) std::byte[static_cast<std::size_t>(size)] };
			if (!buffer)
				return false;

			std::uint64_t offset = 0;
			while (offset < size)
			{
				const auto request = static_cast<DWORD>(size - offset);
				DWORD bytesRead = 0;
				if (!WaitForOriginal(s_readFile)(
					a_file, buffer.get() + static_cast<std::size_t>(offset), request, &bytesRead, nullptr) ||
					bytesRead == 0 || bytesRead > request)
				{
					ResetFilePointer(a_file);
					return false;
				}

				offset += bytesRead;
			}

			if (!ResetFilePointer(a_file))
				return false;

			a_bufferedFile.buffer = std::move(buffer);
			a_bufferedFile.size = size;
			a_bufferedFile.cursor = 0;
			return true;
		}

		static void TrackReadFile(HANDLE a_file) noexcept
		{
			BufferedReadFile bufferedFile;
			if (!TryBufferReadFile(a_file, bufferedFile))
				return;

			try
			{
				std::lock_guard lock{ s_filesLock };
				s_files.insert_or_assign(a_file, std::move(bufferedFile));
			}
			catch (...)
			{}
		}

		static OffsetResult AddOffset(std::uint64_t a_base, std::int64_t a_offset, std::uint64_t& a_result) noexcept
		{
			if (a_offset < 0)
			{
				const auto magnitude = static_cast<std::uint64_t>(-(a_offset + 1)) + 1;
				if (a_base < magnitude)
					return OffsetResult::kNegative;

				a_result = a_base - magnitude;
				return OffsetResult::kSuccess;
			}

			const auto magnitude = static_cast<std::uint64_t>(a_offset);
			if (a_base > std::numeric_limits<std::uint64_t>::max() - magnitude)
				return OffsetResult::kOverflow;

			a_result = a_base + magnitude;
			return OffsetResult::kSuccess;
		}

		static BOOL CopyReadData(void* a_destination, const void* a_source, DWORD a_length,
			LPDWORD a_bytesRead) noexcept
		{
			__try
			{
				*a_bytesRead = 0;
				if (a_length != 0)
					std::memcpy(a_destination, a_source, a_length);
				*a_bytesRead = a_length;
				return TRUE;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				::SetLastError(ERROR_NOACCESS);
				return FALSE;
			}
		}

		static BOOL WriteNewFilePointer(LARGE_INTEGER* a_newFilePointer, std::uint64_t a_position) noexcept
		{
			if (!a_newFilePointer)
				return TRUE;

			__try
			{
				a_newFilePointer->QuadPart = static_cast<LONGLONG>(a_position);
				return TRUE;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				::SetLastError(ERROR_NOACCESS);
				return FALSE;
			}
		}

		static HANDLE WINAPI HookedCreateFileA(LPCSTR a_fileName, DWORD a_desiredAccess, DWORD a_shareMode,
			LPSECURITY_ATTRIBUTES a_securityAttributes, DWORD a_creationDisposition, DWORD a_flagsAndAttributes,
			HANDLE a_templateFile)
		{
			const auto original = WaitForOriginal(s_createFileA);
			if (!s_enabled.load(std::memory_order_acquire))
				return original(a_fileName, a_desiredAccess, a_shareMode, a_securityAttributes,
					a_creationDisposition, a_flagsAndAttributes, a_templateFile);

			const auto file = original(a_fileName, a_desiredAccess, a_shareMode, a_securityAttributes,
				a_creationDisposition, a_flagsAndAttributes, a_templateFile);
			if (file != INVALID_HANDLE_VALUE && HasCosaveExtension(a_fileName) &&
				IsReadOnlyOpen(a_desiredAccess, a_creationDisposition))
				TrackReadFile(file);

			return file;
		}

		static HANDLE WINAPI HookedCreateFileW(LPCWSTR a_fileName, DWORD a_desiredAccess, DWORD a_shareMode,
			LPSECURITY_ATTRIBUTES a_securityAttributes, DWORD a_creationDisposition, DWORD a_flagsAndAttributes,
			HANDLE a_templateFile)
		{
			const auto original = WaitForOriginal(s_createFileW);
			if (!s_enabled.load(std::memory_order_acquire))
				return original(a_fileName, a_desiredAccess, a_shareMode, a_securityAttributes,
					a_creationDisposition, a_flagsAndAttributes, a_templateFile);

			const auto file = original(a_fileName, a_desiredAccess, a_shareMode, a_securityAttributes,
				a_creationDisposition, a_flagsAndAttributes, a_templateFile);
			if (file != INVALID_HANDLE_VALUE && HasCosaveExtension(a_fileName) &&
				IsReadOnlyOpen(a_desiredAccess, a_creationDisposition))
				TrackReadFile(file);

			return file;
		}

		static BOOL WINAPI HookedReadFile(HANDLE a_file, LPVOID a_buffer, DWORD a_bytesToRead,
			LPDWORD a_bytesRead, LPOVERLAPPED a_overlapped)
		{
			const auto original = WaitForOriginal(s_readFile);
			if (!s_enabled.load(std::memory_order_acquire) || a_overlapped)
				return original(a_file, a_buffer, a_bytesToRead, a_bytesRead, a_overlapped);

			std::unique_lock lock{ s_filesLock };
			const auto it = s_files.find(a_file);
			if (it == s_files.end())
			{
				lock.unlock();
				return original(a_file, a_buffer, a_bytesToRead, a_bytesRead, a_overlapped);
			}

			auto& bufferedFile = it->second;
			const auto remaining = bufferedFile.cursor < bufferedFile.size ?
				bufferedFile.size - bufferedFile.cursor : 0;
			const auto length = static_cast<DWORD>(std::min<std::uint64_t>(a_bytesToRead, remaining));
			const void* source = length != 0 ?
				bufferedFile.buffer.get() + static_cast<std::size_t>(bufferedFile.cursor) : nullptr;
			if (!CopyReadData(a_buffer, source, length, a_bytesRead))
				return FALSE;

			bufferedFile.cursor += length;
			return TRUE;
		}

		static BOOL WINAPI HookedSetFilePointerEx(HANDLE a_file, LARGE_INTEGER a_distance,
			PLARGE_INTEGER a_newFilePointer, DWORD a_moveMethod)
		{
			const auto original = WaitForOriginal(s_setFilePointerEx);
			if (!s_enabled.load(std::memory_order_acquire))
				return original(a_file, a_distance, a_newFilePointer, a_moveMethod);

			std::unique_lock lock{ s_filesLock };
			const auto it = s_files.find(a_file);
			if (it == s_files.end())
			{
				lock.unlock();
				return original(a_file, a_distance, a_newFilePointer, a_moveMethod);
			}

			auto& bufferedFile = it->second;
			std::uint64_t position = 0;
			OffsetResult result = OffsetResult::kSuccess;
			switch (a_moveMethod)
			{
			case FILE_BEGIN:
				if (a_distance.QuadPart < 0)
					result = OffsetResult::kNegative;
				else
					position = static_cast<std::uint64_t>(a_distance.QuadPart);
				break;
			case FILE_CURRENT:
				result = AddOffset(bufferedFile.cursor, a_distance.QuadPart, position);
				break;
			case FILE_END:
				result = AddOffset(bufferedFile.size, a_distance.QuadPart, position);
				break;
			default:
				::SetLastError(ERROR_INVALID_PARAMETER);
				return FALSE;
			}

			// win32 file positions are signed, anything past INT64_MAX fails as a negative seek
			if (result == OffsetResult::kSuccess &&
				position > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
				result = OffsetResult::kNegative;

			if (result == OffsetResult::kNegative)
			{
				::SetLastError(ERROR_NEGATIVE_SEEK);
				return FALSE;
			}
			if (result == OffsetResult::kOverflow)
			{
				::SetLastError(ERROR_ARITHMETIC_OVERFLOW);
				return FALSE;
			}
			if (!WriteNewFilePointer(a_newFilePointer, position))
				return FALSE;

			bufferedFile.cursor = position;
			return TRUE;
		}

		static BOOL WINAPI HookedCloseHandle(HANDLE a_object)
		{
			const auto original = WaitForOriginal(s_closeHandle);
			if (!s_enabled.load(std::memory_order_acquire))
				return original(a_object);

			std::unique_ptr<std::byte[]> buffer;
			{
				std::lock_guard lock{ s_filesLock };
				const auto it = s_files.find(a_object);
				if (it != s_files.end())
				{
					buffer = std::move(it->second.buffer);
					s_files.erase(it);
				}
			}
			buffer.reset();

			return original(a_object);
		}
	}

	ModuleCosaveIO::ModuleCosaveIO() :
		Module("Cosave IO", &bCosaveIO)
	{}

	bool ModuleCosaveIO::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace cosaveIODetail;

		if (s_installAttempted.exchange(true, std::memory_order_acq_rel))
			return s_enabled.load(std::memory_order_acquire);

		const auto serialization = F4SE::GetSerializationInterface();
		if (!serialization)
		{
			REX::WARN("Cosave IO: F4SE serialization interface is unavailable; patch not applied."sv);
			return false;
		}

		HMODULE f4seModule = nullptr;
		if (!::GetModuleHandleExA(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(serialization), &f4seModule) || !f4seModule)
		{
			REX::WARN("Cosave IO: failed to resolve the F4SE module; patch not applied."sv);
			return false;
		}

		const auto targetModule = reinterpret_cast<std::uintptr_t>(f4seModule);
		const auto createFileA = reinterpret_cast<CreateFileA_t>(RELEX::DetourIAT(
			targetModule, "kernel32.dll", "CreateFileA", reinterpret_cast<std::uintptr_t>(&HookedCreateFileA)));
		s_createFileA.store(createFileA, std::memory_order_release);
		const auto readFile = reinterpret_cast<ReadFile_t>(RELEX::DetourIAT(
			targetModule, "kernel32.dll", "ReadFile", reinterpret_cast<std::uintptr_t>(&HookedReadFile)));
		s_readFile.store(readFile, std::memory_order_release);
		const auto setFilePointerEx = reinterpret_cast<SetFilePointerEx_t>(RELEX::DetourIAT(
			targetModule, "kernel32.dll", "SetFilePointerEx", reinterpret_cast<std::uintptr_t>(&HookedSetFilePointerEx)));
		s_setFilePointerEx.store(setFilePointerEx, std::memory_order_release);
		const auto closeHandle = reinterpret_cast<CloseHandle_t>(RELEX::DetourIAT(
			targetModule, "kernel32.dll", "CloseHandle", reinterpret_cast<std::uintptr_t>(&HookedCloseHandle)));
		s_closeHandle.store(closeHandle, std::memory_order_release);
		const auto createFileW = reinterpret_cast<CreateFileW_t>(RELEX::DetourIAT(
			targetModule, "kernel32.dll", "CreateFileW", reinterpret_cast<std::uintptr_t>(&HookedCreateFileW)));
		s_createFileW.store(createFileW, std::memory_order_release);

		const char* missingImport = !createFileA ? "CreateFileA" :
			!readFile ? "ReadFile" :
			!setFilePointerEx ? "SetFilePointerEx" :
			!closeHandle ? "CloseHandle" : nullptr;
		if (missingImport)
		{
			REX::WARN("Cosave IO: failed to hook required kernel32!{} in F4SE; installed hooks remain disabled."sv,
				missingImport);
			return false;
		}

		s_enabled.store(true, std::memory_order_release);
		REX::INFO("Cosave IO: F4SE co-save read buffering installed."sv);
		return true;
	}

}
