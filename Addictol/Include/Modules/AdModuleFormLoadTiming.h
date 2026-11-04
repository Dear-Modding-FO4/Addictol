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
		[[nodiscard]] virtual bool DoListener(
			F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;

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

		void RecordSpan(
			std::string_view a_series,
			std::string_view a_bucket,
			uint64_t a_from,
			uint64_t a_to) noexcept;
		void RecordStartupEndpoints() noexcept;
		void RecordCanLoadEndpoints() noexcept;
		void InstallInitStepTimers() noexcept;

		template <class, size_t>
		friend uint64_t LoadTiming::TimedForward(uint64_t, uint64_t, uint64_t, uint64_t);
		static void BeginTimedCall(size_t a_index, uint64_t a_start) noexcept;
		static void EndTimedCall(size_t a_index, uint64_t a_start, uint64_t a_end) noexcept;
		inline static std::array<LoadTiming::ForwardedCall, LoadTiming::kInitStepSites.size()> s_timedOriginals{};

		uint64_t m_loadQpc{ 0 };
		// Written and read on the game-data thread.
		uint64_t m_menuQpc{ 0 };
		bool m_startupRecorded{ false };
		bool m_canLoadRecorded{ false };

		// both callers stay on the activation thread
		bool m_installAttempted{ false };
	};
}
