#include <AdConsoleSubsystem.h>
#include <AdUtils.h>

#include <RE/B/BSInputEventUser.h>
#include <RE/B/ButtonEvent.h>
#include <RE/C/CharacterEvent.h>
#include <RE/C/Console.h>
#include <RE/I/IMenu.h>
#include <RE/M/MenuOpenCloseEvent.h>
#include <RE/U/UI.h>
#include <Scaleform/G/GFx_Movie.h>
#include <Scaleform/G/GFx_Value.h>

namespace Addictol
{
	ConsoleSubsystem::TOnChar   ConsoleSubsystem::sOrigOnChar   = nullptr;
	ConsoleSubsystem::TOnButton ConsoleSubsystem::sOrigOnButton = nullptr;

	namespace consoleSubsystemDetail
	{
		static constexpr std::size_t kMaxWalkDepth = 8;

		struct DiscoveryState
		{
			std::string firstInputCandidate;
			std::string firstOutputCandidate;
			std::size_t textFieldCount = 0;
		};

		static const char* GFxTypeName(Scaleform::GFx::Value::ValueType a_type) noexcept
		{
			using VT = Scaleform::GFx::Value::ValueType;
			switch (a_type) {
			case VT::kUndefined:     return "undefined";
			case VT::kNull:          return "null";
			case VT::kBoolean:       return "bool";
			case VT::kInt:           return "int";
			case VT::kUInt:          return "uint";
			case VT::kNumber:        return "number";
			case VT::kString:        return "string";
			case VT::kStringW:       return "stringW";
			case VT::kObject:        return "object";
			case VT::kArray:         return "array";
			case VT::kDisplayObject: return "displayObject";
			default:                 return "?";
			}
		}

		static bool IsTextFieldLikeName(std::string_view a_name) noexcept
		{
			if (a_name.size() >= 4) {
				auto suffix = a_name.substr(a_name.size() - 4);
				if (suffix == "_txt" || suffix == "_tf")
					return true;
			}
			return a_name.find("Text")  != std::string_view::npos
			    || a_name.find("Input") != std::string_view::npos
			    || a_name.find("text")  != std::string_view::npos
			    || a_name.find("input") != std::string_view::npos;
		}

		static void WalkMember(
			DiscoveryState& a_state,
			const Scaleform::GFx::Value& a_value,
			std::string_view a_path,
			std::size_t a_depth) noexcept
		{
			if (a_depth >= kMaxWalkDepth || !a_value.IsObject()) {
				return;
			}

			a_value.VisitMembers([&](const char* a_name, const Scaleform::GFx::Value& a_member) {
				if (!a_name || a_name[0] == '\0') return;

				std::string childPath;
				childPath.reserve(a_path.size() + std::strlen(a_name) + 1);
				childPath.append(a_path).append(".").append(a_name);

				const auto type = a_member.GetType();
				const char* typeName = GFxTypeName(type);
				REX::INFO("ConsoleSubsystem: GFx {} ({})"sv, childPath, typeName);

				if (a_member.IsDisplayObject() && IsTextFieldLikeName(a_name)) {
					++a_state.textFieldCount;
					if (a_state.firstInputCandidate.empty()) {
						a_state.firstInputCandidate = childPath;
					} else if (a_state.firstOutputCandidate.empty()) {
						a_state.firstOutputCandidate = childPath;
					}
				}

				if (a_member.IsObject()) {
					WalkMember(a_state, a_member, childPath, a_depth + 1);
				}
			});
		}
	}

	namespace
	{
		// SEH-only shims. These must not contain locals with non-trivial dtors
		// (MSVC C2712).
		bool SafeInvokeCharCallback(
			const ConsoleSubsystem::CharCallback* a_cb,
			RE::BSInputEventUser* a_self,
			const RE::CharacterEvent* a_event,
			bool* a_outSwallowed) noexcept
		{
			*a_outSwallowed = false;
			__try {
				if (*a_cb && (*a_cb)(a_self, a_event)) {
					*a_outSwallowed = true;
				}
				return true;
			}
			__except (1) {
				return false;
			}
		}

