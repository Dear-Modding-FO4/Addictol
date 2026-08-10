#include "Harness.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

namespace
{
	inline constexpr std::array stress_sizes{
		std::size_t{ 1 },
		std::size_t{ 8 },
		std::size_t{ 9 },
		std::size_t{ 32 },
		std::size_t{ 65 },
		std::size_t{ 129 },
		std::size_t{ 257 },
		std::size_t{ 513 },
		std::size_t{ 1025 },
		std::size_t{ 4097 },
		std::size_t{ 8193 },
		std::size_t{ 16385 },
		std::size_t{ 32769 },
		std::size_t{ 65537 },
		std::size_t{ 131073 },
		std::size_t{ 200000 }
	};

	struct QueuedBlock
	{
		void* pointer;
		std::size_t size;
		std::uint64_t seed;
	};
}

namespace vmm_tests
{
	void run_threading_checks(Runner& runner)
	{
		runner.test("deterministic multi-threaded stress", [] {
			constexpr unsigned thread_count = 8;
			constexpr std::size_t iterations = 1500;

			std::atomic failed{ false };
			std::mutex error_mutex;
			std::string error;
			auto set_failure = [&](std::string message) {
				failed.store(true, std::memory_order_relaxed);
				std::scoped_lock lock{ error_mutex };
				if (error.empty())
					error = std::move(message);
			};

			std::vector<std::thread> threads;
			threads.reserve(thread_count);
			for (unsigned thread_index = 0; thread_index < thread_count; ++thread_index)
			{
				threads.emplace_back([&, thread_index] {
					const auto thread_seed = 0xD37E21A5ull + thread_index * 0x9E3779B97F4A7C15ull;
					std::mt19937_64 random{ thread_seed };
					std::uniform_int_distribution<std::size_t> distribution{ 0, stress_sizes.size() - 1 };

					for (std::size_t iteration = 0; iteration < iterations && !failed.load(std::memory_order_relaxed); ++iteration)
					{
						const auto size = stress_sizes[distribution(random)];
						const auto seed = thread_seed ^ iteration ^ (size << 19);
						void* pointer = voltek::scalable_alloc(size);
						if (!pointer)
						{
							set_failure("allocation failed during multi-threaded stress");
							break;
						}

						fill_pattern(pointer, size, seed);
						if (!verify_pattern(pointer, size, seed))
							set_failure("memory corruption detected during multi-threaded stress");
						if (!voltek::scalable_free(pointer))
							set_failure("free failed during multi-threaded stress");
					}
				});
			}

			for (auto& thread : threads)
				thread.join();

			require(!failed.load(std::memory_order_relaxed), error.empty() ? "multi-threaded stress failed" : error);
		});

		runner.test("cross-thread free preserves ownership and contents", [] {
			constexpr std::size_t iterations = 1500;
			std::deque<QueuedBlock> queue;
			std::mutex queue_mutex;
			std::condition_variable available;
			bool producer_done = false;
			std::atomic failed{ false };
			std::mutex error_mutex;
			std::string error;

			auto set_failure = [&](std::string message) {
				failed.store(true, std::memory_order_relaxed);
				std::scoped_lock lock{ error_mutex };
				if (error.empty())
					error = std::move(message);
			};

			std::thread consumer{ [&] {
				for (;;)
				{
					QueuedBlock block{};
					{
						std::unique_lock lock{ queue_mutex };
						available.wait(lock, [&] { return producer_done || !queue.empty(); });
						if (queue.empty())
						{
							if (producer_done)
								break;
							continue;
						}
						block = queue.front();
						queue.pop_front();
					}

					if (!verify_pattern(block.pointer, block.size, block.seed))
						set_failure("memory corruption detected before cross-thread free");
					if (!voltek::scalable_free(block.pointer))
						set_failure("cross-thread free returned false");
				}
			} };

			std::thread producer{ [&] {
				for (std::size_t iteration = 0; iteration < iterations; ++iteration)
				{
					const auto size = stress_sizes[iteration % stress_sizes.size()];
					const auto seed = 0xC20557A11ull ^ iteration ^ (size << 11);
					void* pointer = voltek::scalable_alloc(size);
					if (!pointer)
					{
						set_failure("allocation failed during cross-thread free test");
						break;
					}
					fill_pattern(pointer, size, seed);

					{
						std::scoped_lock lock{ queue_mutex };
						queue.push_back({ pointer, size, seed });
					}
					available.notify_one();
				}

				{
					std::scoped_lock lock{ queue_mutex };
					producer_done = true;
				}
				available.notify_one();
			} };

			producer.join();
			consumer.join();

			require(!failed.load(std::memory_order_relaxed), error.empty() ? "cross-thread free test failed" : error);
		});
	}
}
