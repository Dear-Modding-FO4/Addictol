#include <DearModdingUI/CarrierMenu.h>

#include <F4SE/API.h>
#include <RE/B/BSScaleformManager.h>
#include <RE/I/IMenu.h>
#include <RE/U/UI.h>
#include <RE/U/UIMessageQueue.h>
#include <REX/REX.h>

#include <atomic>
#include <mutex>
#include <string_view>

namespace Addictol::DearModdingUI::CarrierMenu
{
	namespace
	{
		using namespace std::literals;

		inline constexpr auto kMenuName = "Addictol_DearModdingUI_CursorCarrier";
		inline constexpr auto kMoviePath = "Interface/CursorMenu.swf"sv;

		class CursorCarrierMenu final :
			public RE::IMenu
		{
		public:
			CursorCarrierMenu()
			{
				auto* scaleform = RE::BSScaleformManager::GetSingleton();
				if (!scaleform || !scaleform->LoadMovieEx(*this, kMoviePath) || !uiMovie)
					return;

				uiMovie->SetVisible(false);
				menuFlags.set(
					RE::UI_MENU_FLAGS::kUsesCursor,
					RE::UI_MENU_FLAGS::kModal,
					RE::UI_MENU_FLAGS::kAdvancesUnderPauseMenu,
					RE::UI_MENU_FLAGS::kRendersUnderPauseMenu);
				inputContext = RE::UserEvents::INPUT_CONTEXT_ID::kCursor;
			}

			void AdvanceMovie(
				[[maybe_unused]] float a_interval,
				[[maybe_unused]] uint64_t a_currentTime) override
			{}

			RE::UI_MESSAGE_RESULTS ProcessMessage(
				RE::UIMessage& a_message) override
			{
				return a_message.menu == kMenuName ?
					RE::UI_MESSAGE_RESULTS::kHandled :
					RE::UI_MESSAGE_RESULTS::kPassOn;
			}

			void PreDisplay() override
			{
				if (uiMovie)
					uiMovie->SetVisible(false);
			}

			[[nodiscard]] bool Valid() const noexcept
			{
				return uiMovie != nullptr;
			}

			[[nodiscard]] static RE::IMenu* Creator(
				[[maybe_unused]] const RE::UIMessage& a_message)
			{
				auto* menu = new CursorCarrierMenu();
				if (!menu->Valid())
				{
					delete menu;
					return nullptr;
				}
				return menu;
			}
		};

		std::atomic<bool> s_registrationAttempted{ false };
		std::atomic<bool> s_registered{ false };
		std::atomic<bool> s_taskFailureLogged{ false };
		std::atomic<bool> s_queueFailureLogged{ false };
		std::mutex s_stateLock;
		State s_state{};

		[[nodiscard]] bool HasExpectedRegistration(RE::UI& a_ui) noexcept
		{
			const RE::BSFixedString name{ kMenuName };
			const RE::BSAutoReadLock lock{ RE::UI::GetMenuMapRWLock() };
			const auto entry = a_ui.menuMap.find(name);
			return entry != a_ui.menuMap.end() &&
				entry->second.create == &CursorCarrierMenu::Creator;
		}

		[[nodiscard]] bool Dispatch(Action a_action) noexcept
		{
			if (a_action == Action::kNone)
				return true;
			auto* tasks = F4SE::GetTaskInterface();
			if (!tasks)
			{
				if (!s_taskFailureLogged.exchange(true, std::memory_order_acq_rel))
					REX::ERROR("DearModdingUI: carrier menu UI task interface is unavailable"sv);
				return false;
			}

			const auto message = a_action == Action::kShow ?
				RE::UI_MESSAGE_TYPE::kShow :
				RE::UI_MESSAGE_TYPE::kHide;
			tasks->AddUITask([message] {
				auto* queue = RE::UIMessageQueue::GetSingleton();
				if (!queue)
				{
					if (!s_queueFailureLogged.exchange(true, std::memory_order_acq_rel))
						REX::ERROR("DearModdingUI: carrier menu message queue is unavailable"sv);
					return;
				}
				const RE::BSFixedString name{ kMenuName };
				queue->AddMessage(name, message);
			});
			return true;
		}
	}

	bool Register() noexcept
	{
		bool expected{ false };
		if (!s_registrationAttempted.compare_exchange_strong(
				expected, true, std::memory_order_acq_rel))
			return s_registered.load(std::memory_order_acquire);

		auto* ui = RE::UI::GetSingleton();
		if (!ui)
		{
			REX::ERROR("DearModdingUI: carrier menu registration failed because UI is unavailable"sv);
			return false;
		}
		if (HasExpectedRegistration(*ui))
		{
			s_registered.store(true, std::memory_order_release);
			return true;
		}

		{
			const RE::BSFixedString name{ kMenuName };
			const RE::BSAutoReadLock lock{ RE::UI::GetMenuMapRWLock() };
			if (ui->menuMap.find(name) != ui->menuMap.end())
			{
				REX::ERROR("DearModdingUI: carrier menu registration failed because \"{}\" is already registered"sv,
					kMenuName);
				return false;
			}
		}

		ui->RegisterMenu(kMenuName, &CursorCarrierMenu::Creator);
		const auto registered = HasExpectedRegistration(*ui);
		s_registered.store(registered, std::memory_order_release);
		if (registered)
			REX::INFO("DearModdingUI: registered carrier menu \"{}\""sv, kMenuName);
		else
			REX::ERROR("DearModdingUI: carrier menu registration was not retained by UI"sv);
		return registered;
	}

	void Handle(Event a_event) noexcept
	{
		if (!s_registered.load(std::memory_order_acquire))
			return;

		const std::lock_guard lock{ s_stateLock };
		const auto previous = s_state;
		const auto action = Transition(s_state, a_event);
		if (!Dispatch(action))
			s_state = previous;
	}
}
