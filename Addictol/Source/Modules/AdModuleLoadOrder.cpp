#include <Modules/AdModuleLoadOrder.h>
#include <AdUtils.h>

#include <RE/T/TESDataHandler.h>
#include <RE/T/TESFile.h>

#include <Windows.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesLoadOrder{ "Fixes"sv, "bLoadOrder"sv, true };

	namespace loadOrderDetail
	{
		struct Entry
		{
			const char* 	fileName;		// 00
			std::byte 		unk08[0x98];	// 08
			std::uint32_t 	index;			// A0
			std::byte 		unkA4[0x15];	// A4
			bool 			checked;		// B9
			bool 			invalid;		// BA
		};

		struct ModManagerSetChecked
		{
			static std::uintptr_t thunk(Entry* a_entry, bool a_checked)
			{
				if (!a_checked)
				{
					RE::TESDataHandler* dataHandler = RE::TESDataHandler::GetSingleton();
					const RE::TESFile* file = dataHandler && a_entry->fileName ? dataHandler->LookupModByName(a_entry->fileName) : nullptr;

					if (file && file->IsActive())
						a_checked = true;
				}

				return func(a_entry, a_checked);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct TESFileSetChecked
		{
			static void thunk(RE::TESFile* a_self, bool a_checked)
			{
				if (!a_checked && a_self->IsActive())
					a_checked = true;
				
				return func(a_self, a_checked);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		static BOOL WINAPI SetFileAttributes(LPCSTR a_fileName, DWORD a_fileAttributes) noexcept
		{
			const auto currentAttributes = ::GetFileAttributesA(a_fileName);
			if (currentAttributes != INVALID_FILE_ATTRIBUTES &&
				(currentAttributes & FILE_ATTRIBUTE_READONLY) != 0 &&
				(a_fileAttributes & FILE_ATTRIBUTE_READONLY) == 0)
				return TRUE;

			return ::SetFileAttributesA(a_fileName, a_fileAttributes);
		}

		struct ModManagerValidateDependencies
		{
			static bool thunk(void* a_modManager, const char* a_fileName)
			{
				if (func(a_modManager, a_fileName))
					return true;

				RE::TESDataHandler* dataHandler = RE::TESDataHandler::GetSingleton();
				RE::TESFile* file = dataHandler && a_fileName ? const_cast<RE::TESFile*>(dataHandler->LookupModByName(a_fileName)) : nullptr;

				if (!file)
					return false;

				for (auto* masterName : file->masters)
				{
					if (!masterName)
						continue;

					const auto master = dataHandler->LookupModByName(masterName);
					if (!master || (!master->flags.any(RE::TESFile::RecordFlag::kChecked) && !master->IsActive()))
						return false;
				}

				return true;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	ModuleLoadOrder::ModuleLoadOrder() :
		Module("Load Order", &bFixesLoadOrder)
	{}

	bool ModuleLoadOrder::DoQuery() const noexcept
	{
		return RELEX::IsRuntimeAE();
	}

	bool ModuleLoadOrder::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		auto& trampoline = REL::GetTrampoline();

		// SetChecked Patches
		loadOrderDetail::ModManagerSetChecked::func = trampoline.write_call<5>(REL::Relocation<std::uintptr_t>{ REL::ID{ 4487533 }, REL::Offset{ 0xA9 } }.address(), loadOrderDetail::ModManagerSetChecked::thunk);
		loadOrderDetail::TESFileSetChecked::func = trampoline.write_call<5>(REL::Relocation<std::uintptr_t>{ REL::ID{ 4487533 }, REL::Offset{ 0xB8 } }.address(), loadOrderDetail::TESFileSetChecked::thunk);

		// SetFileAttributes Patches
		trampoline.write_call<6>(REL::Relocation<std::uintptr_t>{ REL::ID{ 4476764 }, REL::Offset{ 0x74 } }.address(), loadOrderDetail::SetFileAttributes);
		trampoline.write_call<6>(REL::Relocation<std::uintptr_t>{ REL::ID{ 2189106 }, REL::Offset{ 0x92 } }.address(), loadOrderDetail::SetFileAttributes);

		// Load Order Index Patch
		RELEX::WriteSafe(REL::Relocation<std::uintptr_t>{ REL::ID{ 4487642 }, REL::Offset{ 0xC6 } }.address(), { 0xEB, 0x1A, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 });

		// Validate Dependencies Patch
		loadOrderDetail::ModManagerValidateDependencies::func = trampoline.write_call<5>(REL::Relocation<std::uintptr_t>{ REL::ID{ 4487642 }, REL::Offset{ 0x221 } }.address(), loadOrderDetail::ModManagerValidateDependencies::thunk);

		return true;
	}

	bool ModuleLoadOrder::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleLoadOrder::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}
