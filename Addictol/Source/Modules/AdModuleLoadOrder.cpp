#include <Modules/AdModuleLoadOrder.h>
#include <Core/AdUtils.h>

#include <RE/T/TESDataHandler.h>
#include <RE/T/TESFile.h>

#include <Windows.h>

namespace Addictol
{

	namespace loadOrderDetail
	{
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

		struct DeferRefresh_Loop
		{
			static inline thread_local bool skipRefresh = false;

			static void* thunk(void* a_a1, void* a_a2, void* a_a3)
			{
				skipRefresh = true;
				const auto result = func(a_a1, a_a2, a_a3);
				skipRefresh = false;

				return result;
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DeferRefresh_Refresh
		{
			static inline thread_local bool pendingRefresh = false;

			static void thunk(void* a_a1)
			{
				if (DeferRefresh_Loop::skipRefresh)
				{
					pendingRefresh = true;
					return;
				}

				return func(a_a1);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DeferRefresh_Finish
		{
			static inline std::atomic_bool refreshScheduled = false;

			static std::uintptr_t thunk(void* a_a1)
			{
				if (DeferRefresh_Refresh::pendingRefresh)
				{
					DeferRefresh_Refresh::pendingRefresh = false;
					if (!refreshScheduled.load())
					{
						refreshScheduled.store(true);
						std::jthread([a_a1]()
						{
							std::this_thread::sleep_for(std::chrono::seconds(1));
							F4SE::GetTaskInterface()->AddTask([a_a1]()
							{
								DeferRefresh_Refresh::thunk(a_a1);
								refreshScheduled.store(false);
							});
						}).detach();
					}
				}

				return func(a_a1);
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
		// Targets
		const auto targetSetFileAttributes1 	= REL::Relocation{ REL::ID{ 4476764 }, REL::Offset{ 0x074 } }.address();
		const auto targetSetFileAttributes2 	= REL::Relocation{ REL::ID{ 2189106 }, REL::Offset{ 0x092 } }.address();
		const auto targetValidateDependencies 	= REL::Relocation{ REL::ID{ 4487642 }, REL::Offset{ 0x221 } }.address();
		const auto targetDeferLoop				= REL::Relocation{ REL::ID{ 8517260 }, REL::Offset{ 0x0AD } }.address();
		const auto targetDeferRefresh			= REL::Relocation{ REL::ID{ 4487501 }, REL::Offset{ 0x04E } }.address();
		const auto targetDeferFinish			= REL::Relocation{ REL::ID{ 8517260 }, REL::Offset{ 0x0E9 } }.address();
		const auto targetLoadOrder1				= REL::Relocation{ REL::ID{ 4487632 }, REL::Offset{ 0x28D } }.address();
		const auto targetLoadOrder2				= REL::Relocation{ REL::ID{ 4487642 }, REL::Offset{ 0x0C6 } }.address();
		const auto targetLoadOrder3				= REL::Relocation{ REL::ID{ 4487642 }, REL::Offset{ 0x151 } }.address();

		// Validate
		if (!RELEX::Validate(targetSetFileAttributes1,		{ 0xFF, 0x15 })				||
			!RELEX::Validate(targetSetFileAttributes2, 		{ 0xFF, 0x15 })				||
			!RELEX::Validate(targetValidateDependencies,	{ 0xE8 })					||
			!RELEX::Validate(targetDeferLoop,				{ 0xE8 })					||
			!RELEX::Validate(targetDeferRefresh,			{ 0xE8 })					||
			!RELEX::Validate(targetDeferFinish,				{ 0xE8 })					||
			!RELEX::Validate(targetLoadOrder1,				{ 0x74, 0x2F })				||
			!RELEX::Validate(targetLoadOrder2,				{ 0xE8 })					||
			!RELEX::Validate(targetLoadOrder2 + 5,			{ 0x84, 0xC0, 0x75 })		||
			!RELEX::Validate(targetLoadOrder3,				{ 0x85, 0xF6, 0x74, 0x29 }))
			return false;

		// SetFileAttributes Patches
		RELEX::DetourClassCall(targetSetFileAttributes1, &loadOrderDetail::SetFileAttributes);
		RELEX::DetourClassCall(targetSetFileAttributes2, &loadOrderDetail::SetFileAttributes);

		// Validate Dependencies Patch
		loadOrderDetail::ModManagerValidateDependencies::func = RELEX::DetourClassCall(targetValidateDependencies, &loadOrderDetail::ModManagerValidateDependencies::thunk);

		// Defer Refresh & Loop Optimization
		loadOrderDetail::DeferRefresh_Loop::func	= RELEX::DetourClassCall(targetDeferLoop,		&loadOrderDetail::DeferRefresh_Loop::thunk);
		loadOrderDetail::DeferRefresh_Refresh::func	= RELEX::DetourClassCall(targetDeferRefresh, 	&loadOrderDetail::DeferRefresh_Refresh::thunk);
		loadOrderDetail::DeferRefresh_Finish::func	= RELEX::DetourClassCall(targetDeferFinish,		&loadOrderDetail::DeferRefresh_Finish::thunk);

		// Preserve Load Order Patches
		RELEX::WriteSafe(targetLoadOrder1, { 0x90, 0x90 });
		RELEX::WriteSafe(targetLoadOrder2, { 0xEB, 0x1A, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 });
		RELEX::WriteSafe(targetLoadOrder3, { 0xEB, 0x2B, 0x90, 0x90 });

		// Validate the Funcs
		return 	loadOrderDetail::ModManagerValidateDependencies::func != 0 &&
				loadOrderDetail::DeferRefresh_Loop::func != 0 &&
				loadOrderDetail::DeferRefresh_Refresh::func != 0 &&
				loadOrderDetail::DeferRefresh_Finish::func != 0;
	}

}
