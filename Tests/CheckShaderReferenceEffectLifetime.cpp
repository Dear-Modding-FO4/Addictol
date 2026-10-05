#include "Harness.h"

#include <Core/AdDependentRegistry.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace vmm_tests
{
	void run_shader_reference_effect_lifetime_checks(Runner& runner)
	{
		using Registry = Addictol::DependentRegistry<uintptr_t, uintptr_t>;

		runner.test("owner release detaches only its live shaders", [] {
			Registry registry;
			registry.Link(0x10, 0x1000);
			registry.Link(0x10, 0x2000);
			registry.Link(0x20, 0x3000);
			registry.Unlink(0x2000);

			std::vector<uintptr_t> detached;
			registry.Release(0x10, [&](uintptr_t a_shader) { detached.push_back(a_shader); });

			require(detached == std::vector<uintptr_t>{ 0x1000 }, "release did not detach exactly the live shader");
			require(registry.LinkedCount() == 1, "release disturbed another owner's shader");

			detached.clear();
			registry.Release(0x10, [&](uintptr_t a_shader) { detached.push_back(a_shader); });
			require(detached.empty(), "released owner detached shaders twice");
		});

		runner.test("reused shader address follows its new owner", [] {
			Registry registry;
			registry.Link(0x10, 0x1000);
			registry.Link(0x20, 0x1000);

			bool detachedFromOld = false;
			registry.Release(0x10, [&](uintptr_t) { detachedFromOld = true; });
			require(!detachedFromOld, "stale owner detached a reused shader address");

			bool detachedFromNew = false;
			registry.Release(0x20, [&](uintptr_t) { detachedFromNew = true; });
			require(detachedFromNew, "new owner did not detach its shader");
			require(registry.LinkedCount() == 0, "released shaders remained linked");
		});

		runner.test("shader teardown waits for an in-flight owner detach", [] {
			Registry registry;
			registry.Link(0x10, 0x1000);

			std::atomic_bool unlinked{};
			bool unlinkedDuringDetach = true;
			std::thread teardown;
			registry.Release(0x10, [&](uintptr_t a_shader) {
				teardown = std::thread{ [&, a_shader]() {
					registry.Unlink(a_shader);
					unlinked = true;
				} };
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				unlinkedDuringDetach = unlinked.load();
			});
			teardown.join();

			require(!unlinkedDuringDetach, "shader unlinked while its owner was detaching it");
			require(unlinked.load(), "shader teardown never completed");
		});
	}
}
