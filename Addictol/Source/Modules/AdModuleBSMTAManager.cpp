#include <Modules/AdModuleBSMTAManager.h>
#include <Core/AdUtils.h>

#include <RE/B/BSMTAManager.h>
#include <RE/B/BSShaderAccumulator.h>

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
				_tid = std::this_thread::get_id();
				_tasks = SplitInterval<THREAD_COUNT>(
					std::span{ _proxyTasks.begin(), _proxyTasks.end() });
				_idx = 0;
			}

		private:
			using RegisterObjectTasks_t = RE::BSTArray<RE::BSMTAManager::RegisterObjectTask>;

			WorkManager() noexcept = default;
			~WorkManager() noexcept = default;

			static constexpr auto THREAD_COUNT = 4;

			std::thread::id _tid{};
			std::array<std::span<RE::BSMTAManager::RegisterObjectTask>, THREAD_COUNT> _tasks{};
			std::atomic_size_t _idx{ 0 };
			RegisterObjectTasks_t& _proxyTasks{ *reinterpret_cast<RegisterObjectTasks_t*>(REL::ID{ 377203, 2712969 }.address()) };
		};

		class Cache
		{
		public:
			Cache(const Cache&) = delete;
			Cache(Cache&&) = delete;
			Cache& operator=(const Cache&) = delete;
			Cache& operator=(Cache&&) = delete;

			[[nodiscard]] static Cache& GetSingleton() noexcept
			{
				static Cache singleton;
				return singleton;
			}

			void ExecuteTask(RE::BSMTAManager::RegisterObjectTask& a_self) const
			{
				if (RELEX::IsRuntimeOG())
				{
					return _executeTask(a_self);
				}
				else
				{
					RE::BSShaderAccumulator* shaderAccumulator = *reinterpret_cast<RE::BSShaderAccumulator**>(shaderAccumulatorAddress);
					void** vtable = *reinterpret_cast<void***>(shaderAccumulator);

					using Func = void (*)(RE::BSShaderAccumulator*, RE::BSGeometry*);
					auto func = reinterpret_cast<Func>(vtable[45]); // 0x168

					return func(shaderAccumulator, a_self.geometry);
				}
			}

		private:
			using ExecuteTask_t = void(RE::BSMTAManager::RegisterObjectTask&);

			Cache() noexcept = default;
			~Cache() noexcept = default;

			// OG-only id, resolving it on NG/AE hard-fails even though the call site is gated
			ExecuteTask_t* const _executeTask{ RELEX::IsRuntimeOG() ?
				reinterpret_cast<ExecuteTask_t*>(REL::ID{ 92771 }.address()) : nullptr };
			uintptr_t shaderAccumulatorAddress = REL::ID{ 1301235, 2712963 }.address();
		};

		inline void RegisterObjects(RE::BSMTAManager::JobData& a_jobData)
		{
			auto& cache = Cache::GetSingleton();
			for (auto& task : WorkManager::get().get_work()) {
				task.owner = std::addressof(a_jobData);
				task.registerBatchRendererPassIndex = a_jobData.registerBatchRendererPasses.size();
				task.registerGeometryGroupPassIndex = a_jobData.registerGeometryGroupPasses.size();
				cache.ExecuteTask(task);
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

		const auto target = REL::ID{ 883019, 2318478 };
		REL::WriteSafeFill(target.address(), REL::INT3, REL::Offset{ 0xC5, 0x14A }.offset());
		RELEX::DetourJump(target.address(), reinterpret_cast<std::uintptr_t>(bsMTAManagerDetail::RegisterObjects));

		bsMTAManagerDetail::Submit::func = trampoline.write_call<5>(REL::Relocation(REL::ID{ 485563, 2318474 }, REL::Offset{ 0x8E, 0x9F }).address(), bsMTAManagerDetail::Submit::thunk);

		return true;
	}

}