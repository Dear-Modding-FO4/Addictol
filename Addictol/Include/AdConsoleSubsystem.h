#pragma once

#include <REX/REX.h>
#include <F4SE/F4SE.h>
#include <RE/B/BSTEvent.h>
#include <RE/M/MenuOpenCloseEvent.h>

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace RE
{
	class BSInputEventUser;
	class ButtonEvent;
	class CharacterEvent;
}

namespace Addictol
{
	// Shared Console substrate: single Console[1] vtable patch (OnChar/OnButton) dispatched to feature modules.
	class ConsoleSubsystem :
		public REX::TSingleton<ConsoleSubsystem>
	{
	public:
		using CharCallback      = std::function<bool(RE::BSInputEventUser*, const RE::CharacterEvent*)>;
		using ButtonCallback    = std::function<bool(RE::BSInputEventUser*, const RE::ButtonEvent*)>;
		using OpenCloseCallback = std::function<void(bool a_opening)>;

		// Lifecycle. Called by ModuleConsoleSubsystem from F4SE load + kDataLoaded stages.
		bool InstallVTableHooks() noexcept;
		bool RegisterMenuSink() noexcept;

		// Returning true from a callback swallows the event (engine handler skipped).
		void AddCharCallback(CharCallback a_cb) noexcept;
		void AddButtonCallback(ButtonCallback a_cb) noexcept;
		void AddOpenCloseCallback(OpenCloseCallback a_cb) noexcept;

		// Discovered TextField paths (empty until first Console open + walk).
		[[nodiscard]] std::string GetInputFieldPath() const noexcept;
		[[nodiscard]] std::string GetOutputFieldPath() const noexcept;
		[[nodiscard]] bool HasDiscoveredFields() const noexcept;

		// UI-thread only (call from a Char/Button/OpenClose callback). UTF-8 in/out; false if console gone or paths unresolved.
		bool GetInputText(std::string& a_out) noexcept;
		bool SetInputText(std::string_view a_utf8) noexcept;
		bool GetInputSelection(std::int32_t& a_anchor, std::int32_t& a_active) noexcept;
		bool SetInputSelection(std::int32_t a_anchor, std::int32_t a_active) noexcept;
		bool ClearOutputText() noexcept;
		bool ApplyOutputFontSize(std::int32_t a_sizePx) noexcept;

	private:
		// Hooks installed on Console vtable subobject [1].
		using TOnChar   = void (*)(RE::BSInputEventUser*, const RE::CharacterEvent*);
		using TOnButton = void (*)(RE::BSInputEventUser*, const RE::ButtonEvent*);

		static void HookOnCharacterEvent(RE::BSInputEventUser* a_self, const RE::CharacterEvent* a_event) noexcept;
		static void HookOnButtonEvent(RE::BSInputEventUser* a_self, const RE::ButtonEvent* a_event) noexcept;

		static TOnChar   sOrigOnChar;
		static TOnButton sOrigOnButton;

		// MenuOpenCloseEvent sink that triggers discovery walks.
		class MenuSink :
			public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::MenuOpenCloseEvent& a_event,
				RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_source) override;
		};
		MenuSink menuSink;

		// Walks Console.menuObj on each open, logs members, caches first input/output TextField paths.
		void DiscoverFields() noexcept;

		mutable std::mutex                callbackMutex;
		std::vector<CharCallback>         charCallbacks;
		std::vector<ButtonCallback>       buttonCallbacks;
		std::vector<OpenCloseCallback>    openCloseCallbacks;

		mutable std::mutex                pathsMutex;
		std::string                       inputFieldPath;
		std::string                       outputFieldPath;
		bool                              discoveredFields{ false };

		bool                              vtableHooksInstalled{ false };
		bool                              menuSinkRegistered{ false };
	};
}
