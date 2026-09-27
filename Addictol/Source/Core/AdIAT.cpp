#include <Core/AdIAT.h>

#include <cstring>

namespace RELEX
{
	using namespace std::literals;

	bool VisitImports(uintptr_t a_base, ImportVisitor a_visitor, void* a_context) noexcept
	{
		__try
		{
			const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(a_base);
			if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
				return false;
			const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(a_base + dos->e_lfanew);
			if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
				return false;
			const auto size = nt->OptionalHeader.SizeOfImage;
			const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
			if (!directory.VirtualAddress)
				return true;
			if (directory.VirtualAddress >= size || directory.Size > size - directory.VirtualAddress)
				return false;
			const auto imports = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(a_base + directory.VirtualAddress);
			for (size_t index = 0; index < directory.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR); ++index)
			{
				const auto& descriptor = imports[index];
				if (!descriptor.Name)
					return true;
				if (descriptor.Name >= size || !descriptor.OriginalFirstThunk || !descriptor.FirstThunk)
					return false;
				const auto library = reinterpret_cast<const char*>(a_base + descriptor.Name);
				if (!std::memchr(library, 0, size - descriptor.Name))
					return false;
				for (size_t thunk = 0;; ++thunk)
				{
					const auto nameOffset = size_t(descriptor.OriginalFirstThunk) + thunk * sizeof(IMAGE_THUNK_DATA64);
					const auto slotOffset = size_t(descriptor.FirstThunk) + thunk * sizeof(IMAGE_THUNK_DATA64);
					if (nameOffset > size || size - nameOffset < sizeof(IMAGE_THUNK_DATA64) ||
						slotOffset > size || size - slotOffset < sizeof(void*))
						return false;
					const auto name = reinterpret_cast<const IMAGE_THUNK_DATA64*>(a_base + nameOffset)->u1.AddressOfData;
					if (!name)
						break;
					if (IMAGE_SNAP_BY_ORDINAL64(name))
						continue;
					if (name >= size || size - name <= sizeof(WORD))
						return false;
					const auto function = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(a_base + name)->Name;
					if (!std::memchr(function, 0, size - name - sizeof(WORD)) ||
						!a_visitor({ reinterpret_cast<void**>(a_base + slotOffset), library, function }, a_context))
						return false;
				}
			}
			return false;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool PreparePointers(std::span<PointerPatch> a_patches) noexcept
	{
		size_t prepared{};
		for (auto& patch : a_patches)
		{
			MEMORY_BASIC_INFORMATION memory{};
			if (!patch.address || (reinterpret_cast<uintptr_t>(patch.address) % alignof(void*)) ||
				!VirtualQuery(patch.address, &memory, sizeof(memory)) || memory.State != MEM_COMMIT ||
				(memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
			{
				RestorePointersProtection(a_patches.first(prepared));
				return false;
			}
			const auto writable = memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY) ?
				PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
			if (!VirtualProtect(patch.address, sizeof(void*), writable, &patch.protection))
			{
				RestorePointersProtection(a_patches.first(prepared));
				return false;
			}
			patch.previous = InterlockedCompareExchangePointer(patch.address, nullptr, nullptr);
			++prepared;
			if (!patch.previous)
			{
				RestorePointersProtection(a_patches.first(prepared));
				return false;
			}
		}
		return true;
	}

	void RestorePointersProtection(std::span<PointerPatch> a_patches) noexcept
	{
		for (size_t index = a_patches.size(); index > 0; --index)
		{
			auto& patch = a_patches[index - 1];
			DWORD ignored{};
			if (!VirtualProtect(patch.address, sizeof(void*), patch.protection, &ignored))
				REX::WARN("IAT: could not restore protection at {}"sv, static_cast<void*>(patch.address));
		}
	}

	void PublishPointers(std::span<PointerPatch> a_patches) noexcept
	{
		for (auto& patch : a_patches)
			InterlockedExchangePointer(patch.address, patch.target);
		RestorePointersProtection(a_patches);
	}

	uintptr_t PatchImport(uintptr_t a_base, const char* a_library, const char* a_name, uintptr_t a_target) noexcept
	{
		struct Search
		{
			const char* library;
			const char* name;
			PointerPatch patch;
		} search{ a_library, a_name, { nullptr, reinterpret_cast<void*>(a_target) } };
		if (!VisitImports(a_base, [](const ImportSlot& a_slot, void* a_context) noexcept {
			auto& search = *static_cast<Search*>(a_context);
			if (_stricmp(a_slot.library, search.library) == 0 && std::strcmp(a_slot.name, search.name) == 0)
				search.patch.address = a_slot.address;
			return true;
		}, &search) || !search.patch.address)
			return 0;
		const std::span patches{ &search.patch, 1 };
		if (!PreparePointers(patches))
			return 0;
		PublishPointers(patches);
		return reinterpret_cast<uintptr_t>(search.patch.previous);
	}
}
