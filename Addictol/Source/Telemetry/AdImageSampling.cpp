#include <Telemetry/AdImageSampling.h>
#include <Core/AdClock.h>

#include <TlHelp32.h>
#include <winternl.h>
#include <REX/REX.h>

#include <algorithm>
#include <intrin.h>

namespace Addictol
{
	using namespace std::literals;

	namespace
	{
		constexpr std::array<ImageSeriesKind, 3> s_kinds{
			ImageSeriesKind{ "image.cpu", "main" }, { "image.cpu", "render" }, { "image.cpu", "other" }
		};
		constexpr size_t s_stackCopyBytes{ 32 * 1024 };

		struct ThreadInformation
		{
			LONG exitStatus;
			void* teb;
			CLIENT_ID client;
			ULONG_PTR affinity;
			LONG priority;
			LONG basePriority;
		};

		bool ValidStack(uintptr_t a_stack, const ImageSampling::Thread& a_thread) noexcept
		{
			return a_stack >= a_thread.stackLow && a_stack < a_thread.stackHigh &&
				a_thread.stackHigh - a_stack >= sizeof(uintptr_t) && !(a_stack % sizeof(uintptr_t));
		}
	}

	ImageSampling::ImageSampling() noexcept :
		ImageSeriesSource(bTelemetryImageSampling, s_kinds)
	{}

	bool ImageSampling::ReadThread(DWORD a_id, Thread& a_thread) noexcept
	{
		using QueryThread = LONG(NTAPI*)(HANDLE, ULONG, void*, ULONG, ULONG*);
		const auto query = reinterpret_cast<QueryThread>(
			GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
		if (!query)
			return false;
		const auto handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, a_id);
		if (!handle)
			return false;
		ThreadInformation information{};
		NT_TIB tib{};
		SIZE_T bytes{};
		uint64_t cycles{};
		const bool valid = query(handle, 0, &information, sizeof(information), nullptr) >= 0 &&
			ReadProcessMemory(GetCurrentProcess(), information.teb, &tib, sizeof(tib), &bytes) &&
			bytes == sizeof(tib) && QueryThreadCycleTime(handle, &cycles) &&
			reinterpret_cast<uintptr_t>(tib.StackLimit) < reinterpret_cast<uintptr_t>(tib.StackBase);
		if (!valid)
		{
			CloseHandle(handle);
			return false;
		}
		a_thread = { handle, a_id, reinterpret_cast<uintptr_t>(tib.StackLimit), reinterpret_cast<uintptr_t>(tib.StackBase), cycles };
		return true;
	}

	ImageId ImageSampling::Walk(CONTEXT& a_context, const Thread& a_thread, const ImageRegistry& a_registry,
		std::span<uintptr_t> a_copy, bool a_forceFault) noexcept
	{
		const auto leaf = a_registry.Find(a_context.Rip);
		const auto originalStack = a_context.Rsp;
		const auto copyBase = reinterpret_cast<uintptr_t>(a_copy.data());
		const Thread copy{ nullptr, 0, copyBase, copyBase + a_copy.size_bytes() };
		const auto rebase = [&](uint64_t a_value) {
			if (a_value < a_thread.stackLow || a_value > a_thread.stackHigh)
				return a_value;
			return a_value >= originalStack && a_value - originalStack <= a_copy.size_bytes() ?
				copyBase + a_value - originalStack : uint64_t{};
		};
		std::array registers{
			&a_context.Rax, &a_context.Rcx, &a_context.Rdx, &a_context.Rbx,
			&a_context.Rsp, &a_context.Rbp, &a_context.Rsi, &a_context.Rdi,
			&a_context.R8, &a_context.R9, &a_context.R10, &a_context.R11,
			&a_context.R12, &a_context.R13, &a_context.R14, &a_context.R15
		};
		for (auto reg : registers)
			*reg = rebase(*reg);
		// Saved frame registers must also address the copy, never the now-running target's stack.
		for (auto& word : a_copy)
			word = rebase(word);
		// Without a plugin frame, system code is charged to the first game frame that called it.
		const Image* game{};
		if (a_forceFault)
			a_context.Rip = *static_cast<volatile uintptr_t*>(nullptr);
		for (size_t depth = 0; depth < 16; ++depth)
		{
			if (!a_context.Rip || !ValidStack(a_context.Rsp, copy))
				break;
			const auto pc = a_context.Rip - (depth ? 1 : 0);
			const auto image = a_registry.Find(pc);
			if (image && image->kind != ImageKind::kSystem && image->kind != ImageKind::kGame)
				return image->id;
			if (image && image->kind == ImageKind::kGame && !game)
				game = image;
			const auto oldStack = a_context.Rsp;
			if (const auto function = a_registry.FunctionEntry(pc))
			{
				const auto unwind = reinterpret_cast<const uint8_t*>(image->base + function->UnwindData);
				const auto frame = unwind[3] & 15;
				if (frame && !ValidStack(*registers[frame] - (unwind[3] >> 4) * 16, copy))
					break;
				void* handler{};
				DWORD64 establisher{};
				RtlVirtualUnwind(UNW_FLAG_NHANDLER, image->base, pc, function, &a_context,
					&handler, &establisher, nullptr);
			}
			else
			{
				a_context.Rip = *reinterpret_cast<const uintptr_t*>(a_context.Rsp);
				a_context.Rsp += sizeof(uintptr_t);
			}
			if (a_context.Rsp <= oldStack || !ValidStack(a_context.Rsp, copy))
				break;
		}
		if (game)
			return game->id;
		return leaf ? leaf->id : kInvalidImage;
	}

