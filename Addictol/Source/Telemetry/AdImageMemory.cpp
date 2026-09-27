#include <Telemetry/AdImageMemory.h>
#include <Core/AdIAT.h>
#include <xbyak/xbyak.h>
#include <REX/REX.h>

#include <algorithm>
#include <cstring>
#include <malloc.h>

namespace Addictol
{
	using namespace std::literals;

	namespace
	{
		constexpr size_t s_largeBlock{ 64 * 1024 };
		constexpr std::array<ImageSeriesKind, 2> s_kinds{
			ImageSeriesKind{ "image.memory.alloc" }, { "image.memory.live", {}, true }
		};
		constexpr std::array<const char*, 12> s_importNames{
			"malloc", "calloc", "realloc", "free", "_aligned_malloc", "_aligned_realloc", "_aligned_free",
			"HeapAlloc", "HeapReAlloc", "HeapFree", "VirtualAlloc", "VirtualFree"
		};

		bool IsLibrary(const char* a_library, size_t a_import) noexcept
		{
			if (a_import >= 7)
				return _stricmp(a_library, "kernel32.dll") == 0 || _stricmp(a_library, "kernelbase.dll") == 0;
			return _stricmp(a_library, "api-ms-win-crt-heap-l1-1-0.dll") == 0 ||
				_stricmp(a_library, "ucrtbase.dll") == 0 ||
				(_strnicmp(a_library, "msvcr", 5) == 0 && std::strstr(a_library, ".dll"));
		}

		size_t Hash(uintptr_t a_pointer) noexcept
		{
			auto value = a_pointer >> 4;
			value ^= value >> 17;
			value *= 0x9E3779B97F4A7C15ull;
			return value ^ (value >> 32);
		}
	}

	ImageMemory::ImageMemory() noexcept :
		ImageSeriesSource(bTelemetryImageMemory, s_kinds)
	{
		s_instance = this;
	}

	bool ImageMemory::Start() noexcept
	{
		if (m_started)
			return true;
		m_started = ImageRegistry::Get().Subscribe(&OnImage, this);
		if (m_started)
			REX::INFO("Image Memory: active; large-block threshold 65536, delay imports and late DllMain allocations excluded"sv);
		return m_started;
	}

	void ImageMemory::OnImage(const Image& a_image, void* a_context) noexcept
	{
		if (a_image.kind == ImageKind::kOther)
			(void)static_cast<ImageMemory*>(a_context)->Patch(a_image);
	}

