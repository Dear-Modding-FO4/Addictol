#pragma once

#include <Core/AdModule.h>
#include <Telemetry/AdLoadTiming.h>

namespace Addictol
{
	using FormLoadTimingSource = BurstSeriesMetricSource<
		LoadTiming::kFormMetricSchema.size(),
		LoadTiming::kFormBurstCapacity>;

	class ModuleFormLoadTiming :
		public Module,
		public FormLoadTimingSource
	{
	public:
		using CompileFilesFn = bool(__fastcall*)(void*, bool);
		using ConstructObjectListFn = bool(__fastcall*)(void*, void*, bool);

		ModuleFormLoadTiming();
		virtual ~ModuleFormLoadTiming() = default;

		[[nodiscard]] virtual bool DoInstall(
			[[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;

	private:
		[[nodiscard]] static bool __fastcall HookCompileFiles(
			void* a_this,
			bool a_load);
		[[nodiscard]] static bool __fastcall HookConstructObjectList(
			void* a_this,
			void* a_file,
			bool a_isFirst);

		inline static ModuleFormLoadTiming* s_instance{ nullptr };
		inline static CompileFilesFn s_originalCompileFiles{ nullptr };
		inline static ConstructObjectListFn s_originalConstructObjectList{ nullptr };

		// both callers stay on the activation thread
		bool m_installAttempted{ false };
	};
}
