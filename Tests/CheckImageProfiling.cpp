#include "Harness.h"

#include <Telemetry/AdPluginCallbacks.h>
#include <Core/AdClock.h>
#include <Core/AdIAT.h>
#include <Telemetry/AdImageMemory.h>
#include <Telemetry/AdImageSampling.h>
#include <Telemetry/AdAllocatorPoolTelemetry.h>
#include <Modules/AdModuleLibDeflate.h>

#include <atomic>
#include <memory>
#include <vector>

namespace
{
	using namespace Addictol;
	using namespace vmm_tests;

	struct Worker
	{
		using Fill = void* (*)(void*, int, size_t);
		std::atomic<bool> stop{};
		std::atomic<uint64_t> progress{};
		std::unique_ptr<char[]> buffer{ std::make_unique<char[]>(8 * 1024 * 1024) };
		Fill fill{ reinterpret_cast<Fill>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "memset")) };
		HANDLE handle{};
		DWORD id{};
		ImageSampling::Thread thread{};

		Worker()
		{
			require(fill != nullptr, "ntdll memset is unavailable");
			handle = CreateThread(nullptr, 0, [](void* a_context) -> DWORD {
				auto& self = *static_cast<Worker*>(a_context);
				while (!self.stop.load(std::memory_order_relaxed))
				{
					self.fill(self.buffer.get(), 0x5A, 8 * 1024 * 1024);
					self.progress.fetch_add(1, std::memory_order_relaxed);
				}
				return 0;
			}, this, 0, &id);
			require(handle != nullptr, "worker creation failed");
			const auto deadline = GetTickCount64() + 2000;
			while (!progress.load(std::memory_order_relaxed) && GetTickCount64() < deadline)
				Sleep(1);
			if (!ImageSampling::ReadThread(id, thread))
			{
				stop.store(true);
				WaitForSingleObject(handle, INFINITE);
				CloseHandle(handle);
				throw Failure("worker stack limits unavailable");
			}
		}

		~Worker()
		{
			stop.store(true, std::memory_order_relaxed);
			WaitForSingleObject(handle, INFINITE);
			CloseHandle(thread.handle);
			CloseHandle(handle);
		}
	};

	HANDLE s_heap{};
	std::array<std::atomic<uint32_t>, 3> s_chainCalls{};
	decltype(&HeapAlloc) s_alloc{};
	decltype(&HeapReAlloc) s_realloc{};
	decltype(&HeapFree) s_free{};

	LPVOID WINAPI ChainedAlloc(HANDLE a_heap, DWORD a_flags, SIZE_T a_size)
	{
		if (a_heap == s_heap)
			s_chainCalls[0].fetch_add(1);
		return s_alloc(a_heap, a_flags, a_size);
	}

	LPVOID WINAPI ChainedRealloc(HANDLE a_heap, DWORD a_flags, LPVOID a_pointer, SIZE_T a_size)
	{
		if (a_heap == s_heap)
			s_chainCalls[1].fetch_add(1);
		return s_realloc(a_heap, a_flags, a_pointer, a_size);
	}

	BOOL WINAPI ChainedFree(HANDLE a_heap, DWORD a_flags, LPVOID a_pointer)
	{
		if (a_heap == s_heap)
			s_chainCalls[2].fetch_add(1);
		return s_free(a_heap, a_flags, a_pointer);
	}

	F4SE::MessagingInterface::EventCallback* s_callback{};
	F4SE::MessagingInterface::Message* s_received{};
	uint32_t s_registeredHandle{};
	bool FakeRegister(uint32_t a_handle, const char*, void* a_callback)
	{
		s_registeredHandle = a_handle;
		s_callback = reinterpret_cast<F4SE::MessagingInterface::EventCallback*>(a_callback);
		return true;
	}
	F4SE::Impl::F4SEMessagingInterface s_messaging{ F4SE::MessagingInterface::kVersion, &FakeRegister };
	void* FakeQuery(uint32_t a_kind)
	{
		return a_kind == F4SE::LoadInterface::kMessaging ? &s_messaging : nullptr;
	}
	uint32_t FakeHandle()
	{
		return 42;
	}
	void Receive(F4SE::MessagingInterface::Message* a_message)
	{
		s_received = a_message;
		const auto start = ReadQpc();
		while (ReadQpc() == start)
		{}
	}

	HANDLE s_faultTarget{};
	DWORD s_faultCaller{};
	DWORD s_faultSuspendCount{ DWORD(-1) };

	LONG CALLBACK ObserveWalkFault(EXCEPTION_POINTERS* a_exception)
	{
		if (s_faultTarget && GetCurrentThreadId() == s_faultCaller &&
			a_exception->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
		{
			s_faultSuspendCount = SuspendThread(s_faultTarget);
			if (s_faultSuspendCount != DWORD(-1))
				ResumeThread(s_faultTarget);
		}
		return EXCEPTION_CONTINUE_SEARCH;
	}
}