	bool ImageMemory::Patch(const Image& a_image) noexcept
	{
		const std::scoped_lock lock{ m_patchMutex };
		if (a_image.id >= kImageCapacity || m_patched[a_image.id])
			return false;
		HMODULE module{};
		if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(a_image.base), &module))
			return false;
		struct Preflight
		{
			std::array<RELEX::PointerPatch, 128> patches{};
			std::array<Import, 128> imports{};
			size_t count{};
		} preflight;
		bool success = RELEX::VisitImports(a_image.base, [](const RELEX::ImportSlot& a_slot, void* a_context) noexcept {
			auto& plan = *static_cast<Preflight*>(a_context);
			for (size_t index = 0; index < s_importNames.size(); ++index)
			{
				if (std::strcmp(a_slot.name, s_importNames[index]) != 0 || !IsLibrary(a_slot.library, index))
					continue;
				if (plan.count == plan.patches.size() || !*a_slot.address)
					return false;
				plan.imports[plan.count] = static_cast<Import>(index);
				plan.patches[plan.count++].address = a_slot.address;
			}
			return true;
		}, &preflight);
		if (success && preflight.count && m_slotCount + preflight.count <= m_slots.size())
		{
			const auto bytes = (preflight.count + 1) * 64;
			const auto memory = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
			if (!memory)
				success = false;
			else
			{
				RUNTIME_FUNCTION* unwindTable{};
				bool registered{};
				try
				{
					Xbyak::CodeGenerator code(bytes, memory);
					// r11 is private to the stub; marshal it before any compiler-generated code.
					const auto entry = code.getCurr();
					code.sub(code.rsp, 56);
					const auto prologueSize = code.getSize();
					code.mov(code.ptr[code.rsp + 32], code.r9);
					code.mov(code.r9, code.r8);
					code.mov(code.r8, code.rdx);
					code.mov(code.rdx, code.rcx);
					code.mov(code.rcx, code.r11);
					code.mov(code.rax, reinterpret_cast<uintptr_t>(&Hook));
					code.call(code.rax);
					code.add(code.rsp, 56);
					code.ret();
					const auto entryEnd = code.getSize();
					for (size_t index = 0; index < preflight.count; ++index)
					{
						preflight.patches[index].target = const_cast<uint8_t*>(code.getCurr());
						code.mov(code.r11, m_slotCount + index);
						code.jmp(entry);
					}
					code.align(4);
					const auto unwindOffset = code.getSize();
					// UWOP_ALLOC_SMALL describes the adapter's 56-byte stack frame for exception propagation.
					const std::initializer_list<uint8_t> unwind{
						1, static_cast<uint8_t>(prologueSize), 1, 0,
						static_cast<uint8_t>(prologueSize), 0x62, 0, 0
					};
					code.db(unwind.begin(), static_cast<int>(unwind.size()));
					unwindTable = reinterpret_cast<RUNTIME_FUNCTION*>(const_cast<uint8_t*>(code.getCurr()));
					code.dd(0);
					code.dd(static_cast<uint32_t>(entryEnd));
					code.dd(static_cast<uint32_t>(unwindOffset));
					code.ready();
					DWORD ignored{};
					success = VirtualProtect(memory, bytes, PAGE_EXECUTE_READ, &ignored) != FALSE;
					if (success)
						success = FlushInstructionCache(GetCurrentProcess(), memory, bytes) != FALSE;
					if (success)
					{
						registered = RtlAddFunctionTable(unwindTable, 1, reinterpret_cast<DWORD64>(memory)) != FALSE;
						success = registered;
					}
				}
				catch (...)
				{
					success = false;
				}
				const std::span patches{ preflight.patches.data(), preflight.count };
				if (success)
					success = RELEX::PreparePointers(patches);
				if (success)
				{
					for (size_t index = 0; index < preflight.count; ++index)
						m_slots[m_slotCount + index] = { a_image.id, preflight.imports[index], patches[index].previous };
					m_slotCount += preflight.count;
					RELEX::PublishPointers(patches);
				}
				else
				{
					if (registered)
						RtlDeleteFunctionTable(unwindTable);
					VirtualFree(memory, 0, MEM_RELEASE);
				}
			}
		}
		else if (preflight.count)
			success = false;
		if (success)
		{
			m_patched[a_image.id] = true;
			REX::INFO("Image Memory: {} patched ({} imports)"sv, a_image.name, preflight.count);
		}
		else
		{
			m_skipped.fetch_add(1, std::memory_order_relaxed);
			REX::WARN("Image Memory: {} skipped (import preflight, capacity or protection)"sv, a_image.name);
		}
		FreeLibrary(module);
		return success;
	}

	uintptr_t ImageMemory::Hook(size_t a_slot, uintptr_t a_first, uintptr_t a_second, uintptr_t a_third, uintptr_t a_fourth)
	{
		auto& self = *s_instance;
		const auto& slot = self.m_slots[a_slot];
		if (s_inside)
			return self.Invoke(slot, a_first, a_second, a_third, a_fourth);
		uintptr_t result{};
		s_inside = true;
		__try
		{
			result = self.Account(slot, a_first, a_second, a_third, a_fourth);
		}
		__finally
		{
			s_inside = false;
		}
		return result;
	}

	uintptr_t ImageMemory::Invoke(const Slot& a_slot, uintptr_t a_first, uintptr_t a_second, uintptr_t a_third, uintptr_t a_fourth)
	{
		const auto first = reinterpret_cast<void*>(a_first);
		const auto third = reinterpret_cast<void*>(a_third);
		switch (a_slot.import)
		{
		case Import::kMalloc:
			return reinterpret_cast<uintptr_t>(reinterpret_cast<decltype(&malloc)>(a_slot.previous)(a_first));
		case Import::kCalloc:
			return reinterpret_cast<uintptr_t>(reinterpret_cast<decltype(&calloc)>(a_slot.previous)(a_first, a_second));
		case Import::kRealloc:
			return reinterpret_cast<uintptr_t>(reinterpret_cast<decltype(&realloc)>(a_slot.previous)(first, a_second));
		case Import::kFree:
			reinterpret_cast<decltype(&free)>(a_slot.previous)(first);
			return 0;
		case Import::kAlignedMalloc:
			return reinterpret_cast<uintptr_t>(reinterpret_cast<decltype(&_aligned_malloc)>(a_slot.previous)(a_first, a_second));
		case Import::kAlignedRealloc:
			return reinterpret_cast<uintptr_t>(reinterpret_cast<decltype(&_aligned_realloc)>(a_slot.previous)(first, a_second, a_third));
		case Import::kAlignedFree:
			reinterpret_cast<decltype(&_aligned_free)>(a_slot.previous)(first);
			return 0;
		case Import::kHeapAlloc:
			return reinterpret_cast<uintptr_t>(reinterpret_cast<decltype(&HeapAlloc)>(a_slot.previous)(first, static_cast<DWORD>(a_second), a_third));
		case Import::kHeapReAlloc:
			return reinterpret_cast<uintptr_t>(reinterpret_cast<decltype(&HeapReAlloc)>(a_slot.previous)(first, static_cast<DWORD>(a_second), third, a_fourth));
		case Import::kHeapFree:
			return reinterpret_cast<decltype(&HeapFree)>(a_slot.previous)(first, static_cast<DWORD>(a_second), third);
		case Import::kVirtualAlloc:
			return reinterpret_cast<uintptr_t>(reinterpret_cast<decltype(&VirtualAlloc)>(a_slot.previous)(first, a_second,
				static_cast<DWORD>(a_third), static_cast<DWORD>(a_fourth)));
		case Import::kVirtualFree:
			return reinterpret_cast<decltype(&VirtualFree)>(a_slot.previous)(first, a_second, static_cast<DWORD>(a_third));
		}
		return 0;
	}

	uintptr_t ImageMemory::Account(const Slot& a_slot, uintptr_t a_first, uintptr_t a_second, uintptr_t a_third, uintptr_t a_fourth)
	{
		const auto kind = a_slot.import;
		const bool region = kind == Import::kVirtualAlloc || kind == Import::kVirtualFree;
		const bool freeing = kind == Import::kFree || kind == Import::kAlignedFree ||
			kind == Import::kHeapFree || kind == Import::kVirtualFree;
		const bool resizing = kind == Import::kRealloc || kind == Import::kAlignedRealloc || kind == Import::kHeapReAlloc;
		const auto oldPointer = kind == Import::kHeapFree || kind == Import::kHeapReAlloc ? a_third : a_first;
		Block old{};
		if (freeing || resizing)
		{
			if (!region || (a_third & MEM_RELEASE) || ((a_third & MEM_DECOMMIT) && a_second))
				old = Remove(oldPointer, region, region && !(a_third & MEM_RELEASE) ? a_second : 0);
		}
		uintptr_t result{};
		bool completed{};
		__try
		{
			result = Invoke(a_slot, a_first, a_second, a_third, a_fourth);
			completed = true;
		}
		__finally
		{
			if (!completed && old.pointer)
				Insert(old);
		}
		const auto lastError = GetLastError();
		auto& counters = m_rows.At(a_slot.image, kAlloc);
		if (freeing)
		{
			m_freeCalls.fetch_add(1, std::memory_order_relaxed);
			if (!result && (kind == Import::kHeapFree || kind == Import::kVirtualFree) && old.pointer)
				Insert(old);
			SetLastError(lastError);
			return result;
		}
		uint64_t size = a_first;
		switch (kind)
		{
		case Import::kCalloc: size = a_second && a_first > UINT64_MAX / a_second ? 0 : a_first * a_second; break;
		case Import::kRealloc:
		case Import::kAlignedRealloc:
		case Import::kVirtualAlloc: size = a_second; break;
		case Import::kHeapAlloc: size = a_third; break;
		case Import::kHeapReAlloc: size = a_fourth; break;
		default: break;
		}
		if (!region || (a_third & MEM_COMMIT))
		{
			counters.calls.fetch_add(1, std::memory_order_relaxed);
			if (result)
			{
				counters.bytes.fetch_add(size, std::memory_order_relaxed);
				if (region)
				{
					MEMORY_BASIC_INFORMATION info{};
					if (VirtualQuery(reinterpret_cast<void*>(result), &info, sizeof(info)) &&
						info.AllocationBase && info.State == MEM_COMMIT)
					{
						const auto base = reinterpret_cast<uintptr_t>(info.AllocationBase);
						auto cursor = base;
						uint64_t committed{};
						while (VirtualQuery(reinterpret_cast<void*>(cursor), &info, sizeof(info)) &&
							reinterpret_cast<uintptr_t>(info.AllocationBase) == base)
						{
							if (info.State == MEM_COMMIT)
								committed += info.RegionSize;
							if (!info.RegionSize || info.RegionSize > UINTPTR_MAX - cursor)
								break;
							cursor += info.RegionSize;
						}
						const auto previous = Remove(base, true);
						Insert({ base, committed, previous.pointer ? previous.image : a_slot.image, true, cursor - base });
					}
				}
				else
					Insert({ result, size, a_slot.image, false });
			}
		}
		if (resizing && !result && old.pointer && (size || kind == Import::kHeapReAlloc))
			Insert(old);
		SetLastError(lastError);
		return result;
	}

	void ImageMemory::Insert(Block a_block) noexcept
	{
		if (!a_block.pointer || a_block.size < s_largeBlock)
			return;
		const auto hash = Hash(a_block.pointer);
		const auto stripe = hash % kStripeCount;
		auto& lock = m_locks[stripe];
		while (lock.test_and_set(std::memory_order_acquire))
			YieldProcessor();
		size_t available = kBlockCapacity;
		for (size_t probe = 0; probe < kBlockCapacity / kStripeCount; ++probe)
		{
			const auto index = (((hash / kStripeCount) + probe) % (kBlockCapacity / kStripeCount)) * kStripeCount + stripe;
			auto& block = m_blocks[index];
			if (!block.pointer)
			{
				available = index;
				break;
			}
			if (block.pointer == a_block.pointer && block.region == a_block.region)
			{
				m_rows.At(block.image, kLive).bytes.fetch_sub(block.size, std::memory_order_relaxed);
				m_rows.At(block.image, kLive).calls.fetch_sub(1, std::memory_order_relaxed);
				available = index;
				break;
			}
		}
		if (available != kBlockCapacity)
		{
			m_blocks[available] = a_block;
			m_rows.At(a_block.image, kLive).bytes.fetch_add(a_block.size, std::memory_order_relaxed);
			m_rows.At(a_block.image, kLive).calls.fetch_add(1, std::memory_order_relaxed);
		}
		else
			m_overflow.fetch_add(1, std::memory_order_relaxed);
		lock.clear(std::memory_order_release);
	}

	ImageMemory::Block ImageMemory::Remove(uintptr_t a_pointer, bool a_region, uint64_t a_fullSize) noexcept
	{
		if (!a_pointer)
			return {};
		const auto hash = Hash(a_pointer);
		const auto stripe = hash % kStripeCount;
		auto& lock = m_locks[stripe];
		while (lock.test_and_set(std::memory_order_acquire))
			YieldProcessor();
		Block result{};
		for (size_t probe = 0; probe < kBlockCapacity / kStripeCount; ++probe)
		{
			const auto index = (((hash / kStripeCount) + probe) % (kBlockCapacity / kStripeCount)) * kStripeCount + stripe;
			auto& block = m_blocks[index];
			if (!block.pointer)
				break;
			if (block.pointer == a_pointer && block.region == a_region)
			{
				if (!a_fullSize || a_fullSize >= (block.extent ? block.extent : block.size))
				{
					result = block;
					constexpr auto length = kBlockCapacity / kStripeCount;
					auto hole = index / kStripeCount;
					m_blocks[index] = {};
					for (size_t step = 1; step < length; ++step)
					{
						const auto position = (index / kStripeCount + step) % length;
						auto& next = m_blocks[position * kStripeCount + stripe];
						if (!next.pointer)
							break;
						const auto home = (Hash(next.pointer) / kStripeCount) % length;
						if ((hole + length - home) % length < (position + length - home) % length)
						{
							m_blocks[hole * kStripeCount + stripe] = next;
							next = {};
							hole = position;
						}
					}
					m_rows.At(result.image, kLive).bytes.fetch_sub(result.size, std::memory_order_relaxed);
					m_rows.At(result.image, kLive).calls.fetch_sub(1, std::memory_order_relaxed);
				}
				break;
			}
		}
		lock.clear(std::memory_order_release);
		return result;
	}

	uint64_t ImageMemory::LiveBytes(ImageId a_image) const noexcept
	{
		return a_image < kImageCapacity ? m_rows.At(a_image, kLive).bytes.load(std::memory_order_relaxed) : 0;
	}

	std::span<const MetricDescriptor> ImageMemory::Schema() const noexcept
	{
		static constexpr std::array schema{
			MetricDescriptor{ "image.memory.free_calls", Unit::kCount },
			MetricDescriptor{ "image.memory.live_overflow", Unit::kCount },
			MetricDescriptor{ "image.memory.skipped_images", Unit::kCount },
			MetricDescriptor{ "image.memory.series_overflow", Unit::kCount }
		};
		return schema;
	}

	void ImageMemory::Drain(std::span<MetricValue> a_out) noexcept
	{
		if (!m_rows.Draining() || a_out.size() != Schema().size())
			return;
		const auto frees = m_freeCalls.exchange(0, std::memory_order_relaxed);
		const auto started = m_started.load(std::memory_order_acquire);
		a_out[0] = { static_cast<double>(frees), started };
		a_out[1] = { static_cast<double>(m_overflow.exchange(0, std::memory_order_relaxed)), started };
		a_out[2] = { static_cast<double>(m_skipped.exchange(0, std::memory_order_relaxed)), started };
		a_out[3] = { static_cast<double>(m_rows.TakeOverflow()), started };
	}
}
