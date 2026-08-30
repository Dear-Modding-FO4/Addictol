#include <Modules/AdModuleFormLoadTiming.h>

#include <Core/AdUtils.h>
#include <Telemetry/AdTelemetryHub.h>

#include <RE/T/TESFile.h>
#include <detours/Detours.h>

#include <cstddef>

namespace
{
	struct FileName
	{
		const char* data;
		size_t length;
	};

	// file pointer may be invalid
	[[nodiscard]] FileName SafeTESFileName(void* a_file) noexcept
	{
		FileName result{};
		if (!a_file)
			return result;
		__try
		{
			const auto name = reinterpret_cast<const char*>(
				reinterpret_cast<uintptr_t>(a_file) +
					Addictol::LoadTiming::kTESFileNameOffset);
			size_t length{ 0 };
			while (length < Addictol::LoadTiming::kTESFileNameCapacity && name[length])
				++length;
			if (length && length < Addictol::LoadTiming::kTESFileNameCapacity)
				result = { name, length };
		}
		__except (1)
		{}
		return result;
	}

	[[nodiscard]] uint32_t DebugThreadId() noexcept
	{
#ifndef NDEBUG
		return GetCurrentThreadId();
#else
		return 0;
#endif
	}

	[[nodiscard]] Addictol::LoadTiming::Runtime CurrentRuntime() noexcept
	{
		return RELEX::IsRuntimeOG() ? Addictol::LoadTiming::Runtime::kOG :
			RELEX::IsRuntimeAE() ? Addictol::LoadTiming::Runtime::kAE :
			Addictol::LoadTiming::Runtime::kNG;
	}

	[[nodiscard]] std::string_view RuntimeName(
		Addictol::LoadTiming::Runtime a_runtime) noexcept
	{
		return a_runtime == Addictol::LoadTiming::Runtime::kOG ? "OG" :
			a_runtime == Addictol::LoadTiming::Runtime::kNG ? "NG" : "AE";
	}
}

namespace Addictol
{
	static_assert(offsetof(RE::TESFile, filename) == LoadTiming::kTESFileNameOffset);


	ModuleFormLoadTiming::ModuleFormLoadTiming() :
		Module("Form Load Timing", &bTelemetryFormLoadTiming),
		FormLoadTimingSource(LoadTiming::kFormMetricSchema)
	{
		s_instance = this;
	}

	bool ModuleFormLoadTiming::DoInstall(
		[[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (m_installAttempted)
		{
			Skip("installation already attempted"sv);
			return false;
		}
		m_installAttempted = true;

		const auto runtime = CurrentRuntime();
		const auto compileAddress = REL::ID{
			LoadTiming::kCompileFilesId.og,
			LoadTiming::kCompileFilesId.ng,
			LoadTiming::kCompileFilesId.ae
		}.address();
		const auto constructAddress = REL::ID{
			LoadTiming::kConstructObjectListId.og,
			LoadTiming::kConstructObjectListId.ng,
			LoadTiming::kConstructObjectListId.ae
		}.address();
		const auto compileValid = LoadTiming::ValidateSignature(
			compileAddress,
			LoadTiming::CompileFilesSignature(runtime));
		const auto constructValid = LoadTiming::ValidateSignature(
			constructAddress,
			LoadTiming::ConstructObjectListSignature(runtime));

		if (!compileValid || !constructValid)
		{
			if (!compileValid)
			{
				REX::ERROR(
					"Form Load Timing: {} CompileFiles signature mismatch at {:016X}"sv,
					RuntimeName(runtime),
					compileAddress);
			}
			if (!constructValid)
			{
				REX::ERROR(
					"Form Load Timing: {} ConstructObjectList signature mismatch at {:016X}"sv,
					RuntimeName(runtime),
					constructAddress);
			}
			Skip(
				!compileValid ?
					"CompileFiles signature mismatch"sv :
					"ConstructObjectList signature mismatch"sv);
			return false;
		}

		const auto constructOriginal = RELEX::DetourJump(
			constructAddress,
			reinterpret_cast<uintptr_t>(&HookConstructObjectList));
		if (!constructOriginal)
		{
			REX::ERROR(
				"Form Load Timing: ConstructObjectList detour installation failed"sv);
			Skip("ConstructObjectList detour installation failed"sv);
			return false;
		}
		s_originalConstructObjectList =
			reinterpret_cast<ConstructObjectListFn>(constructOriginal);

		const auto compileOriginal = RELEX::DetourJump(
			compileAddress,
			reinterpret_cast<uintptr_t>(&HookCompileFiles));
		if (!compileOriginal)
		{
			if (Detours::X64::DetourRemove(constructOriginal))
			{
				s_originalConstructObjectList = nullptr;
				REX::ERROR(
					"Form Load Timing: CompileFiles detour installation failed; ConstructObjectList hook removed"sv);
			}
			else
			{
				REX::CRITICAL(
					"Form Load Timing: CompileFiles detour installation failed and ConstructObjectList rollback failed; valid hook remains active"sv);
			}
			Skip("CompileFiles detour installation failed"sv);
			return false;
		}
		s_originalCompileFiles = reinterpret_cast<CompileFilesFn>(compileOriginal);

		Activate(DebugThreadId());
		return true;
	}

	bool __fastcall ModuleFormLoadTiming::HookCompileFiles(
		void* a_this,
		bool a_load)
	{
		const auto original = s_originalCompileFiles;
		if (!original)
			return false;
		const auto start = TelemetryDetail::ReadQpc();
		const auto result = original(a_this, a_load);
		const auto end = TelemetryDetail::ReadQpc();
		if (s_instance && end >= start)
		{
			(void)s_instance->Record(
				LoadTiming::kFormSeriesNames[0],
				LoadTiming::kCompileBucket,
				end - start,
				DebugThreadId());
		}
		return result;
	}

	bool __fastcall ModuleFormLoadTiming::HookConstructObjectList(
		void* a_this,
		void* a_file,
		bool a_isFirst)
	{
		const auto original = s_originalConstructObjectList;
		if (!original)
			return false;
		const auto fileName = SafeTESFileName(a_file);
		const std::string_view bucket{
			fileName.data ? fileName.data : "(unknown)",
			fileName.data ? fileName.length : sizeof("(unknown)") - 1
		};
		const auto start = TelemetryDetail::ReadQpc();
		const auto result = original(a_this, a_file, a_isFirst);
		const auto end = TelemetryDetail::ReadQpc();
		if (s_instance && end >= start)
		{
			(void)s_instance->Record(
				LoadTiming::kFormSeriesNames[1],
				bucket,
				end - start,
				DebugThreadId());
		}
		return result;
	}
}
