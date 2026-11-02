#include <Modules/AdModuleBSMTAManager.h>
#include <Core/AdUtils.h>

#include <RE/B/BSMTAManager.h>
#include <RE/B/BSShaderAccumulator.h>
#include <RE/T/TLS.h>

namespace Addictol
{
	namespace bsMTAManagerDetail
	{
		template <std::size_t N, class T>
		auto SplitInterval(std::span<T> a_interval)
			requires(N > 0)
		{
			std::array<
				std::span<T>,
				N>
				result{};
			const auto size = a_interval.size() / N;
			const auto getIter = [&](std::size_t idx) {
				return a_interval.begin() + idx * size;
				};

			for (std::size_t i = 0; i < N - 1; ++i) {
				result[i] = std::span{ getIter(i), getIter(i + 1) };
			}
			result[N - 1] = std::span{ getIter(N - 1), a_interval.end() };
			return result;
		}

		class WorkManager
		{
		public:
			WorkManager(const WorkManager&) = delete;
			WorkManager(WorkManager&&) = delete;
			WorkManager& operator=(const WorkManager&) = delete;
			WorkManager& operator=(WorkManager&&) = delete;

			[[nodiscard]] static WorkManager& get() noexcept
			{
				static WorkManager singleton;
				return singleton;
			}

			[[nodiscard]] auto get_work()
				-> std::span<RE::BSMTAManager::RegisterObjectTask>
			{
				if (std::this_thread::get_id() == _tid) {
					return _tasks.back();
				}
				else {
					assert(_idx < _tasks.size() - 1);
					return _tasks[_idx++];
				}
			}

			void reset()
			{
				auto& tasks = RE::BSMTAManager::GetRegisterObjectTasks();
				_tid = std::this_thread::get_id();
				_tasks = SplitInterval<THREAD_COUNT>(
					std::span{ tasks.begin(), tasks.end() });
				_idx = 0;
			}

		private:
			WorkManager() noexcept = default;
			~WorkManager() noexcept = default;

			static constexpr auto THREAD_COUNT = 4;

			std::thread::id _tid{};
			std::array<std::span<RE::BSMTAManager::RegisterObjectTask>, THREAD_COUNT> _tasks{};
			std::atomic_size_t _idx{ 0 };
		};

		// Vanilla's RegisterObjectTask::Execute, inlined on NG/AE.
		inline void ExecuteTask(RE::TLS& a_tls, const RE::BSMTAManager::RegisterObjectTask& a_task)
		{
			const RELEX::ScopeValue zPrePass{ a_tls.alphaGroupZPrePass, a_task.alphaGroupZPrePass };
			const RELEX::ScopeValue grouping{ a_tls.groupingAlphas, a_task.groupingAlphas };
			const RELEX::ScopeValue alphaGroup{ a_tls.currentAlphaGroup, a_task.currentAlphaGroup };
			RE::BSMTAManager::GetShaderAccumulator()->RegisterObject(a_task.geometry);
		}

		inline void RegisterObjects(RE::BSMTAManager::JobData& a_jobData)
		{
			// Vanilla allocates the registered passes from memory context 30.
			constexpr std::int32_t kRegisterObjectsMemoryContext{ 30 };
			auto& tls = *RE::TLS::GetSingleton();
			const RELEX::ScopeValue memoryContext{ tls.memoryContext, kRegisterObjectsMemoryContext };
			for (auto& task : WorkManager::get().get_work()) {
				task.owner = std::addressof(a_jobData);
				task.registerBatchRendererPassIndex = a_jobData.registerBatchRendererPasses.size();
				task.registerGeometryGroupPassIndex = a_jobData.registerGeometryGroupPasses.size();
				ExecuteTask(tls, task);
				task.registerBatchRendererPassCount = a_jobData.registerBatchRendererPasses.size() - task.registerBatchRendererPassIndex;
				task.registerGeometryGroupPassCount = a_jobData.registerGeometryGroupPasses.size() - task.registerGeometryGroupPassIndex;
			}
		}

		struct Submit
		{
			static void thunk(void* a_self, void* a_waitForJobList, bool a_singleThreaded)
			{
				WorkManager::get().reset();
				return func(a_self, a_waitForJobList, a_singleThreaded);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	ModuleBSMTAManager::ModuleBSMTAManager() :
		Module("BSMTAManager", &bPatchesBSMTAManager)
	{}

	bool ModuleBSMTAManager::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		auto& trampoline = REL::GetTrampoline();

		const auto target = RE::ID::BSMTAManager::RegisterObjects;
		REL::WriteSafeFill(target.address(), REL::INT3, REL::Offset{ 0xC5, 0x14A }.offset());
		RELEX::DetourJump(target.address(), reinterpret_cast<std::uintptr_t>(bsMTAManagerDetail::RegisterObjects));

		bsMTAManagerDetail::Submit::func = trampoline.write_call<5>(REL::Relocation(RE::ID::BSMTAManager::EndAccumulation, REL::Offset{ 0x8E, 0x9F }).address(), bsMTAManagerDetail::Submit::thunk);

		return true;
	}
}