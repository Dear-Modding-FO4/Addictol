#include <Modules\AdModuleMaxPapyrusOps.h>
#include <AdUtils.h>

#include <RE/B/BSScript_Internal_IVMSaveLoadInterface.h>
#include <RE/B/BSScript_SimpleAllocMemoryPagePolicy.h>
#include <RE/G/GameScript.h>
#include <RE/S/Script.h>

#include <xbyak/xbyak.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesBakaMaxPapyrusOps{ "Fixes"sv, "bBakaMaxPapyrusOps"sv, true };
	static REX::TOML::I32<> nAdditionalMaxPapyrusOpsPerFrame{ "Additional"sv, "nMaxPapyrusOpsPerFrame"sv, 500 };

	class hkGetLargestAvailablePage :
		public REX::Singleton<hkGetLargestAvailablePage>
	{
	public:
		static void Install()
		{
			static REL::Relocation<std::uintptr_t> target{ RE::BSScript::SimpleAllocMemoryPagePolicy::VTABLE[0] };
			_GetLargestAvailablePage = target.write_vfunc(0x04, GetLargestAvailablePage);
		}

	private:
		static RE::BSScript::SimpleAllocMemoryPagePolicy::AllocationStatus GetLargestAvailablePage(RE::BSScript::SimpleAllocMemoryPagePolicy* a_this, RE::BSTSmartPointer<RE::BSScript::MemoryPage, RE::BSTSmartPointerAutoPtr>& a_newPage)
		{
			const RE::BSAutoLock lock{ a_this->dataLock };

			auto maxPageSize = a_this->maxAllocatedMemory - a_this->currentMemorySize;
			auto currentMemorySize = a_this->currentMemorySize;
			if (maxPageSize < 0)
			{
				a_this->currentMemorySize = a_this->maxAllocatedMemory;
			}

			auto result = _GetLargestAvailablePage(a_this, a_newPage);
			if (maxPageSize < 0)
			{
				a_this->currentMemorySize = currentMemorySize;
			}

			return result;
		}

		inline static REL::Relocation<decltype(&GetLargestAvailablePage)> _GetLargestAvailablePage;
	};

	class hkEndSaveLoad :
		public REX::Singleton<hkEndSaveLoad>
	{
	public:
		static void Install()
		{
			auto& trampoline = REL::GetTrampoline();

			if (RELEX::IsRuntimeOG())
			{
				trampoline.write_call<5>(REL::Relocation{ REL::ID{ 1321787 }, REL::Offset{ 0x05A } }.address(), EndSaveLoad);
				trampoline.write_call<5>(REL::Relocation{ REL::ID{ 1014572 }, REL::Offset{ 0x5CF } }.address(), EndSaveLoad);
				trampoline.write_call<5>(REL::Relocation{ REL::ID{ 124452  }, REL::Offset{ 0x32F } }.address(), EndSaveLoad);
				trampoline.write_call<5>(REL::Relocation{ REL::ID{ 371005  }, REL::Offset{ 0x478 } }.address(), EndSaveLoad);
			}
			else
			{
				trampoline.write_call<5>(REL::Relocation{ REL::ID{ 2228095 }, REL::Offset{ 0x0, 0x030, 0x030 } }.address(), EndSaveLoad);
				trampoline.write_call<5>(REL::Relocation{ REL::ID{ 2228097 }, REL::Offset{ 0x0, 0x69E, 0x6A3 } }.address(), EndSaveLoad);
				trampoline.write_call<5>(REL::Relocation{ REL::ID{ 2228945 }, REL::Offset{ 0x0, 0x30B, 0x310 } }.address(), EndSaveLoad);
			}
		}

	private:
		static void EndSaveLoad(RE::GameVM* a_this)
		{
			if (a_this->saveLoadInterface)
			{
				a_this->saveLoadInterface->CleanupLoad();
				a_this->saveLoadInterface->CleanupSave();
			}

			a_this->RegisterForAllGameEvents();
			a_this->saveLoad = false;

			a_this->handlePolicy.DropSaveLoadRemapData();
			a_this->objectBindPolicy.EndSaveLoad();
			a_this->handlePolicy.UpdatePersistence();

			if (RE::Script::GetProcessScripts())
			{
				if (RELEX::IsRuntimeAE())
				{
					const RE::BSAutoLock lock{ a_this->freezeLock };
					a_this->frozen = false;
				}
				else
				{
					const RE::BSAutoLock lock{ *reinterpret_cast<RE::BSSpinLock*>(reinterpret_cast<std::byte*>(a_this) + 0x5F8) };
					*reinterpret_cast<bool*>(reinterpret_cast<std::byte*>(a_this) + 0x600) = false;
				}
			}
		}
	};

	class hkLock :
		public REX::Singleton<hkLock>
	{
	public:
		static void Install()
		{
			auto& trampoline = REL::GetTrampoline();

			// Unnecessary on OG
			if (!RELEX::IsRuntimeOG())
			{
				trampoline.write_call<5>(REL::Relocation{ REL::ID{ 2251339 }, REL::Offset{ 0x797 } }.address(), Lock);
				static REL::Relocation<std::uintptr_t> target{ REL::ID{ 2251339 }, static_cast<std::ptrdiff_t>(REL::Offset{ 0x79C }.offset()) };
				target.write_fill(REL::NOP, 0x08);
			}
		}

	private:
		static void Lock(void*, void*)
		{
			auto GameVM = RE::GameVM::GetSingleton();
			if (!GameVM)
			{
				return;
			}

			if (RE::Script::GetProcessScripts())
			{
				const RE::BSAutoLock lock{ GameVM->freezeLock };
				GameVM->frozen = false;
			}
		}
	};

	class hkFreeze :
		public REX::Singleton<hkFreeze>
	{
	public:
		static void Install()
		{
			auto& trampoline = REL::GetTrampoline();

			if (RELEX::IsRuntimeOG())
			{
				_Freeze = trampoline.write_call<5>(REL::Relocation{ REL::ID{ 1238017 }, REL::Offset{ 0x370 } }.address(), Freeze);
			}
			else
			{
				_Freeze = trampoline.write_call<5>(REL::Relocation{ REL::ID{ 2251333 }, REL::Offset{ 0x119 } }.address(), Freeze);
				trampoline.write_call<5>(REL::Relocation{ REL::ID{ 2251336 }, REL::Offset{ 0x370 } }.address(), Freeze);
				trampoline.write_call<5>(REL::Relocation{ REL::ID{ 2251364 }, REL::Offset{ 0x382 } }.address(), Freeze);
			}
		}

	private:
		static void Freeze(RE::GameVM* a_this, bool a_freeze)
		{
			if (RE::Script::GetProcessScripts())
			{
				return _Freeze(a_this, a_freeze);
			}
		}

		inline static REL::Relocation<decltype(&Freeze)> _Freeze;
	};

	class hkMaxPapyrusOpsPerFrame
	{
	public:
		static void Install()
		{
			// Needs some OG changes
			struct PapyrusOpsPerFrame : Xbyak::CodeGenerator
			{
				PapyrusOpsPerFrame(std::uintptr_t a_begin, std::uintptr_t a_end)
				{
					if (RELEX::IsRuntimeOG())
					{
						inc(r14d);
						cmp(r14d, MaxOpsPerFrame);
					}
					else
					{
						inc(r12d);
						mov(r8d, 0x1570);
						cmp(r12d, MaxOpsPerFrame);
					}
					jb("Loop");
					mov(rcx, a_end);
					jmp(rcx);
					L("Loop");
					mov(rcx, a_begin);
					jmp(rcx);
				}
			};

			static REL::Relocation<std::uintptr_t> target{ REL::ID{ 614585, 2315660 }, static_cast<std::ptrdiff_t>(REL::Offset{ 0x4F0, 0xA64 }.offset()) };
			static REL::Relocation<std::uintptr_t> loopin{ REL::ID{ 614585, 2315660 }, static_cast<std::ptrdiff_t>(REL::Offset{ 0x0A0 }.offset()) };
			static REL::Relocation<std::uintptr_t> loopex{ REL::ID{ 614585, 2315660 }, static_cast<std::ptrdiff_t>(REL::Offset{ 0x4FD, 0xA77 }.offset()) };

			auto code = PapyrusOpsPerFrame(loopin.address(), loopex.address());
			target.write_fill(REL::NOP, loopex.address() - target.address());

			auto& trampoline = REL::GetTrampoline();
			auto result = trampoline.allocate(code);
			target.write_jmp<5>(result);
		}

		template <typename T>
		static void Update(T a_value)
		{
			MaxOpsPerFrame = static_cast<std::int32_t>(a_value);
		}

	private:
		inline static std::int32_t MaxOpsPerFrame{ 100 };
	};

	ModuleMaxPapyrusOps::ModuleMaxPapyrusOps() :
		Module("MaxPapyrusOps", &bFixesBakaMaxPapyrusOps)
	{}

	bool ModuleMaxPapyrusOps::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleMaxPapyrusOps::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kPostLoad)
		{
			// FixScriptPageAllocation
			hkGetLargestAvailablePage::Install();

			// FixToggleScriptsCommand
			hkEndSaveLoad::Install();
			hkLock::Install();
			hkFreeze::Install();

			// MaxPapyrusOps
			if (nAdditionalMaxPapyrusOpsPerFrame.GetValue() > 100)
			{
				hkMaxPapyrusOpsPerFrame::Update(nAdditionalMaxPapyrusOpsPerFrame.GetValue());
				hkMaxPapyrusOpsPerFrame::Install();
			}
		}

		return true;
	}

	bool ModuleMaxPapyrusOps::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleMaxPapyrusOps::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}