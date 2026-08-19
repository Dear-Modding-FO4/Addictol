#pragma once

#include <Core/AdModule.h>
#include <Telemetry/AdLoadTiming.h>

#include <Windows.h>

#ifdef ERROR
#	undef ERROR
#endif

namespace Addictol
{
	using PluginTimingSource = BurstSeriesMetricSource<
		LoadTiming::kPluginMetricSchema.size(),
		LoadTiming::kPluginBurstCapacity>;

	class ModulePluginTiming :
		public Module,
		public PluginTimingSource
	{
	public:
		ModulePluginTiming();
		virtual ~ModulePluginTiming() = default;

		[[nodiscard]] virtual bool DoInstall(
			[[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;

	private:
		using GetProcAddressFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);
		using QueryFn = bool(*)(const void*, void*);
		using LoadFn = bool(*)(const void*);

		struct PluginName
		{
			HMODULE module{ nullptr };
			char name[LoadTiming::kModuleFileNameCapacity]{};
			uint16_t length{ 0 };
		};

		[[nodiscard]] static FARPROC WINAPI HookGetProcAddress(
			HMODULE a_module,
			LPCSTR a_procName) noexcept;
		[[nodiscard]] static bool HookQuery(const void* a_f4se, void* a_info);
		[[nodiscard]] static bool HookLoad(const void* a_f4se);
		[[nodiscard]] std::string_view FindPluginName(HMODULE a_module) noexcept;
		void RecordPlugin(std::string_view a_series, uint64_t a_ticks) noexcept;

		inline static ModulePluginTiming* s_instance{ nullptr };
		inline static GetProcAddressFn s_originalGetProcAddress{ nullptr };
		inline static QueryFn s_originalQuery{ nullptr };
		inline static LoadFn s_originalLoad{ nullptr };
		inline static HMODULE s_ownModule{ nullptr };
		inline static const char* s_activeName{ nullptr };
		inline static size_t s_activeNameLength{ 0 };

		// f4se resolves and calls exports serially
		PluginName m_plugins[LoadTiming::kPluginNameCapacity]{};
		char m_path[LoadTiming::kModulePathCapacity]{};
		size_t m_pluginCount{ 0 };
		bool m_installAttempted{ false };
	};
}