namespace vmm_tests
{
	void run_image_profiling_checks(Runner& runner)
	{
		runner.test("image sampler attributes system work to its calling image", [] {
			auto& registry = ImageRegistry::Get();
			require(registry.Start(), "image registry startup failed");
			const auto executable = registry.Find(reinterpret_cast<uintptr_t>(&run_image_profiling_checks));
			require(executable != nullptr, "test image missing");
			Worker worker;
			bool attributed{};
			const auto deadline = GetTickCount64() + 2000;
			while (GetTickCount64() < deadline && !attributed)
			{
				const auto sample = ImageSampling::Capture(worker.thread, registry);
				const auto leaf = registry.At(sample.leaf);
				attributed = leaf && leaf->kind == ImageKind::kSystem && sample.image == executable->id;
				Sleep(1);
			}
			require(attributed, "no system-leaf stack unwound to the calling executable");
		});

		runner.test("image sampler resumes its target after a stack-walk fault", [] {
			auto& registry = ImageRegistry::Get();
			require(registry.Start(), "image registry startup failed");
			Worker worker;
			s_faultTarget = worker.thread.handle;
			s_faultCaller = GetCurrentThreadId();
			s_faultSuspendCount = DWORD(-1);
			const auto handler = AddVectoredExceptionHandler(1, &ObserveWalkFault);
			require(handler != nullptr, "fault observer registration failed");
			const auto sample = ImageSampling::Capture(worker.thread, registry, true);
			RemoveVectoredExceptionHandler(handler);
			s_faultTarget = nullptr;
			const auto count = SuspendThread(worker.thread.handle);
			if (count != DWORD(-1))
			{
				ResumeThread(worker.thread.handle);
				for (DWORD index = 0; index < count; ++index)
					ResumeThread(worker.thread.handle);
			}
			const auto progress = worker.progress.load(std::memory_order_relaxed);
			const auto deadline = GetTickCount64() + 2000;
			while (worker.progress.load(std::memory_order_relaxed) == progress && GetTickCount64() < deadline)
				Sleep(1);
			require(sample.fault && !sample.suspendFailed, "forced stack-read fault was not exercised");
			require(s_faultSuspendCount == 0, "vectored handlers ran while the target was suspended");
			require(count == 0, "capture left the worker suspended");
			require(worker.progress.load(std::memory_order_relaxed) > progress, "worker stopped after fault");
		});

		runner.test("image callback wrapper preserves arguments and registering plugin attribution", [] {
			auto& registry = ImageRegistry::Get();
			require(registry.Start(), "image registry startup failed");
			const auto executable = registry.Find(reinterpret_cast<uintptr_t>(&FakeRegister));
			require(executable != nullptr, "test image missing");
			static auto* callbacks = new PluginCallbacks;
			callbacks->BeginInterval();
			F4SE::Impl::F4SEInterface load{};
			load.QueryInterface = &FakeQuery;
			load.GetPluginHandle = &FakeHandle;
			callbacks->Install(load, *executable);
			callbacks->BeginLoad(load.GetPluginHandle(), executable->id);
			const auto registered = s_messaging.RegisterListener(load.GetPluginHandle(), "F4SE", reinterpret_cast<void*>(&Receive));
			callbacks->EndLoad();
			require(registered && s_callback && s_callback != &Receive, "fake interface did not register a wrapper");
			F4SE::MessagingInterface::Message message{ "F4SE", F4SE::MessagingInterface::kPreLoadGame, 0, &load };
			s_callback(&message);
			std::vector<SeriesSample> rows(PluginCallbacks::kSeriesCapacity);
			const auto count = callbacks->Drain(rows);
			require(s_received == &message && s_registeredHandle == 42, "callback argument or handle changed");
			bool found{};
			for (size_t index = 0; index < count; ++index)
				found |= rows[index].series == "plugin.message.pre_load_game" &&
					rows[index].bucket == executable->name && rows[index].calls == 1 && rows[index].ticks > 0;
			require(found, "callback timing was not attributed to the registering image");
			s_callback(&message);
			message.type = F4SE::MessagingInterface::kPostLoadGame;
			s_callback(&message);
			require(callbacks->Drain(std::span{ rows }.first(1)) == 1 &&
				callbacks->TakeSeriesOverflow() == 1, "capped callback drain did not count the excess row");

			const auto enabled = bTelemetryEnabled.GetValue();
			bTelemetryEnabled.SetValue(false);
			const auto telemetryOff = ImageSeriesCapacity(true);
			bTelemetryEnabled.SetValue(true);
			const auto featureOff = ImageSeriesCapacity(false);
			const auto featureOn = ImageSeriesCapacity(true);
			bTelemetryEnabled.SetValue(enabled);
			require(telemetryOff == 0 && featureOff == 0 && featureOn == kImageSeriesCapacity,
				"image series capacity ignored feature or telemetry enablement");
			const auto baseline = kMaxHeapClasses * AllocatorPoolTelemetry::kSeriesPerClass +
				ModuleLibDeflate::kSeriesCapacity + kBurstSeriesDrainCapacity;
			const auto before = baseline + kImageCapacity * (PluginCallbacks::kKindCount + 3 + 2) + kBurstSeriesDrainCapacity;
			const auto afterOff = baseline + 3 * telemetryOff;
			const auto afterOn = baseline + 3 * featureOn;
			std::cout << "[INFO] series ring (operation profiling off): sizeof(SeriesSample)=" << sizeof(SeriesSample)
				<< ", buffers=122, before off/on=" << before << " rows/" << before * sizeof(SeriesSample) * 122
				<< " bytes, after off=" << afterOff << " rows/" << afterOff * sizeof(SeriesSample) * 122
				<< " bytes, after on=" << afterOn << " rows/" << afterOn * sizeof(SeriesSample) * 122 << " bytes\n";
		});

		runner.test("image allocator round trip preserves chained hooks and live accounting", [] {
			auto& registry = ImageRegistry::Get();
			require(registry.Start(), "image registry startup failed");
			const auto executable = registry.Find(reinterpret_cast<uintptr_t>(&ChainedAlloc));
			require(executable != nullptr, "test image missing");
			s_heap = HeapCreate(0, 0, 0);
			require(s_heap != nullptr, "private test heap creation failed");
			s_alloc = reinterpret_cast<decltype(s_alloc)>(RELEX::PatchImport(executable->base, "kernel32.dll", "HeapAlloc", reinterpret_cast<uintptr_t>(&ChainedAlloc)));
			s_realloc = reinterpret_cast<decltype(s_realloc)>(RELEX::PatchImport(executable->base, "kernel32.dll", "HeapReAlloc", reinterpret_cast<uintptr_t>(&ChainedRealloc)));
			s_free = reinterpret_cast<decltype(s_free)>(RELEX::PatchImport(executable->base, "kernel32.dll", "HeapFree", reinterpret_cast<uintptr_t>(&ChainedFree)));
			require(s_alloc && s_realloc && s_free, "pre-existing IAT hook installation failed");
			static auto* memory = new ImageMemory;
			require(memory->Patch(*executable), "image allocator patching failed");
			const auto baseline = memory->LiveBytes(executable->id);
			auto block = HeapAlloc(s_heap, 0, 128 * 1024);
			const auto allocated = memory->LiveBytes(executable->id);
			auto resized = block ? HeapReAlloc(s_heap, 0, block, 256 * 1024) : nullptr;
			const auto reallocated = memory->LiveBytes(executable->id);
			const bool freed = (resized || block) && HeapFree(s_heap, 0, resized ? resized : block);
			const auto final = memory->LiveBytes(executable->id);
			HeapDestroy(s_heap);
			s_heap = nullptr;
			require(block && resized && freed, "large allocation round trip failed");
			require(allocated == baseline + 128 * 1024 && reallocated == baseline + 256 * 1024 &&
				final == baseline, "large-block live accounting did not return to baseline");
			require(s_chainCalls[0] == 1 && s_chainCalls[1] == 1 && s_chainCalls[2] == 1,
				"previous hook did not see exactly one allocation, reallocation and free");
		});
	}
}
