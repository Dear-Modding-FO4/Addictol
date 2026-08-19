#include "Harness.h"

#include <vmmblock.h>
#include <vmmgeometry.h>

#include <array>
#include <barrier>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{
	inline constexpr std::array stress_sizes{
		size_t{ 1 },
		size_t{ 8 },
		size_t{ 9 },
		size_t{ 32 },
		size_t{ 65 },
		size_t{ 129 },
		size_t{ 257 },
		size_t{ 513 },
		size_t{ 1025 },
		size_t{ 4097 },
		size_t{ 8193 },
		size_t{ 16385 },
		size_t{ 32769 },
		size_t{ 65537 },
		size_t{ 131073 },
		size_t{ 200000 }
	};

	class FailureLog
	{
	public:
		void Add(std::string message)
		{
			std::scoped_lock lock{ m_mutex };
			++m_total;
			if (m_messages.size() < 32)
				m_messages.push_back(std::move(message));
		}

		[[nodiscard]] bool Empty() const
		{
			std::scoped_lock lock{ m_mutex };
			return m_total == 0;
		}

		[[nodiscard]] std::string Describe() const
		{
			std::scoped_lock lock{ m_mutex };
			std::ostringstream description;
			description << m_total << " concurrent invariant failure";
			if (m_total != 1)
				description << 's';
			for (const auto& message : m_messages)
				description << "\n- " << message;
			if (m_total > m_messages.size())
				description << "\n- " << m_total - m_messages.size() << " more failures omitted";
			return description.str();
		}

	private:
		mutable std::mutex m_mutex;
		std::vector<std::string> m_messages;
		size_t m_total{ 0 };
	};

	template <class... Values>
	void AddFailure(FailureLog& a_failures, Values&&... a_values)
	{
		std::ostringstream message;
		(message << ... << std::forward<Values>(a_values));
		a_failures.Add(message.str());
	}

	struct PatternMismatch
	{
		size_t offset;
		uint8_t expected;
		uint8_t actual;
	};

	[[nodiscard]] std::optional<PatternMismatch> FindPatternMismatch(
		const void* a_pointer,
		size_t a_size,
		uint64_t a_seed)
	{
		const auto* bytes = static_cast<const uint8_t*>(a_pointer);
		for (size_t offset = 0; offset < a_size; ++offset)
		{
			const auto expected = vmm_tests::pattern_byte(offset, a_size, a_seed);
			if (bytes[offset] != expected)
				return PatternMismatch{ offset, expected, bytes[offset] };
		}
		return std::nullopt;
	}

	struct AllocatedBlock
	{
		void* pointer;
		size_t size;
		uint64_t seed;
		size_t thread;
		size_t operation;
	};

	void CheckBlock(const AllocatedBlock& a_block, std::string_view a_case, FailureLog& a_failures)
	{
		const auto measured = voltek::scalable_msize(a_block.pointer);
		if (measured != a_block.size)
		{
			AddFailure(
				a_failures,
				a_case,
				": thread ",
				a_block.thread,
				", operation ",
				a_block.operation,
				", requested ",
				a_block.size,
				", measured ",
				measured);
		}

		if (const auto mismatch = FindPatternMismatch(a_block.pointer, a_block.size, a_block.seed))
		{
			AddFailure(
				a_failures,
				a_case,
				": thread ",
				a_block.thread,
				", operation ",
				a_block.operation,
				", size ",
				a_block.size,
				", offset ",
				mismatch->offset,
				", expected ",
				static_cast<unsigned>(mismatch->expected),
				", actual ",
				static_cast<unsigned>(mismatch->actual));
		}
	}

	void FreeBlock(const AllocatedBlock& a_block, std::string_view a_case, FailureLog& a_failures)
	{
		if (!voltek::scalable_free(a_block.pointer))
		{
			AddFailure(
				a_failures,
				a_case,
				": thread ",
				a_block.thread,
				", operation ",
				a_block.operation,
				", size ",
				a_block.size,
				", free returned false for ",
				a_block.pointer);
		}
	}

	struct QueuedBlock
	{
		void* pointer;
		size_t size;
		uint64_t seed;
		size_t operation;
	};
}

