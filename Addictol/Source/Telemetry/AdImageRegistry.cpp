#include <Telemetry/AdImageRegistry.h>
#include <Telemetry/AdLoadTiming.h>

#include <Psapi.h>
#include <winternl.h>
#include <REX/REX.h>

#include <algorithm>
#include <cstring>
#include <new>

namespace Addictol
{
	using namespace std::literals;

	namespace
	{
		struct DllNotification
		{
			ULONG flags;
			const UNICODE_STRING* fullName;
			const UNICODE_STRING* baseName;
			void* base;
			ULONG size;
		};

		bool ReadImage(Image& a_image) noexcept
		{
			__try
			{
				const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(a_image.base);
				if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
					return false;
				const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(a_image.base + dos->e_lfanew);
				if (nt->Signature != IMAGE_NT_SIGNATURE ||
					nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
					return false;
				a_image.end = a_image.base + nt->OptionalHeader.SizeOfImage;
				const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
				if (directory.VirtualAddress && directory.Size &&
					directory.VirtualAddress < nt->OptionalHeader.SizeOfImage &&
					directory.Size <= nt->OptionalHeader.SizeOfImage - directory.VirtualAddress)
				{
					a_image.functions = reinterpret_cast<RUNTIME_FUNCTION*>(a_image.base + directory.VirtualAddress);
					a_image.functionCount = directory.Size / sizeof(RUNTIME_FUNCTION);
				}
				return a_image.end > a_image.base;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}
	}

	ImageRegistry& ImageRegistry::Get() noexcept
	{
		// Hooks and sampler readers outlive ordinary static destruction.
		static auto* registry = new ImageRegistry;
		return *registry;
	}

	bool ImageRegistry::Start() noexcept
	{
		const std::scoped_lock lock{ m_startMutex };
		if (m_started)
			return true;
		if (m_startAttempted)
			return false;
		m_startAttempted = true;
		using Register = LONG(NTAPI*)(ULONG, decltype(&Notify), void*, void**);
		const auto registration = reinterpret_cast<Register>(
			GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "LdrRegisterDllNotification"));
		if (!registration)
			return false;
		for (size_t index = 0; index < kQueueCapacity; ++index)
			m_queue[index].sequence.store(index, std::memory_order_relaxed);
		const auto windowsLength = GetWindowsDirectoryW(m_windows, static_cast<UINT>(std::size(m_windows)));
		if (!windowsLength || windowsLength >= std::size(m_windows))
			return false;
		HMODULE own{};
		if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
			reinterpret_cast<LPCWSTR>(&Notify), &own))
			return false;
		m_ownBase = reinterpret_cast<uintptr_t>(own);
		m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		m_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!m_event || !m_ready || registration(0, &Notify, this, &m_cookie) < 0)
		{
			if (m_event)
				CloseHandle(m_event);
			if (m_ready)
				CloseHandle(m_ready);
			return false;
		}
		const auto thread = CreateThread(nullptr, 0, &Worker, this, 0, nullptr);
		if (!thread)
		{
			using Unregister = LONG(NTAPI*)(void*);
			const auto unregister = reinterpret_cast<Unregister>(
				GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "LdrUnregisterDllNotification"));
			if (unregister && unregister(m_cookie) >= 0)
			{
				CloseHandle(m_event);
				CloseHandle(m_ready);
			}
			return false;
		}
		CloseHandle(thread);
		WaitForSingleObject(m_ready, INFINITE);
		m_started = Count() != 0 && m_snapshot.load(std::memory_order_acquire) != nullptr;
		if (m_started)
			REX::INFO("Image Registry: active, {} images"sv, Count());
		else
			REX::WARN("Image Registry: initial publication failed"sv);
		return m_started;
	}

	void CALLBACK ImageRegistry::Notify(ULONG a_reason, const void* a_data, void* a_context) noexcept
	{
		auto& self = *static_cast<ImageRegistry*>(a_context);
		const auto& data = *static_cast<const DllNotification*>(a_data);
		auto position = self.m_write.load(std::memory_order_relaxed);
		for (;;)
		{
			auto& slot = self.m_queue[position % kQueueCapacity];
			const auto sequence = slot.sequence.load(std::memory_order_acquire);
			const auto difference = static_cast<intptr_t>(sequence - position);
			if (difference < 0)
			{
				self.m_dropped.fetch_add(1, std::memory_order_relaxed);
				SetEvent(self.m_event);
				return;
			}
			if (difference == 0 && self.m_write.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
			{
				slot.event.reason = a_reason;
				slot.event.base = reinterpret_cast<uintptr_t>(data.base);
				slot.sequence.store(position + 1, std::memory_order_release);
				SetEvent(self.m_event);
				return;
			}
			position = self.m_write.load(std::memory_order_relaxed);
		}
	}

	bool ImageRegistry::Pop(Event& a_event) noexcept
	{
		auto& slot = m_queue[m_read % kQueueCapacity];
		if (slot.sequence.load(std::memory_order_acquire) != m_read + 1)
			return false;
		a_event = slot.event;
		slot.sequence.store(m_read + kQueueCapacity, std::memory_order_release);
		++m_read;
		return true;
	}

	DWORD WINAPI ImageRegistry::Worker(void* a_context) noexcept
	{
		auto& self = *static_cast<ImageRegistry*>(a_context);
		uint64_t dropped{};
		for (;;)
		{
			{
				const std::scoped_lock lock{ self.m_updateMutex };
				if (!self.m_snapshot.load(std::memory_order_relaxed))
					self.Reconcile();
				Event event;
				while (self.Pop(event))
				{
					if (event.reason == 2)
						self.Remove(event.base);
					else if (event.reason == 1)
						self.Add(event.base);
				}
				const auto now = self.DroppedNotifications();
				if (now != dropped)
				{
					REX::WARN("Image Registry: notification queue overflow ({}), reconciling"sv, now);
					self.Reconcile();
					dropped = now;
				}
				self.Publish();
			}
			SetEvent(self.m_ready);
			WaitForSingleObject(self.m_event, INFINITE);
		}
	}

	void ImageRegistry::Reconcile() noexcept
	{
		std::array<HMODULE, 2048> modules{};
		DWORD bytes{};
		if (!EnumProcessModules(GetCurrentProcess(), modules.data(), sizeof(modules), &bytes) || bytes > sizeof(modules))
		{
			REX::WARN("Image Registry: module enumeration failed or exceeded capacity"sv);
			return;
		}
		const auto count = bytes / sizeof(HMODULE);
		for (size_t index = 0; index < Count(); ++index)
		{
			auto& image = m_images[index];
			if (std::find(modules.begin(), modules.begin() + count, reinterpret_cast<HMODULE>(image.base)) == modules.begin() + count)
				image.alive.store(false, std::memory_order_release);
		}
		for (size_t index = 0; index < count; ++index)
			Add(reinterpret_cast<uintptr_t>(modules[index]));
	}

	void ImageRegistry::Add(uintptr_t a_base) noexcept
	{
		for (size_t index = 0; index < Count(); ++index)
			if (m_images[index].base == a_base && m_images[index].alive.load(std::memory_order_relaxed))
				return;
		if (Count() == kImageCapacity)
		{
			REX::WARN("Image Registry: permanent image ID capacity exhausted"sv);
			return;
		}
		HMODULE module{};
		if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(a_base), &module))
			return;
		auto& image = m_images[Count()];
		image.base = a_base;
		image.functions = nullptr;
		image.functionCount = 0;
		wchar_t path[1024]{};
		char utf8[4096]{};
		const auto length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
		if (!length || length >= std::size(path) || !ReadImage(image) ||
			!WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), nullptr, nullptr))
		{
			FreeLibrary(module);
			return;
		}
		const auto name = LoadTiming::FileNameFromPath(utf8);
		if (name.empty() || name.size() >= std::size(image.name))
		{
			FreeLibrary(module);
			return;
		}
		std::memcpy(image.name, name.data(), name.size());
		image.name[name.size()] = '\0';
		const auto windowsLength = std::wcslen(m_windows);
		image.kind = a_base == m_ownBase ? ImageKind::kAddictol :
			a_base == reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) ? ImageKind::kGame :
			windowsLength && _wcsnicmp(path, m_windows, windowsLength) == 0 &&
				(path[windowsLength] == L'\\' || path[windowsLength] == L'/') ? ImageKind::kSystem : ImageKind::kOther;
		image.id = static_cast<ImageId>(Count());
		image.alive.store(true, std::memory_order_release);
		m_count.store(Count() + 1, std::memory_order_release);
		for (size_t index = 0; index < m_consumerCount; ++index)
			m_consumers[index].callback(image, m_consumers[index].context);
		FreeLibrary(module);
	}

	void ImageRegistry::Remove(uintptr_t a_base) noexcept
	{
		for (size_t index = 0; index < Count(); ++index)
			if (m_images[index].base == a_base)
				m_images[index].alive.store(false, std::memory_order_release);
	}

	void ImageRegistry::Publish() noexcept
	{
		Snapshot next;
		for (size_t index = 0; index < Count(); ++index)
			if (m_images[index].alive.load(std::memory_order_relaxed))
				next.ids[next.count++] = static_cast<ImageId>(index);
		std::sort(next.ids.begin(), next.ids.begin() + next.count,
			[this](ImageId a_left, ImageId a_right) { return m_images[a_left].base < m_images[a_right].base; });
		const auto current = m_snapshot.load(std::memory_order_relaxed);
		if (current && current->count == next.count &&
			std::equal(next.ids.begin(), next.ids.begin() + next.count, current->ids.begin()))
			return;
		auto snapshot = new (std::nothrow) Snapshot{ next };
		if (!snapshot)
		{
			REX::WARN("Image Registry: snapshot allocation failed"sv);
			return;
		}
		m_snapshot.store(snapshot, std::memory_order_release);
	}

	const Image* ImageRegistry::Find(uintptr_t a_address) const noexcept
	{
		const auto snapshot = m_snapshot.load(std::memory_order_acquire);
		if (!snapshot)
			return nullptr;
		size_t first{}, last = snapshot->count;
		while (first < last)
		{
			const auto middle = first + (last - first) / 2;
			const auto& image = m_images[snapshot->ids[middle]];
			if (a_address < image.base)
				last = middle;
			else if (a_address >= image.end)
				first = middle + 1;
			else
				return image.alive.load(std::memory_order_acquire) ? &image : nullptr;
		}
		return nullptr;
	}

	const Image* ImageRegistry::Resolve(uintptr_t a_base) noexcept
	{
		if (const auto image = Find(a_base))
			return image;
		const std::scoped_lock lock{ m_updateMutex };
		Add(a_base);
		Publish();
		return Find(a_base);
	}

	RUNTIME_FUNCTION* ImageRegistry::FunctionEntry(uintptr_t a_address) const noexcept
	{
		const auto image = Find(a_address);
		if (!image)
			return nullptr;
		size_t first{}, last = image->functionCount;
		const auto offset = a_address - image->base;
		while (first < last)
		{
			const auto middle = first + (last - first) / 2;
			auto& function = image->functions[middle];
			if (offset < function.BeginAddress)
				last = middle;
			else if (offset >= function.EndAddress)
				first = middle + 1;
			else
				return &function;
		}
		return nullptr;
	}

	const Image* ImageRegistry::At(ImageId a_id) const noexcept
	{
		return a_id < Count() ? &m_images[a_id] : nullptr;
	}

	size_t ImageRegistry::Count() const noexcept
	{
		return m_count.load(std::memory_order_acquire);
	}

	bool ImageRegistry::Subscribe(Consumer a_consumer, void* a_context) noexcept
	{
		if (!Start() || !a_consumer)
			return false;
		const std::scoped_lock lock{ m_updateMutex };
		if (m_consumerCount == m_consumers.size())
			return false;
		m_consumers[m_consumerCount++] = { a_consumer, a_context };
		for (size_t index = 0; index < Count(); ++index)
			if (m_images[index].alive.load(std::memory_order_acquire))
				a_consumer(m_images[index], a_context);
		return true;
	}

	uint64_t ImageRegistry::DroppedNotifications() const noexcept
	{
		return m_dropped.load(std::memory_order_relaxed);
	}
}
