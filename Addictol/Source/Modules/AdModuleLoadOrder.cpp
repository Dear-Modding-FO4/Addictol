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

		// Targets
		const auto targetSetFileAttributes1 	= REL::Relocation{ REL::ID{ 4476764 }, REL::Offset{ 0x074 } }.address();
		const auto targetSetFileAttributes2 	= REL::Relocation{ REL::ID{ 2189106 }, REL::Offset{ 0x092 } }.address();
		const auto targetSetChecked1 			= REL::Relocation{ REL::ID{ 4487533 }, REL::Offset{ 0x0A9 } }.address();
		const auto targetSetChecked2 			= REL::Relocation{ REL::ID{ 4487533 }, REL::Offset{ 0x0B8 } }.address();
		const auto targetValidateDependencies 	= REL::Relocation{ REL::ID{ 4487642 }, REL::Offset{ 0x221 } }.address();
		const auto targetLoadOrder1				= REL::Relocation{ REL::ID{ 4487632 }, REL::Offset{ 0x28D } }.address();
		const auto targetLoadOrder2				= REL::Relocation{ REL::ID{ 4487642 }, REL::Offset{ 0x0C6 } }.address();
		const auto targetLoadOrder3				= REL::Relocation{ REL::ID{ 4487642 }, REL::Offset{ 0x151 } }.address();

		// Validate
		if (!RELEX::Validate(targetSetFileAttributes1,		{ 0xFF, 0x15 })				||
			!RELEX::Validate(targetSetFileAttributes2, 		{ 0xFF, 0x15 })				||
			!RELEX::Validate(targetSetChecked1,				{ 0xE8 })					||
			!RELEX::Validate(targetSetChecked2,				{ 0xE8 })					||
			!RELEX::Validate(targetValidateDependencies,	{ 0xE8 })					||
			!RELEX::Validate(targetLoadOrder1,				{ 0x74, 0x2F })				||
			!RELEX::Validate(targetLoadOrder2,				{ 0xE8 })					||
			!RELEX::Validate(targetLoadOrder3,				{ 0x85, 0xF6, 0x74, 0x29 }))
			return false;

		// SetFileAttributes Patches
		RELEX::DetourClassCall(targetSetFileAttributes1, &loadOrderDetail::SetFileAttributes);
		RELEX::DetourClassCall(targetSetFileAttributes2, &loadOrderDetail::SetFileAttributes);

		// SetChecked Patches
		loadOrderDetail::ModManagerSetChecked::func = RELEX::DetourClassCall(targetSetChecked1, &loadOrderDetail::ModManagerSetChecked::thunk);
		loadOrderDetail::TESFileSetChecked::func 	= RELEX::DetourClassCall(targetSetChecked2, &loadOrderDetail::TESFileSetChecked::thunk);

		// Validate Dependencies Patch
		loadOrderDetail::ModManagerValidateDependencies::func = RELEX::DetourClassCall(targetValidateDependencies, &loadOrderDetail::ModManagerValidateDependencies::thunk);

		// Preserve Load Order Patches
		RELEX::WriteSafe(targetLoadOrder1, { 0x90, 0x90 });
		RELEX::WriteSafe(targetLoadOrder2, { 0xEB, 0x1A, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 });
		RELEX::WriteSafe(targetLoadOrder3, { 0xEB, 0x2B, 0x90, 0x90 });

		// Validate the Funcs
		return 	loadOrderDetail::ModManagerSetChecked::func != 0 &&
				loadOrderDetail::TESFileSetChecked::func != 0 &&
				loadOrderDetail::ModManagerValidateDependencies::func != 0;
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
