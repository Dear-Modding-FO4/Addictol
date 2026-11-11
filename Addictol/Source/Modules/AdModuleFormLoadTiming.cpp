#include <Modules/AdModuleFormLoadTiming.h>

#include <Core/AdClock.h>
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

}

namespace Addictol
{
	static_assert(offsetof(RE::TESFile, filename) == LoadTiming::kTESFileNameOffset);

	ModuleFormLoadTiming::ModuleFormLoadTiming() :
		Module(
			"Form Load Timing",
			&bTelemetryFormLoadTiming,
			{ F4SE::MessagingInterface::kGameDataReady, F4SE::MessagingInterface::kGameLoaded }),
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

		const auto runtime = LoadTiming::CurrentRuntime();
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
					LoadTiming::RuntimeName(runtime),
					compileAddress);
			}
			if (!constructValid)
			{
				REX::ERROR(
					"Form Load Timing: {} ConstructObjectList signature mismatch at {:016X}"sv,
					LoadTiming::RuntimeName(runtime),
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

		InstallInitStepTimers();
		m_loadQpc = Addictol::ReadQpc();
		Activate(DebugThreadId());
		return true;
	}

	void ModuleFormLoadTiming::BeginTimedCall(size_t a_index, uint64_t a_start) noexcept
	{
		if (!s_instance)
			return;
		switch (LoadTiming::kInitStepSites[a_index].function)
		{
		case LoadTiming::InitStepFunction::kWinMain:
			s_instance->RecordSpan(LoadTiming::kStartupSeriesName, LoadTiming::kLoadToStartUpBucket, s_instance->m_loadQpc, a_start);
			break;
		case LoadTiming::InitStepFunction::kThreadProc:
			s_instance->m_menuQpc = a_start;
			break;
		default:
			break;
		}
	}

	void ModuleFormLoadTiming::EndTimedCall(size_t a_index, uint64_t a_start, uint64_t a_end) noexcept
	{
		const auto& site = LoadTiming::kInitStepSites[a_index];
		if (!s_instance || site.function == LoadTiming::InitStepFunction::kWinMain)
			return;
		const auto series = site.function == LoadTiming::InitStepFunction::kStartUp ?
			LoadTiming::kPreMenuStepSeriesName :
			LoadTiming::kInitStepSeriesName;
		s_instance->RecordSpan(series, site.name, a_start, a_end);
	}

	void ModuleFormLoadTiming::RecordSpan(
		std::string_view a_series,
		std::string_view a_bucket,
		uint64_t a_from,
		uint64_t a_to) noexcept
	{
		if (a_from && a_to >= a_from)
			(void)Record(a_series, a_bucket, a_to - a_from, DebugThreadId());
	}

	void ModuleFormLoadTiming::InstallInitStepTimers() noexcept
	{
		const auto siteAddress = [](const LoadTiming::InitStepSite& a_site) {
			constexpr std::array kFunctions{
				RE::ID::WinMain,
				RE::ID::Main::StartUp,
				RE::ID::Main::InitGameData,
				RE::ID::Main::InitGameDataThreadProc
			};
			return REL::Relocation{ kFunctions[std::to_underlying(a_site.function)], a_site.offset }.address();
		};
		for (const auto& site : LoadTiming::kInitStepSites)
		{
			const auto address = siteAddress(site);
			if (!(site.size == 5 ? RELEX::Validate(address, { 0xE8 }) : RELEX::Validate(address, { 0xFF, 0x15 })))
			{
				REX::WARN(
					"Form Load Timing: {} call site has unexpected bytes; startup step timers skipped"sv,
					site.name);
				return;
			}
		}

		auto& trampoline = REL::GetTrampoline();
		[&]<size_t... I>(std::index_sequence<I...>) {
			((s_timedOriginals[I] = reinterpret_cast<LoadTiming::ForwardedCall>(
				  LoadTiming::kInitStepSites[I].size == 5 ?
					  trampoline.write_call<5>(siteAddress(LoadTiming::kInitStepSites[I]), &LoadTiming::TimedForward<ModuleFormLoadTiming, I>) :
					  // CALL6 returns the old pointer slot, not the function in it.
					  *reinterpret_cast<const uintptr_t*>(
						  trampoline.write_call<6>(siteAddress(LoadTiming::kInitStepSites[I]), &LoadTiming::TimedForward<ModuleFormLoadTiming, I>)))),
				...);
		}(std::make_index_sequence<LoadTiming::kInitStepSites.size()>{});
	}

	bool ModuleFormLoadTiming::DoListener(
		F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// Listeners run even when installation was skipped.
		if (!a_msg || !m_loadQpc)
			return true;
		// Sent from the game-data thread, like the CompileFiles records.
		if (a_msg->type == F4SE::MessagingInterface::kGameDataReady && a_msg->data)
			RecordStartupEndpoints();
		// Sent once the game-data thread returns, after its completion event.
		else if (a_msg->type == F4SE::MessagingInterface::kGameLoaded)
			RecordCanLoadEndpoints();
		return true;
	}

	void ModuleFormLoadTiming::RecordCanLoadEndpoints() noexcept
	{
		if (m_canLoadRecorded || !m_loadQpc)
			return;
		m_canLoadRecorded = true;
		const auto canLoad = Addictol::ReadQpc();
		RecordSpan(LoadTiming::kStartupSeriesName, LoadTiming::kLoadToCanLoadBucket, m_loadQpc, canLoad);
		RecordSpan(LoadTiming::kStartupSeriesName, LoadTiming::kMenuToCanLoadBucket, m_menuQpc, canLoad);
	}

	void ModuleFormLoadTiming::RecordStartupEndpoints() noexcept
	{
		if (m_startupRecorded || !m_loadQpc)
			return;
		m_startupRecorded = true;
		const auto ready = Addictol::ReadQpc();
		RecordSpan(LoadTiming::kStartupSeriesName, LoadTiming::kLoadToDataReadyBucket, m_loadQpc, ready);
		if (m_menuQpc)
		{
			RecordSpan(LoadTiming::kStartupSeriesName, LoadTiming::kLoadToMenuBucket, m_loadQpc, m_menuQpc);
			RecordSpan(LoadTiming::kStartupSeriesName, LoadTiming::kMenuToDataReadyBucket, m_menuQpc, ready);
		}
	}

	bool __fastcall ModuleFormLoadTiming::HookCompileFiles(
		void* a_this,
		bool a_load)
	{
		const auto original = s_originalCompileFiles;
		if (!original)
			return false;
		const auto start = Addictol::ReadQpc();
		const auto result = original(a_this, a_load);
		if (s_instance)
			s_instance->RecordSpan(LoadTiming::kFormSeriesNames[0], LoadTiming::kCompileBucket, start, Addictol::ReadQpc());
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
		const auto start = Addictol::ReadQpc();
		const auto result = original(a_this, a_file, a_isFirst);
		if (s_instance)
			s_instance->RecordSpan(LoadTiming::kFormSeriesNames[1], bucket, start, Addictol::ReadQpc());
		return result;
	}
}