		bool SafeInvokeButtonCallback(
			const ConsoleSubsystem::ButtonCallback* a_cb,
			RE::BSInputEventUser* a_self,
			const RE::ButtonEvent* a_event,
			bool* a_outSwallowed) noexcept
		{
			*a_outSwallowed = false;
			__try {
				if (*a_cb && (*a_cb)(a_self, a_event)) {
					*a_outSwallowed = true;
				}
				return true;
			}
			__except (1) {
				return false;
			}
		}

		bool SafeInvokeOpenCloseCallback(
			const ConsoleSubsystem::OpenCloseCallback* a_cb,
			bool a_opening) noexcept
		{
			__try {
				if (*a_cb) (*a_cb)(a_opening);
				return true;
			}
			__except (1) {
				return false;
			}
		}

		bool SafeWalkConsoleMenu(
			consoleSubsystemDetail::DiscoveryState* a_state,
			const Scaleform::GFx::Value* a_menuObj,
			const char* a_rootName) noexcept
		{
			__try {
				if (!a_menuObj->IsObject()) return false;
				consoleSubsystemDetail::WalkMember(*a_state, *a_menuObj, a_rootName, 0);
				return true;
			}
			__except (1) {
				return false;
			}
		}
	}

	void ConsoleSubsystem::HookOnCharacterEvent(RE::BSInputEventUser* a_self, const RE::CharacterEvent* a_event) noexcept
	{
		auto* self = ConsoleSubsystem::GetSingleton();

		std::vector<CharCallback> snapshot;
		{
			std::lock_guard lk{ self->callbackMutex };
			snapshot = self->charCallbacks;
		}

		for (auto& cb : snapshot) {
			bool swallowed = false;
			if (!SafeInvokeCharCallback(&cb, a_self, a_event, &swallowed)) {
				continue;
			}
			if (swallowed) return;
		}

		if (sOrigOnChar) {
			sOrigOnChar(a_self, a_event);
		}
	}

	void ConsoleSubsystem::HookOnButtonEvent(RE::BSInputEventUser* a_self, const RE::ButtonEvent* a_event) noexcept
	{
		auto* self = ConsoleSubsystem::GetSingleton();

		std::vector<ButtonCallback> snapshot;
		{
			std::lock_guard lk{ self->callbackMutex };
			snapshot = self->buttonCallbacks;
		}

		for (auto& cb : snapshot) {
			bool swallowed = false;
			if (!SafeInvokeButtonCallback(&cb, a_self, a_event, &swallowed)) {
				continue;
			}
			if (swallowed) return;
		}

		if (sOrigOnButton) {
			sOrigOnButton(a_self, a_event);
		}
	}

	bool ConsoleSubsystem::InstallVTableHooks() noexcept
	{
		if (vtableHooksInstalled) {
			return true;
		}

		// Console vtable subobject [1] is the BSInputEventUser branch (offset +0x10 in Console).
		// Slots 0x07 = OnCharacterEvent, 0x08 = OnButtonEvent per BSInputEventUser.h:31-32.
		const auto vtable = RE::VTABLE::Console[1].address();
		if (vtable == 0) {
			REX::ERROR("ConsoleSubsystem: RE::VTABLE::Console[1] resolved to 0"sv);
			return false;
		}

		sOrigOnChar = reinterpret_cast<TOnChar>(
			RELEX::DetourVTable(vtable, reinterpret_cast<std::uintptr_t>(&HookOnCharacterEvent), 0x07));
		sOrigOnButton = reinterpret_cast<TOnButton>(
			RELEX::DetourVTable(vtable, reinterpret_cast<std::uintptr_t>(&HookOnButtonEvent), 0x08));

		if (!sOrigOnChar || !sOrigOnButton) {
			REX::ERROR("ConsoleSubsystem: vtable patch failed (origChar={}, origButton={})"sv,
				reinterpret_cast<void*>(sOrigOnChar), reinterpret_cast<void*>(sOrigOnButton));
			return false;
		}

		REX::INFO("ConsoleSubsystem: hooked Console vtable[1] @ 0x{:X} slots 0x07/0x08"sv,
			static_cast<std::uint64_t>(vtable));
		vtableHooksInstalled = true;
		return true;
	}

