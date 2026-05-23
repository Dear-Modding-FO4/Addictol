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
	// ConsoleSubsystem - shared substrate for the Console AIO feature modules
	// (paste, autocomplete, history, font). One vtable patch on Console[1]
	// slots 0x07 (OnCharacterEvent) / 0x08 (OnButtonEvent) feeds three
	// independent feature modules via callback dispatcher.
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

		// Feature-module callback registration. Returning true from a callback
		// swallows the event (original vtable function is NOT called).
		void AddCharCallback(CharCallback a_cb) noexcept;
		void AddButtonCallback(ButtonCallback a_cb) noexcept;
		void AddOpenCloseCallback(OpenCloseCallback a_cb) noexcept;

		// Discovered TextField paths (empty until first Console open + walk).
		[[nodiscard]] std::string GetInputFieldPath() const noexcept;
		[[nodiscard]] std::string GetOutputFieldPath() const noexcept;
		[[nodiscard]] bool HasDiscoveredFields() const noexcept;

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

		// First-open discovery: walks Console.menuObj recursively, logs every
		// member with type, caches the first input TextField path.
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
