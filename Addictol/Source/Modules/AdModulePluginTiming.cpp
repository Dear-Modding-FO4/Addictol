#include <Modules/AdModulePluginTiming.h>

#include <Core/AdUtils.h>
#include <Telemetry/AdTelemetryHub.h>

#include <F4SE/Interfaces.h>

#include <cstring>

namespace
{
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
	static REX::TOML::Bool<> bTelemetryPluginTiming{
		"Telemetry"sv,
		"bPluginTiming"sv,
		false
	};

	ModulePluginTiming::ModulePluginTiming() :
		Module("Plugin Timing", &bTelemetryPluginTiming),
		PluginTimingSource(LoadTiming::kPluginMetricSchema)
	{
		s_instance = this;
	}

	bool ModulePluginTiming::DoInstall(
		[[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (m_installAttempted)
		{
			Skip("installation already attempted"sv);
			return false;
		}
		m_installAttempted = true;

		const auto trampoline = F4SE::GetTrampolineInterface();
		const auto proxy =
			reinterpret_cast<const F4SE::Impl::F4SETrampolineInterface*>(trampoline);
		if (!proxy || !proxy->AllocateFromBranchPool)
		{
			REX::ERROR("Plugin Timing: F4SE trampoline interface is unavailable"sv);
			Skip("F4SE trampoline interface is unavailable"sv);
			return false;
		}

		HMODULE f4seModule{ nullptr };
		if (!GetModuleHandleExA(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
				GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(proxy->AllocateFromBranchPool),
			&f4seModule))
		{
			REX::ERROR("Plugin Timing: F4SE module could not be resolved"sv);
			Skip("F4SE module could not be resolved"sv);
			return false;
		}

		if (!GetModuleHandleExA(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
				GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(&HookGetProcAddress),
			&s_ownModule))
		{
			REX::ERROR("Plugin Timing: Addictol module could not be resolved"sv);
			Skip("Addictol module could not be resolved"sv);
			return false;
		}

		const auto original = RELEX::DetourIAT(
			reinterpret_cast<uintptr_t>(f4seModule),
			"kernel32.dll",
			"GetProcAddress",
			reinterpret_cast<uintptr_t>(&HookGetProcAddress));
		if (!original)
		{
			REX::ERROR("Plugin Timing: F4SE GetProcAddress IAT hook failed"sv);
			Skip("F4SE GetProcAddress IAT hook failed"sv);
			return false;
		}

		s_originalGetProcAddress = reinterpret_cast<GetProcAddressFn>(original);
		Activate(DebugThreadId());
		if (RELEX::IsRuntimeOG())
		{
			REX::INFO(
				"Plugin Timing: OG query rows are unavailable and load rows begin after Addictol"sv);
		}
		return true;
	}

	FARPROC WINAPI ModulePluginTiming::HookGetProcAddress(
		HMODULE a_module,
		LPCSTR a_procName) noexcept
	{
		const auto originalGetProcAddress = s_originalGetProcAddress;
		if (!originalGetProcAddress)
			return GetProcAddress(a_module, a_procName);
		if (LoadTiming::IsOrdinalProcName(reinterpret_cast<uintptr_t>(a_procName)) ||
			a_module == s_ownModule)
			return originalGetProcAddress(a_module, a_procName);

		if (std::strcmp(a_procName, "F4SEPlugin_Query") == 0)
		{
			const auto original = originalGetProcAddress(a_module, a_procName);
			if (!original || !s_instance)
				return original;
			const auto name = s_instance->FindPluginName(a_module);
			s_activeName = name.data();
			s_activeNameLength = name.size();
			s_originalQuery = reinterpret_cast<QueryFn>(original);
			return reinterpret_cast<FARPROC>(&HookQuery);
		}

		if (std::strcmp(a_procName, "F4SEPlugin_Load") == 0)
		{
			const auto original = originalGetProcAddress(a_module, a_procName);
			if (!original || !s_instance)
				return original;
			const auto name = s_instance->FindPluginName(a_module);
			s_activeName = name.data();
			s_activeNameLength = name.size();
			s_originalLoad = reinterpret_cast<LoadFn>(original);
			return reinterpret_cast<FARPROC>(&HookLoad);
		}

		return originalGetProcAddress(a_module, a_procName);
	}

	bool ModulePluginTiming::HookQuery(const void* a_f4se, void* a_info)
	{
		const auto original = s_originalQuery;
		if (!original)
			return false;
		const auto start = TelemetryDetail::ReadQpc();
		const auto result = original(a_f4se, a_info);
		const auto end = TelemetryDetail::ReadQpc();
		if (s_instance && end >= start)
			s_instance->RecordPlugin(LoadTiming::kPluginSeriesNames[0], end - start);
		return result;
	}

	bool ModulePluginTiming::HookLoad(const void* a_f4se)
	{
		const auto original = s_originalLoad;
		if (!original)
			return false;
		const auto start = TelemetryDetail::ReadQpc();
		const auto result = original(a_f4se);
		const auto end = TelemetryDetail::ReadQpc();
		if (s_instance && end >= start)
			s_instance->RecordPlugin(LoadTiming::kPluginSeriesNames[1], end - start);
		return result;
	}

	std::string_view ModulePluginTiming::FindPluginName(HMODULE a_module) noexcept
	{
		for (size_t index = 0; index < m_pluginCount; ++index)
		{
			const auto& plugin = m_plugins[index];
			if (plugin.module == a_module)
				return { plugin.name, plugin.length };
		}

		if (m_pluginCount == std::size(m_plugins))
		{
			CountMetric(LoadTiming::kPluginNameFailureMetric);
			return "(unknown)"sv;
		}

		const auto length = GetModuleFileNameA(
			a_module,
			m_path,
			static_cast<DWORD>(std::size(m_path)));
		if (!length || length >= std::size(m_path))
		{
			CountMetric(LoadTiming::kPluginNameFailureMetric);
			return "(unknown)"sv;
		}
		const auto fileName = LoadTiming::FileNameFromPath(
			std::string_view{ m_path, length });
		if (fileName.empty() || fileName.size() >= LoadTiming::kModuleFileNameCapacity)
		{
			CountMetric(LoadTiming::kPluginNameFailureMetric);
			return "(unknown)"sv;
		}

		auto& plugin = m_plugins[m_pluginCount++];
		plugin.module = a_module;
		std::memcpy(plugin.name, fileName.data(), fileName.size());
		plugin.name[fileName.size()] = '\0';
		plugin.length = static_cast<uint16_t>(fileName.size());
		return { plugin.name, plugin.length };
	}

	void ModulePluginTiming::RecordPlugin(
		std::string_view a_series,
		uint64_t a_ticks) noexcept
	{
		const std::string_view name{
			s_activeName ? s_activeName : "(unknown)",
			s_activeName ? s_activeNameLength : sizeof("(unknown)") - 1
		};
		(void)PluginTimingSource::Record(a_series, name, a_ticks, DebugThreadId());
	}
}