	ImageSampling::Sample ImageSampling::Capture(const Thread& a_thread, const ImageRegistry& a_registry,
		bool a_forceFault) noexcept
	{
		Sample sample;
		CONTEXT context{};
		context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
		std::array<uintptr_t, s_stackCopyBytes / sizeof(uintptr_t)> stack{};
		SIZE_T copied{};
		bool captured{};
		DWORD resumed{};
		const auto process = GetCurrentProcess();
		const auto start = ReadQpc();
		const auto previousCount = SuspendThread(a_thread.handle);
		if (previousCount == DWORD(-1))
		{
			sample.suspendFailed = true;
			return sample;
		}
		__try
		{
			if (!previousCount && GetThreadContext(a_thread.handle, &context) && ValidStack(context.Rsp, a_thread))
			{
				const auto bytes = (std::min)(a_thread.stackHigh - context.Rsp, s_stackCopyBytes);
				// Kernel-mediated reads return failure instead of dispatching a fault to process handlers.
				captured = ReadProcessMemory(process, reinterpret_cast<const void*>(context.Rsp),
					stack.data(), bytes, &copied) && copied == bytes;
			}
		}
		__finally
		{
			resumed = ResumeThread(a_thread.handle);
		}
		sample.suspendedTicks = ReadQpc() - start;
		if (previousCount || resumed != 1)
		{
			sample.suspendFailed = true;
			return sample;
		}
		if (!captured)
		{
			sample.fault = true;
			return sample;
		}
		__try
		{
			if (const auto leaf = a_registry.Find(context.Rip))
				sample.leaf = leaf->id;
			sample.image = Walk(context, a_thread, a_registry,
				std::span{ stack }.first(copied / sizeof(uintptr_t)), a_forceFault);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			sample.fault = true;
		}
		return sample;
	}

	bool ImageSampling::Start(uint32_t a_hz, DWORD a_mainThread) noexcept
	{
		if (m_started.load(std::memory_order_acquire))
			return true;
		if (!ImageRegistry::Get().Start())
			return false;
		m_hz = std::clamp(a_hz, 10u, 1000u);
		m_periodTicks = GetQpcFrequency() / m_hz;
		m_startQpc = ReadQpc();
		m_startTsc = __rdtsc();
		m_mainThread = a_mainThread;
		m_timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
		if (!m_timer)
		{
			REX::WARN("Image Sampling: high-resolution timer unavailable"sv);
			return false;
		}
		const auto thread = CreateThread(nullptr, 0, &Worker, this, CREATE_SUSPENDED, &m_samplerThread);
		if (!thread)
		{
			CloseHandle(m_timer);
			m_timer = nullptr;
			return false;
		}
		m_started.store(true, std::memory_order_release);
		ResumeThread(thread);
		CloseHandle(thread);
		REX::INFO("Image Sampling: active at {} Hz, 256 threads, 16 frames"sv, m_hz);
		return true;
	}