	bool ConsoleSubsystem::RegisterMenuSink() noexcept
	{
		if (menuSinkRegistered) {
			return true;
		}

		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			REX::WARN("ConsoleSubsystem: UI singleton null, deferring sink registration"sv);
			return false;
		}

		ui->RE::BSTEventSource<RE::MenuOpenCloseEvent>::RegisterSink(&menuSink);
		menuSinkRegistered = true;
		REX::INFO("ConsoleSubsystem: registered MenuOpenCloseEvent sink"sv);
		return true;
	}

	void ConsoleSubsystem::AddCharCallback(CharCallback a_cb) noexcept
	{
		std::lock_guard lk{ callbackMutex };
		charCallbacks.emplace_back(std::move(a_cb));
	}

	void ConsoleSubsystem::AddButtonCallback(ButtonCallback a_cb) noexcept
	{
		std::lock_guard lk{ callbackMutex };
		buttonCallbacks.emplace_back(std::move(a_cb));
	}

	void ConsoleSubsystem::AddOpenCloseCallback(OpenCloseCallback a_cb) noexcept
	{
		std::lock_guard lk{ callbackMutex };
		openCloseCallbacks.emplace_back(std::move(a_cb));
	}

	std::string ConsoleSubsystem::GetInputFieldPath() const noexcept
	{
		std::lock_guard lk{ pathsMutex };
		return inputFieldPath;
	}

	std::string ConsoleSubsystem::GetOutputFieldPath() const noexcept
	{
		std::lock_guard lk{ pathsMutex };
		return outputFieldPath;
	}

	bool ConsoleSubsystem::HasDiscoveredFields() const noexcept
	{
		std::lock_guard lk{ pathsMutex };
		return discoveredFields;
	}

	void ConsoleSubsystem::DiscoverFields() noexcept
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return;
		}

		auto consoleMenu = ui->GetMenu<RE::Console>();
		if (!consoleMenu) {
			REX::WARN("ConsoleSubsystem: discovery skipped - Console menu not on stack"sv);
			return;
		}

		REX::INFO("ConsoleSubsystem: discovering Console TextFields (recursive walk, max depth {})"sv,
			consoleSubsystemDetail::kMaxWalkDepth);

		consoleSubsystemDetail::DiscoveryState state;
		if (!SafeWalkConsoleMenu(&state, &consoleMenu->menuObj, "menuObj")) {
			REX::ERROR("ConsoleSubsystem: discovery walk threw SEH; aborted"sv);
			return;
		}

		{
			std::lock_guard lk{ pathsMutex };
			inputFieldPath   = std::move(state.firstInputCandidate);
			outputFieldPath  = std::move(state.firstOutputCandidate);
			discoveredFields = !inputFieldPath.empty();
		}

		REX::INFO("ConsoleSubsystem: discovery complete - {} TextField-like members. input='{}' output='{}'"sv,
			state.textFieldCount,
			inputFieldPath.empty() ? "(none)" : inputFieldPath,
			outputFieldPath.empty() ? "(none)" : outputFieldPath);
	}

	RE::BSEventNotifyControl ConsoleSubsystem::MenuSink::ProcessEvent(
		const RE::MenuOpenCloseEvent& a_event,
		[[maybe_unused]] RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_source)
	{
		if (a_event.menuName != RE::Console::MENU_NAME) {
			return RE::BSEventNotifyControl::kContinue;
		}

		auto* self = ConsoleSubsystem::GetSingleton();
		REX::INFO("ConsoleSubsystem: Console menu {}"sv, a_event.opening ? "opening" : "closing");

		if (a_event.opening) {
			// Re-walk every open in case the user installed a Console.swf
			// replacement between sessions.
			self->DiscoverFields();
		}

		std::vector<OpenCloseCallback> snapshot;
		{
			std::lock_guard lk{ self->callbackMutex };
			snapshot = self->openCloseCallbacks;
		}

		for (auto& cb : snapshot) {
			SafeInvokeOpenCloseCallback(&cb, a_event.opening);
		}

		return RE::BSEventNotifyControl::kContinue;
	}
}
