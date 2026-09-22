#include "Harness.h"

#include <Core/AdDeferredOwnerRegistry.h>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace
{
	struct LifetimeProbe
	{
		LifetimeProbe(
			std::atomic_uint32_t& a_destroyed,
			std::thread::id& a_destroyThread) :
			destroyed(a_destroyed),
			destroyThread(a_destroyThread)
		{}

		~LifetimeProbe()
		{
			destroyThread = std::this_thread::get_id();
			++destroyed;
		}

		std::atomic_uint32_t& destroyed;
		std::thread::id& destroyThread;
	};
}

namespace vmm_tests
{
	void run_shader_reference_effect_lifetime_checks(Runner& runner)
	{
		using Registry =
			Addictol::DeferredOwnerRegistry<uintptr_t, std::shared_ptr<LifetimeProbe>>;

		runner.test("shader owner retires after final destructor at task boundary", [] {
			Registry registry;
			std::vector<Registry::Task> tasks;
			std::atomic_uint32_t destroyed{};
			std::thread::id destroyThread;
			auto owner = std::make_shared<LifetimeProbe>(destroyed, destroyThread);
			std::weak_ptr<LifetimeProbe> weakOwner = owner;
			bool destructorFinished = false;
			bool scheduledBeforeDestructor = false;
			const auto schedule = [&](Registry::Task a_task) {
				scheduledBeforeDestructor = !destructorFinished;
				tasks.push_back(std::move(a_task));
			};

			registry.Retain(0x1000, owner, schedule);
			owner.reset();

			bool workerSawOwner = false;
			bool destructorSawOwner = false;
			int result = 0;
			std::thread worker{ [&]() {
				workerSawOwner = !weakOwner.expired();
				result = registry.RetireAfter(
					0x1000,
					[&]() {
						destructorSawOwner = !weakOwner.expired();
						destructorFinished = true;
						return 7;
					},
					schedule);
			} };
			worker.join();

			require(workerSawOwner, "owner did not survive worker use");
			require(destructorSawOwner, "owner was released before the shader destructor");
			require(!scheduledBeforeDestructor, "owner queued before shader destructor completed");
			require(result == 7, "shader destructor result was not preserved");
			require(destroyed.load() == 0, "owner released before the queued task");
			require(registry.RetainedCount() == 0, "retired shader remained registered");
			require(registry.QueuedCount() == 1, "owner was not queued exactly once");
			require(tasks.size() == 1, "retirement did not schedule exactly one task");

			const auto taskThread = std::this_thread::get_id();
			tasks.front()();
			require(destroyed.load() == 1, "queued owner was not released exactly once");
			require(destroyThread == taskThread, "owner released outside the task boundary");
			require(registry.QueuedCount() == 0, "completed owner remained queued");
		});

		runner.test("shader rollback defers release across address reuse", [] {
			Registry registry;
			std::vector<Registry::Task> tasks;
			std::atomic_uint32_t firstDestroyed{};
			std::atomic_uint32_t secondDestroyed{};
			std::thread::id firstDestroyThread;
			std::thread::id secondDestroyThread;
			auto first =
				std::make_shared<LifetimeProbe>(firstDestroyed, firstDestroyThread);
			auto second =
				std::make_shared<LifetimeProbe>(secondDestroyed, secondDestroyThread);
			std::weak_ptr<LifetimeProbe> weakFirst = first;
			std::weak_ptr<LifetimeProbe> weakSecond = second;
			const auto schedule = [&tasks](Registry::Task a_task) {
				tasks.push_back(std::move(a_task));
			};

			registry.Retain(0x3000, first, schedule);
			first.reset();
			registry.RetireAfter(0x3000, [&]() {
				registry.Retain(0x3000, second, schedule);
				second.reset();
			}, schedule);

			require(tasks.size() == 1, "reused address did not queue the old owner");
			require(!weakFirst.expired(), "rollback released the old owner inline");
			require(!weakSecond.expired(), "reused address lost the new owner pin");
			require(registry.RetainedCount() == 1, "reused address retained multiple owners");
			tasks.front()();
			require(weakFirst.expired(), "reused address leaked the old owner");
			require(!weakSecond.expired(), "old-owner task released the new owner");

			registry.RetireAfter(0x3000, [] {}, schedule);
			require(tasks.size() == 2, "new owner retirement was not scheduled");
			tasks.back()();
			require(weakSecond.expired(), "reused address leaked the new owner");
			require(firstDestroyed.load() == 1, "old owner release count was not one");
			require(secondDestroyed.load() == 1, "new owner release count was not one");
		});
	}
}