namespace vmm_tests
{
	void run_threading_checks(Runner& runner)
	{
		runner.test("concurrent size classes preserve unique blocks and contents", [] {
			constexpr std::array sizes{
				size_t{ 8 },
				size_t{ 16 },
				size_t{ 64 },
				size_t{ 256 },
				size_t{ 1024 },
				size_t{ 8192 },
				size_t{ 32768 },
				size_t{ 131072 }
			};
			constexpr size_t blocksPerThread = 32;
			FailureLog failures;
			std::array<std::vector<AllocatedBlock>, sizes.size()> blocks;
			std::barrier start{ static_cast<std::ptrdiff_t>(sizes.size()) };
			std::barrier allocated{ static_cast<std::ptrdiff_t>(sizes.size()) };
			std::barrier checked{ static_cast<std::ptrdiff_t>(sizes.size()) };
			std::vector<std::thread> threads;
			threads.reserve(sizes.size());

			for (size_t threadIndex = 0; threadIndex < sizes.size(); ++threadIndex)
			{
				blocks[threadIndex].reserve(blocksPerThread);
				threads.emplace_back([&, threadIndex] {
					start.arrive_and_wait();
					for (size_t operation = 0; operation < blocksPerThread; ++operation)
					{
						const auto size = sizes[threadIndex];
						const auto seed =
							0xD37E21A5ull ^ (threadIndex << 32) ^ operation ^ (size << 19);
						void* pointer = voltek::scalable_alloc(size);
						if (!pointer)
						{
							AddFailure(
								failures,
								"multi-class allocation: thread ",
								threadIndex,
								", operation ",
								operation,
								", size ",
								size,
								", allocation returned null");
							continue;
						}
						fill_pattern(pointer, size, seed);
						blocks[threadIndex].push_back({ pointer, size, seed, threadIndex, operation });
					}

					allocated.arrive_and_wait();
					for (const auto& block : blocks[threadIndex])
						CheckBlock(block, "multi-class round trip", failures);
					checked.arrive_and_wait();
					for (const auto& block : blocks[threadIndex])
						FreeBlock(block, "multi-class cleanup", failures);
				});
			}

			for (auto& thread : threads)
				thread.join();

			std::unordered_map<void*, const AllocatedBlock*> owners;
			for (const auto& threadBlocks : blocks)
			{
				for (const auto& block : threadBlocks)
				{
					const auto [entry, inserted] = owners.emplace(block.pointer, &block);
					if (!inserted)
					{
						AddFailure(
							failures,
							"multi-class uniqueness: pointer ",
							block.pointer,
							" belonged to thread ",
							entry->second->thread,
							" operation ",
							entry->second->operation,
							" and thread ",
							block.thread,
							" operation ",
							block.operation);
					}
				}
			}

			require(failures.Empty(), failures.Describe());
		});

		runner.test("same-size allocation and free races preserve live ownership", [] {
			constexpr size_t threadCount = 8;
			constexpr size_t iterations = 1000;
			constexpr size_t liveWindow = 8;
			constexpr size_t allocationSize = 32769;
			struct Owner
			{
				size_t thread;
				size_t operation;
			};

			FailureLog failures;
			std::mutex liveMutex;
			std::unordered_map<void*, Owner> live;
			std::barrier start{ static_cast<std::ptrdiff_t>(threadCount) };
			std::vector<std::thread> threads;
			threads.reserve(threadCount);

			for (size_t threadIndex = 0; threadIndex < threadCount; ++threadIndex)
			{
				threads.emplace_back([&, threadIndex] {
					std::array<std::optional<AllocatedBlock>, liveWindow> slots;
					start.arrive_and_wait();

					const auto release = [&](AllocatedBlock& a_block) {
						CheckBlock(a_block, "same-class churn", failures);
						{
							std::scoped_lock lock{ liveMutex };
							const auto found = live.find(a_block.pointer);
							if (found == live.end())
							{
								AddFailure(
									failures,
									"same-class ownership: thread ",
									threadIndex,
									", operation ",
									a_block.operation,
									", pointer ",
									a_block.pointer,
									" was absent from the live set");
							}
							else if (
								found->second.thread != threadIndex ||
								found->second.operation != a_block.operation)
							{
								AddFailure(
									failures,
									"same-class ownership: pointer ",
									a_block.pointer,
									" was owned by thread ",
									found->second.thread,
									" operation ",
									found->second.operation,
									", not thread ",
									threadIndex,
									" operation ",
									a_block.operation);
							}
							else
							{
								live.erase(found);
							}
						}
						FreeBlock(a_block, "same-class churn", failures);
					};

					for (size_t operation = 0; operation < iterations; ++operation)
					{
						auto& slot = slots[operation % slots.size()];
						if (slot)
						{
							release(*slot);
							slot.reset();
						}

						const auto seed = 0x51A6E5EEDull ^ (threadIndex << 32) ^ operation;
						void* pointer = voltek::scalable_alloc(allocationSize);
						if (!pointer)
						{
							AddFailure(
								failures,
								"same-class allocation: thread ",
								threadIndex,
								", operation ",
								operation,
								", size ",
								allocationSize,
								", allocation returned null");
							continue;
						}
						fill_pattern(pointer, allocationSize, seed);
						{
							std::scoped_lock lock{ liveMutex };
							const auto [entry, inserted] = live.emplace(pointer, Owner{ threadIndex, operation });
							if (!inserted)
							{
								AddFailure(
									failures,
									"same-class uniqueness: pointer ",
									pointer,
									" was already live for thread ",
									entry->second.thread,
									" operation ",
									entry->second.operation,
									" when returned to thread ",
									threadIndex,
									" operation ",
									operation);
							}
						}
						slot = AllocatedBlock{ pointer, allocationSize, seed, threadIndex, operation };
					}

					for (auto& slot : slots)
					{
						if (slot)
							release(*slot);
					}
				});
			}

			for (auto& thread : threads)
				thread.join();

			{
				std::scoped_lock lock{ liveMutex };
				if (!live.empty())
					AddFailure(failures, "same-class ownership: ", live.size(), " blocks remained in the live set");
			}
			require(failures.Empty(), failures.Describe());
		});

		runner.test("cross-thread free preserves ownership and contents", [] {
			constexpr size_t iterations = 1500;
			std::deque<QueuedBlock> queue;
			std::mutex queueMutex;
			std::condition_variable available;
			bool producerDone = false;
			FailureLog failures;

			std::thread consumer{ [&] {
				for (;;)
				{
					QueuedBlock queued{};
					{
						std::unique_lock lock{ queueMutex };
						available.wait(lock, [&] { return producerDone || !queue.empty(); });
						if (queue.empty())
						{
							if (producerDone)
								break;
							continue;
						}
						queued = queue.front();
						queue.pop_front();
					}

					const AllocatedBlock block{
						queued.pointer,
						queued.size,
						queued.seed,
						0,
						queued.operation
					};
					CheckBlock(block, "cross-thread handoff", failures);
					FreeBlock(block, "cross-thread handoff", failures);
				}
			} };

			std::thread producer{ [&] {
				for (size_t operation = 0; operation < iterations; ++operation)
				{
					const auto size = stress_sizes[operation % stress_sizes.size()];
					const auto seed = 0xC20557A11ull ^ operation ^ (size << 11);
					void* pointer = voltek::scalable_alloc(size);
					if (!pointer)
					{
						AddFailure(
							failures,
							"cross-thread allocation: operation ",
							operation,
							", size ",
							size,
							", allocation returned null");
						continue;
					}
					fill_pattern(pointer, size, seed);

					{
						std::scoped_lock lock{ queueMutex };
						queue.push_back({ pointer, size, seed, operation });
					}
					available.notify_one();
				}

				{
					std::scoped_lock lock{ queueMutex };
					producerDone = true;
				}
				available.notify_one();
			} };

			producer.join();
			consumer.join();
			require(failures.Empty(), failures.Describe());
		});

		runner.test("concurrent allocation grows a pool without losing blocks", [] {
			constexpr size_t threadCount = 8;
			constexpr size_t allocationSize = 8193;
			constexpr size_t poolIndex = 10;
			constexpr size_t blocksPerPage =
				voltek::memory_manager::blocks_per_page<voltek::memory_manager::block16384_t>;
			constexpr size_t blocksPerThread = blocksPerPage / threadCount + 1;

			FailureLog failures;
			std::array<std::vector<AllocatedBlock>, threadCount> blocks;
			std::barrier start{ static_cast<std::ptrdiff_t>(threadCount + 1) };
			std::barrier allocated{ static_cast<std::ptrdiff_t>(threadCount + 1) };
			std::barrier release{ static_cast<std::ptrdiff_t>(threadCount + 1) };
			std::vector<std::thread> threads;
			threads.reserve(threadCount);

			for (size_t threadIndex = 0; threadIndex < threadCount; ++threadIndex)
			{
				blocks[threadIndex].reserve(blocksPerThread);
				threads.emplace_back([&, threadIndex] {
					start.arrive_and_wait();
					for (size_t operation = 0; operation < blocksPerThread; ++operation)
					{
						const auto seed = 0x6A09E667F3BCC909ull ^ (threadIndex << 32) ^ operation;
						void* pointer = voltek::scalable_alloc(allocationSize);
						if (!pointer)
						{
							AddFailure(
								failures,
								"pool-growth allocation: thread ",
								threadIndex,
								", operation ",
								operation,
								", allocation returned null");
							continue;
						}
						fill_pattern(pointer, allocationSize, seed);
						blocks[threadIndex].push_back(
							{ pointer, allocationSize, seed, threadIndex, operation });
					}
					allocated.arrive_and_wait();
					release.arrive_and_wait();
					for (const auto& block : blocks[threadIndex])
						FreeBlock(block, "pool-growth cleanup", failures);
				});
			}

			start.arrive_and_wait();
			allocated.arrive_and_wait();

			std::unordered_map<void*, const AllocatedBlock*> owners;
			for (const auto& threadBlocks : blocks)
			{
				for (const auto& block : threadBlocks)
				{
					CheckBlock(block, "pool-growth round trip", failures);
					const auto [entry, inserted] = owners.emplace(block.pointer, &block);
					if (!inserted)
					{
						AddFailure(
							failures,
							"pool-growth uniqueness: pointer ",
							block.pointer,
							" belonged to thread ",
							entry->second->thread,
							" operation ",
							entry->second->operation,
							" and thread ",
							block.thread,
							" operation ",
							block.operation);
					}
				}
			}

			release.arrive_and_wait();
			for (auto& thread : threads)
				thread.join();
			require(failures.Empty(), failures.Describe());
		});
	}
}
