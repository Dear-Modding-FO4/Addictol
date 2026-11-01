#include <Modules/PapyrusLeaks/AdModulePapyrusLeaks.h>
#include <Modules/PapyrusLeaks/AdInventoryHandles.h>
#include <Modules/PapyrusLeaks/AdNativeWrappers.h>
#include <Core/AdUtils.h>

#include <RE/B/BSScript_Internal_VirtualMachine.h>
#include <RE/G/GameScript.h>
#include <RE/T/TESLoadGameEvent.h>

namespace Addictol
{
	namespace papyrusLeaksDetail
	{
		template<PapyrusHandleKind... Kinds>
		struct HandleKinds
		{
			template<PapyrusHandleKind K>
			static inline bool installed{ false };

			// a kind passed its site checks but could not be hooked
			static inline bool failed{ false };

			[[nodiscard]] static bool Install() noexcept
			{
				return (InstallKind<Kinds>() | ...);
			}

			static void RegisterSinks() noexcept
			{
				(RegisterSinksOf<Kinds>(), ...);
			}

			// Saves keep scripts that leaked before this fix; drop them once per load
			static void ReleaseOrphans() noexcept
			{
				if (!((SweepsOrphans<Kinds> && installed<Kinds>) || ...))
					return;

				const auto gameVM = RE::GameVM::GetSingleton();
				const auto vm = gameVM ? static_cast<RE::BSScript::Internal::VirtualMachine*>(gameVM->GetVM().get()) : nullptr;
				if (!vm)
					return;

				std::vector<uint64_t> handles;
				{
					RE::BSAutoLock lock{ vm->attachedScriptsLock };
					for (const auto& [handle, scripts] : vm->attachedScripts)
					{
						if (((SweepsOrphans<Kinds> && installed<Kinds> && (handle >> 48) == Kinds::kKind) || ...))
							handles.push_back(handle);
					}
				}

				(ReleaseOrphansOf<Kinds>(handles), ...);
			}

		private:
			template<PapyrusHandleKind K>
			[[nodiscard]] static bool InstallKind() noexcept
			{
				if (const auto reason = K::CheckSites(); !reason.empty())
				{
					REX::WARN("Papyrus Leaks: {} skipped ({})"sv, K::kName, reason);
					return false;
				}

				installed<K> = K::Install();
				if (!installed<K>)
				{
					REX::ERROR("Papyrus Leaks: {} failed to install"sv, K::kName);
					failed = true;
				}
				return installed<K>;
			}

			template<PapyrusHandleKind K>
			static void RegisterSinksOf() noexcept
			{
				if constexpr (RegistersSinks<K>)
				{
					if (installed<K>)
						K::RegisterSinks();
				}
			}

			template<PapyrusHandleKind K>
			static void ReleaseOrphansOf([[maybe_unused]] const std::vector<uint64_t>& a_handles) noexcept
			{
				if constexpr (SweepsOrphans<K>)
				{
					size_t released = 0;
					for (const auto handle : a_handles)
					{
						if ((handle >> 48) == K::kKind && K::IsOrphan(handle))
						{
							K::Release(handle);
							++released;
						}
					}

					if (released)
						REX::INFO("Papyrus Leaks: released {} orphaned {} script handles"sv, released, K::kName);
				}
			}
		};

		using Kinds = HandleKinds<PapyrusLeaks::InventoryHandles, PapyrusLeaks::NativeWrappers>;

		// Sent after GameVM::EndSaveLoad; kPostLoadGame precedes it
		class LoadGameHandler :
			public RE::BSTEventSink<RE::TESLoadGameEvent>
		{
		public:
			[[nodiscard]] static LoadGameHandler* GetSingleton() noexcept
			{
				static LoadGameHandler singleton;
				return std::addressof(singleton);
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESLoadGameEvent&, RE::BSTEventSource<RE::TESLoadGameEvent>*) override
			{
				Kinds::ReleaseOrphans();
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		static inline bool s_anyInstalled{ false };
	}

	ModulePapyrusLeaks::ModulePapyrusLeaks() :
		Module("Papyrus Leaks", &bFixesPapyrusLeaks)
	{}

	bool ModulePapyrusLeaks::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace papyrusLeaksDetail;

		if (a_msg)
		{
			if (!s_anyInstalled)
			{
				Skip("no Papyrus handle kind is installed"sv);
				return false;
			}
			if (a_msg->type == F4SE::MessagingInterface::kGameDataReady)
			{
				Kinds::RegisterSinks();
				RE::TESLoadGameEvent::GetEventSource()->RegisterSink(LoadGameHandler::GetSingleton());
			}
			return true;
		}

		s_anyInstalled = Kinds::Install();
		if (!s_anyInstalled && !Kinds::failed)
			Skip("no Papyrus handle kind could be patched"sv);
		return s_anyInstalled;
	}
}
