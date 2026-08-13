#include <Modules/AdModuleLibDeflate.h>
#include <AdProfilerBA2.h>
#include <AdUtils.h>
#include <AdZlibInflate.h>
#include <libdeflate/libdeflate.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesLibDeflate{ "Patches"sv, "bLibDeflate"sv, true };

	namespace zlibDetail
	{
		constexpr static int32_t Z_STREAM_ERROR		= -2;

		using TInflate = int32_t(*)(ZlibInflate::Stream*, int32_t) noexcept;
		TInflate OriginalInflate;

		struct AtomicCounters
		{
			std::atomic<std::uint64_t> attempted{ 0 };
			std::atomic<std::uint64_t> succeeded{ 0 };
			std::atomic<std::uint64_t> rejectedByState{ 0 };
			std::atomic<std::uint64_t> codecFailed{ 0 };
			std::atomic<std::uint64_t> commitRejected{ 0 };
			std::atomic<std::uint64_t> allocationFailed{ 0 };
		};

		AtomicCounters& GetAtomicCounters() noexcept
		{
			static auto* counters = new AtomicCounters();
			return *counters;
		}

		struct ThreadCounters
		{
			static constexpr std::uint32_t FLUSH_THRESHOLD = 256;

			std::uint64_t attempted{ 0 };
			std::uint64_t succeeded{ 0 };
			std::uint64_t rejectedByState{ 0 };
			std::uint64_t codecFailed{ 0 };
			std::uint64_t commitRejected{ 0 };
			std::uint64_t allocationFailed{ 0 };
			std::uint32_t pending{ 0 };

			~ThreadCounters() noexcept
			{
				Flush();
			}

			void Count(std::uint64_t& a_counter) noexcept
			{
				++a_counter;
				if (++pending == FLUSH_THRESHOLD)
					Flush();
			}

			void Flush() noexcept
			{
				if (!pending)
					return;

				auto& totals = GetAtomicCounters();
				if (attempted)
					totals.attempted.fetch_add(attempted, std::memory_order_relaxed);
				if (succeeded)
					totals.succeeded.fetch_add(succeeded, std::memory_order_relaxed);
				if (rejectedByState)
					totals.rejectedByState.fetch_add(rejectedByState, std::memory_order_relaxed);
				if (codecFailed)
					totals.codecFailed.fetch_add(codecFailed, std::memory_order_relaxed);
				if (commitRejected)
					totals.commitRejected.fetch_add(commitRejected, std::memory_order_relaxed);
				if (allocationFailed)
					totals.allocationFailed.fetch_add(allocationFailed, std::memory_order_relaxed);

				attempted = 0;
				succeeded = 0;
				rejectedByState = 0;
				codecFailed = 0;
				commitRejected = 0;
				allocationFailed = 0;
				pending = 0;
			}
		};

		thread_local ThreadCounters g_threadCounters;

		void LogCounters() noexcept
		{
			g_threadCounters.Flush();
			const auto& counters = GetAtomicCounters();
			REX::INFO(
				"LibDeflate counters: attempted {}, succeeded {}, rejected-by-state {}, codec-failed {}, commit-rejected {}, allocation-failed {}"sv,
				counters.attempted.load(std::memory_order_relaxed),
				counters.succeeded.load(std::memory_order_relaxed),
				counters.rejectedByState.load(std::memory_order_relaxed),
				counters.codecFailed.load(std::memory_order_relaxed),
				counters.commitRejected.load(std::memory_order_relaxed),
				counters.allocationFailed.load(std::memory_order_relaxed));
		}

		namespace Decompression
		{
			struct LibDeflate
			{
				static int32_t Inflate(ZlibInflate::Stream* a_stream, int32_t a_flush) noexcept
				{
					if (!a_stream)
						return Z_STREAM_ERROR;
					if (!OriginalInflate)
						return Z_STREAM_ERROR;

					if (!ZlibInflate::CanAttempt(a_stream, a_flush))
					{
						g_threadCounters.Count(g_threadCounters.rejectedByState);
						return OriginalInflate(a_stream, a_flush);
					}

					thread_local libdeflate_decompressor* decompressor = libdeflate_alloc_decompressor();
					if (!decompressor)
					{
						g_threadCounters.Count(g_threadCounters.allocationFailed);
						return OriginalInflate(a_stream, a_flush);
					}

					const bool profiling = ProfilerCore::GetSingleton()->IsActive() && ProfilerCore::IsBA2TimingEnabled();
					std::chrono::high_resolution_clock::time_point profStart;
					if (profiling)
						profStart = std::chrono::high_resolution_clock::now();

					g_threadCounters.Count(g_threadCounters.attempted);
					const auto* expectedState = a_stream->state;
					size_t inBytes = 0, outBytes = 0;
					libdeflate_result result = libdeflate_zlib_decompress_ex(decompressor, a_stream->next_in, a_stream->avail_in,
						a_stream->next_out, a_stream->avail_out, &inBytes, &outBytes);

					if (result == LIBDEFLATE_SUCCESS)
					{
						const auto committed = ZlibInflate::CommitCompletedStream(
							a_stream, expectedState, inBytes, outBytes);
						if (!committed)
						{
							g_threadCounters.Count(g_threadCounters.commitRejected);
							// libdeflate may touch output on failure; stock rewrites the produced region.
							return OriginalInflate(a_stream, a_flush);
						}

						g_threadCounters.Count(g_threadCounters.succeeded);
						if (profiling)
						{
							auto profEnd = std::chrono::high_resolution_clock::now();
							double elapsedMs = std::chrono::duration<double, std::milli>(profEnd - profStart).count();
							ProfilerBA2::GetSingleton()->RecordDecompression(inBytes, outBytes, elapsedMs);
						}

						return ZlibInflate::Z_STREAM_END;
					}

					g_threadCounters.Count(g_threadCounters.codecFailed);
					// Raw and gzip streams fail the zlib probe and fall back without reading private wrap.
					return OriginalInflate(a_stream, a_flush);
				}
			};
		}
	}

	ModuleLibDeflate::ModuleLibDeflate() :
		Module("LibDeflate", &bPatchesLibDeflate, {
			F4SE::MessagingInterface::kPostLoadGame,
			F4SE::MessagingInterface::kPostSaveGame })
	{}

	bool ModuleLibDeflate::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleLibDeflate::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg) return false;
		using namespace zlibDetail;
		const auto runtime = RELEX::IsRuntimeOG() ? "OG"sv :
			(RELEX::IsRuntimeAE() ? "AE"sv : "NG"sv);
		const auto target = REL::ID{ 224011, 2168026 }.address();
		const auto code = std::span{
			reinterpret_cast<const std::uint8_t*>(target),
			ZlibInflate::Contract::VALIDATION_SIZE
		};
		const auto validation = ZlibInflate::ValidateContract(code);
		if (!validation)
		{
			REX::ERROR(
				"LibDeflate: {} zlib contract rejected: prologue={}, mode-load={}, mode-bounds={}, done-store={}, reset-zero={}, reset-store={}; fast path disabled."sv,
				runtime,
				validation.prologue,
				validation.modeLoad,
				validation.modeBounds,
				validation.doneStore,
				validation.resetZero,
				validation.resetStore);
			return false;
		}

		OriginalInflate = reinterpret_cast<TInflate>(
			RELEX::DetourJump(target, reinterpret_cast<uintptr_t>(&Decompression::LibDeflate::Inflate)));
		if (!OriginalInflate)
		{
			REX::ERROR(
				"LibDeflate: {} detour failed after contract validation; inflate may be left in an indeterminate state."sv,
				runtime);
			return false;
		}

		REX::INFO(
			"LibDeflate: {} zlib contract validated; fast path enabled (mode +0x0, HEAD 0, DONE 28)."sv,
			runtime);
		return true;
	}

	bool ModuleLibDeflate::DoListener(F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg &&
			(a_msg->type == F4SE::MessagingInterface::kPostLoadGame ||
				a_msg->type == F4SE::MessagingInterface::kPostSaveGame))
			zlibDetail::LogCounters();

		return true;
	}

	bool ModuleLibDeflate::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}