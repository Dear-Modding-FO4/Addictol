#include <Modules/AdModuleSaveLoadTiming.h>

#include <Core/AdUtils.h>

#include <RE/L/LoadingMenu.h>
#include <RE/M/MenuOpenCloseEvent.h>
#include <RE/U/UI.h>

namespace Addictol
{
	namespace
	{
		using Prologue = std::array<uint8_t, 8>;

		struct SaveStep
		{
			REL::VariantID function;
			// Indexed by LoadTiming::Runtime.
			std::array<Prologue, 3> prologue;
		};

		// Same order as LoadTiming::kSaveStepNames.
		constexpr std::array kSaveSteps{
			SaveStep{ RE::ID::BGSSaveLoadGlobalData::LoadGlobalData, { {
				{ 0x85, 0xD2, 0x0F, 0x84, 0x59, 0x01, 0x00, 0x00 },
				{ 0x85, 0xD2, 0x0F, 0x84, 0x5A, 0x01, 0x00, 0x00 },
				{ 0x85, 0xD2, 0x0F, 0x84, 0x5A, 0x01, 0x00, 0x00 } } } },
			SaveStep{ RE::ID::GameScript::GameVM::LoadGame, { {
				{ 0x41, 0x54, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x68 },
				{ 0x41, 0x54, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x68 },
				{ 0x41, 0x54, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x68 } } } },
			SaveStep{ RE::ID::BGSSaveLoadGame::InitForms, { {
				{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C },
				{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C },
				{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C } } } },
			SaveStep{ RE::ID::BGSSaveLoadManager::EndLoadGame, { {
				{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57 },
				{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57 },
				{ 0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x53, 0x56 } } } }
		};
		static_assert(kSaveSteps.size() == LoadTiming::kSaveStepNames.size());

		template <size_t I>
		[[nodiscard]] LoadTiming::ForwardedCall DetourStep(LoadTiming::Runtime a_runtime) noexcept
		{
			const auto address = kSaveSteps[I].function.address();
			if (!LoadTiming::ValidateSignature(address, kSaveSteps[I].prologue[std::to_underlying(a_runtime)]))
			{
				REX::WARN("Save Load Timing: {} {} prologue mismatch; that step is not timed"sv,
					LoadTiming::RuntimeName(a_runtime), LoadTiming::kSaveStepNames[I]);
				return nullptr;
			}
			const auto original = RELEX::DetourJump(
				address,
				reinterpret_cast<uintptr_t>(&LoadTiming::TimedForward<ModuleSaveLoadTiming, I>));
			if (!original)
				REX::WARN("Save Load Timing: {} detour failed; that step is not timed"sv, LoadTiming::kSaveStepNames[I]);
			return reinterpret_cast<LoadTiming::ForwardedCall>(original);
		}

		class LoadingMenuCloseSink :
			public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			explicit LoadingMenuCloseSink(ModuleSaveLoadTiming& a_owner) noexcept :
				m_owner(a_owner)
			{}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::MenuOpenCloseEvent& a_event,
				RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!a_event.opening && a_event.menuName == RE::LoadingMenu::MENU_NAME)
					m_owner.OnLoadingMenuClosed();
				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			ModuleSaveLoadTiming& m_owner;
		};
	}

	ModuleSaveLoadTiming::ModuleSaveLoadTiming() :
		Module(
			"Save Load Timing",
			&bTelemetrySaveLoadTiming,
			{ F4SE::MessagingInterface::kPreLoadGame, F4SE::MessagingInterface::kPostLoadGame }),
		SaveLoadTimingSource(LoadTiming::kSaveMetricSchema)
	{
		s_instance = this;
	}

	bool ModuleSaveLoadTiming::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		const auto runtime = LoadTiming::CurrentRuntime();
		[&]<size_t... I>(std::index_sequence<I...>) {
			((s_timedOriginals[I] = DetourStep<I>(runtime)), ...);
		}(std::make_index_sequence<kSaveSteps.size()>{});

		Activate();
		m_installed = true;
		return true;
	}

	bool ModuleSaveLoadTiming::DoListener(F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// Listeners run even when installation was skipped.
		if (!a_msg || !m_installed)
			return true;
		if (a_msg->type == F4SE::MessagingInterface::kPreLoadGame)
		{
			m_preLoadQpc.store(Addictol::ReadQpc());
			if (!m_sinkRegistered)
			{
				if (const auto ui = RE::UI::GetSingleton())
				{
					static LoadingMenuCloseSink sink{ *this };
					ui->RegisterSink<RE::MenuOpenCloseEvent>(&sink);
					m_sinkRegistered = true;
				}
			}
		}
		else if (a_msg->type == F4SE::MessagingInterface::kPostLoadGame)
			RecordSpan(LoadTiming::kSaveSeriesName, LoadTiming::kPreLoadToPostLoadBucket, m_preLoadQpc.load(), Addictol::ReadQpc());
		return true;
	}

	void ModuleSaveLoadTiming::OnLoadingMenuClosed() noexcept
	{
		// One span per load: the start is consumed here.
		const auto start = m_preLoadQpc.exchange(0);
		RecordSpan(LoadTiming::kSaveSeriesName, LoadTiming::kPreLoadToLoadingClosedBucket, start, Addictol::ReadQpc());
	}

	void ModuleSaveLoadTiming::EndTimedCall(size_t a_index, uint64_t a_start, uint64_t a_end) noexcept
	{
		// Cells and quests reuse the other calls outside a save load.
		constexpr size_t kEndLoadGame{ LoadTiming::kSaveStepNames.size() - 1 };
		if (s_instance && (a_index == kEndLoadGame || s_instance->m_preLoadQpc.load()))
			s_instance->RecordSpan(LoadTiming::kSaveStepSeriesName, LoadTiming::kSaveStepNames[a_index], a_start, a_end);
	}

	void ModuleSaveLoadTiming::RecordSpan(
		std::string_view a_series,
		std::string_view a_bucket,
		uint64_t a_from,
		uint64_t a_to) noexcept
	{
		if (!a_from || a_to < a_from)
			return;
		const std::scoped_lock lock{ m_recordMutex };
		(void)Record(a_series, a_bucket, a_to - a_from);
	}
}