	void ImageSampling::Refresh() noexcept
	{
		std::array<Thread, 256> next{};
		size_t count{};
		const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		THREADENTRY32 entry{ sizeof(entry) };
		if (snapshot != INVALID_HANDLE_VALUE && Thread32First(snapshot, &entry))
		{
			do
			{
				if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == m_samplerThread)
					continue;
				Thread thread;
				if (ReadThread(entry.th32ThreadID, thread))
				{
					for (size_t index = 0; index < m_threadCount; ++index)
						if (m_threads[index].id == thread.id)
							thread.cycles = m_threads[index].cycles;
					next[count++] = thread;
				}
			} while (count < next.size() && Thread32Next(snapshot, &entry));
		}
		if (snapshot != INVALID_HANDLE_VALUE)
			CloseHandle(snapshot);
		for (size_t index = 0; index < m_threadCount; ++index)
			CloseHandle(m_threads[index].handle);
		m_threads = next;
		m_threadCount = count;
	}

	void ImageSampling::Tick() noexcept
	{
		const auto renderThread = Telemetry::RenderThreadIdRelaxed();
		auto& registry = ImageRegistry::Get();
		const auto elapsedQpc = ReadQpc() - m_startQpc;
		const auto elapsedTsc = __rdtsc() - m_startTsc;
		// Threads that ran under 2% of a period keep accumulating instead of being suspended.
		const auto minimumCycles = elapsedQpc ? elapsedTsc / elapsedQpc * m_periodTicks / 50 : 0;
		for (size_t index = 0; index < m_threadCount; ++index)
		{
			auto& thread = m_threads[index];
			uint64_t cycles{};
			if (!QueryThreadCycleTime(thread.handle, &cycles) || cycles - thread.cycles <= minimumCycles)
				continue;
			const auto delta = cycles - thread.cycles;
			thread.cycles = cycles;
			const auto sample = Capture(thread, registry);
			if (sample.suspendFailed)
				m_suspendFailures.fetch_add(1, std::memory_order_relaxed);
			if (sample.fault)
				m_walkFaults.fetch_add(1, std::memory_order_relaxed);
			auto maximum = m_maxSuspendedTicks.load(std::memory_order_relaxed);
			while (maximum < sample.suspendedTicks &&
				!m_maxSuspendedTicks.compare_exchange_weak(maximum, sample.suspendedTicks, std::memory_order_relaxed))
			{}
			if (sample.image != kInvalidImage)
			{
				const size_t role = thread.id == m_mainThread ? 0 : thread.id == renderThread ? 1 : 2;
				auto& row = m_rows.At(sample.image, role);
				row.calls.fetch_add(1, std::memory_order_relaxed);
				row.ticks.fetch_add(delta, std::memory_order_relaxed);
			}
		}
	}

	DWORD WINAPI ImageSampling::Worker(void* a_context) noexcept
	{
		auto& self = *static_cast<ImageSampling*>(a_context);
		const auto frequency = GetQpcFrequency();
		uint64_t refresh{};
		auto next = ReadQpc();
		for (;;)
		{
			const auto now = ReadQpc();
			if (now >= refresh)
			{
				self.Refresh();
				refresh = now + frequency;
			}
			self.Tick();
			next += self.m_periodTicks;
			const auto after = ReadQpc();
			if (next <= after)
				next = after + self.m_periodTicks;
			LARGE_INTEGER due{};
			due.QuadPart = -static_cast<LONGLONG>((next - after) * 10000000 / frequency);
			if (!due.QuadPart)
				due.QuadPart = -1;
			if (!SetWaitableTimer(self.m_timer, &due, 0, nullptr, nullptr, FALSE))
				return 0;
			WaitForSingleObject(self.m_timer, INFINITE);
		}
	}

	std::span<const MetricDescriptor> ImageSampling::Schema() const noexcept
	{
		static constexpr std::array schema{
			MetricDescriptor{ "image.cpu.suspend_failures", Unit::kCount },
			MetricDescriptor{ "image.cpu.max_suspended_us", Unit::kCount },
			MetricDescriptor{ "image.cpu.walk_faults", Unit::kCount },
			MetricDescriptor{ "image.cpu.series_overflow", Unit::kCount }
		};
		return schema;
	}

	void ImageSampling::Drain(std::span<MetricValue> a_out) noexcept
	{
		if (!m_rows.Draining() || a_out.size() != Schema().size())
			return;
		const auto active = m_started.load(std::memory_order_acquire);
		a_out[0] = { static_cast<double>(m_suspendFailures.exchange(0, std::memory_order_relaxed)), active };
		a_out[1] = { static_cast<double>(m_maxSuspendedTicks.exchange(0, std::memory_order_relaxed)) * 1000000.0 / GetQpcFrequency(), active };
		a_out[2] = { static_cast<double>(m_walkFaults.exchange(0, std::memory_order_relaxed)), active };
		a_out[3] = { static_cast<double>(m_rows.TakeOverflow()), active };
	}

	double ImageSampling::TickScale() const noexcept
	{
		const auto elapsedQpc = ReadQpc() - m_startQpc;
		const auto elapsedTsc = __rdtsc() - m_startTsc;
		return elapsedTsc ? static_cast<double>(elapsedQpc) / elapsedTsc : 0.0;
	}
}
