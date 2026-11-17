#include <Modules/AdModuleInputSwitch.h>
#include <Menu/AdMenu.h>
#include <Core/AdUtils.h>
#include <Core/AdAssert.h>

#include <RE/B/BSInputDeviceManager.h>
#include <RE/B/BSInputEventReceiver.h>
#include <RE/B/BSInputEventUser.h>
#include <RE/B/BSScriptUtil.h>
#include <RE/B/BSUIMessageData.h>
#include <RE/B/ButtonEvent.h>
#include <RE/C/ControlMap.h>
#include <RE/C/CursorMenu.h>
#include <RE/F/FlatScreenModel.h>
#include <RE/I/INPUT_DEVICE.h>
#include <RE/I/InputEvent.h>
#include <RE/M/MenuControls.h>
#include <RE/M/MenuCursor.h>
#include <RE/M/MouseMoveEvent.h>
#include <RE/P/PIPBOY_PAGES.h>
#include <RE/P/PipboyManager.h>
#include <RE/P/PipboyMenu.h>
#include <RE/T/ThumbstickEvent.h>
#include <RE/U/UI.h>
#include <RE/U/UIMessageQueue.h>

#include <Core/AdXbyak.h>

namespace Addictol
{
	template <class E, class U = std::underlying_type_t<E>> class enumeration : public REX::EnumSet<E, U>
	{
		using super = REX::EnumSet<E, U>;

	public:
		using enum_type = E;
		using underlying_type = U;

		using super::super;
		using super::operator=;
		using super::operator*;
	};

	template <class... Args>
	enumeration(Args...) -> enumeration<std::common_type_t<Args...>, std::underlying_type_t<std::common_type_t<Args...>>>;

	namespace inputSwitchDetail
	{
		enum class Device
		{
			none,
			kbm,
			gamepad
		};

		// Decides the active device from each frame's input before the game reads it.
		class DeviceTracker
		{
		public:
			[[nodiscard]] static DeviceTracker& GetSingleton() noexcept
			{
				static DeviceTracker singleton;
				return singleton;
			}

			DeviceTracker(const DeviceTracker&) = delete;
			DeviceTracker& operator=(const DeviceTracker&) = delete;

			// Main thread, ahead of PlayerControls' own handlers.
			void Scan(const RE::InputEvent* a_queueHead) noexcept
			{
				for (auto event = a_queueHead; event; event = event->next)
				{
					auto source = Device::none;
					switch (*event->device)
					{
					case RE::INPUT_DEVICE::kKeyboard:
					case RE::INPUT_DEVICE::kMouse:
						source = Device::kbm;
						break;
					case RE::INPUT_DEVICE::kGamepad:
						source = Device::gamepad;
						break;
					default:
						continue;
					}

					// The look vector carries no device, so it follows its last writer exactly.
					if (WritesLookVector(*event))
					{
						_looking = source;
					}

					if (source != _device && IsDeliberate(*event))
					{
						SwitchTo(source);
					}
				}
			}

			[[nodiscard]] Device ActiveDevice() const noexcept { return _device; }
			[[nodiscard]] bool IsGamepadActiveDevice() const noexcept { return _device == Device::gamepad; }
			[[nodiscard]] bool IsGamepadActiveLooking() const noexcept { return _looking == Device::gamepad; }

		private:
			static constexpr int32_t kMouseCountsToSwitch = 32;
			// A longer pause starts a new burst, which filters sensor noise.
			static constexpr std::chrono::milliseconds kMouseBurstGap{ 200 };
			static constexpr float kStickMagnitudeToSwitch = 0.3f;

			DeviceTracker() = default;

			// Matches LookHandler; both events overwrite lookInputVec.
			[[nodiscard]] static bool WritesLookVector(const RE::InputEvent& a_event)
			{
				static const RE::BSFixedString look{ "Look"sv };
				switch (*a_event.eventType)
				{
				case RE::INPUT_EVENT_TYPE::kMouseMove:
				case RE::INPUT_EVENT_TYPE::kThumbstick:
					return a_event.QUserEvent() == look;
				default:
					return false;
				}
			}

			// Cursor moves are excluded: Windows reports them without the mouse moving.
			[[nodiscard]] bool IsDeliberate(const RE::InputEvent& a_event) noexcept
			{
				if (const auto button = a_event.As<RE::ButtonEvent>(); button)
				{
					return button->QJustPressed();
				}

				if (const auto mouse = a_event.As<RE::MouseMoveEvent>(); mouse)
				{
					const auto now = std::chrono::steady_clock::now();
					if (now - _lastMouseMotion > kMouseBurstGap)
					{
						_mouseTravel = 0;
					}

					_lastMouseMotion = now;
					_mouseTravel += std::abs(mouse->mouseInputX) + std::abs(mouse->mouseInputY);
					return _mouseTravel >= kMouseCountsToSwitch;
				}

				if (const auto stick = a_event.As<RE::ThumbstickEvent>(); stick)
				{
					return std::hypot(stick->xValue, stick->yValue) >= kStickMagnitudeToSwitch;
				}

				return false;
			}

			void SwitchTo(Device a_device) noexcept
			{
				_mouseTravel = 0;
				_device = a_device;
			}

			// Written on the main thread, read from the UI job and Papyrus.
			std::atomic<Device> _device{ Device::none };
			std::atomic<Device> _looking{ Device::none };

			int32_t _mouseTravel{ 0 };
			std::chrono::steady_clock::time_point _lastMouseMotion;
		};

		struct PerformInputProcessing  // PlayerControls::PerformInputProcessing
		{
			static void thunk(RE::BSInputEventReceiver* a_self, const RE::InputEvent* a_queueHead)
			{
				DeviceTracker::GetSingleton().Scan(a_queueHead);
				func(a_self, a_queueHead);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		inline void InstallDeviceTracker()
		{
			REL::Relocation vtable{ RE::VTABLE::PlayerControls[0] };
			PerformInputProcessing::func = vtable.write_vfunc(0, PerformInputProcessing::thunk);
		}

		// Tells the menus about a swap; stays in MenuControls so it runs on the UI job.
		class DeviceSwapHandler : public RE::BSInputEventUser
		{
		public:
			[[nodiscard]] static DeviceSwapHandler* GetSingleton()
			{
				static DeviceSwapHandler singleton;
				return &singleton;
			}

			DeviceSwapHandler(const DeviceSwapHandler&) = delete;
			DeviceSwapHandler& operator=(const DeviceSwapHandler&) = delete;

			bool ShouldHandleEvent(const RE::InputEvent*) override
			{
				const auto device = DeviceTracker::GetSingleton().ActiveDevice();
				if (device != _applied)
				{
					_applied = device;
					UpdateControls(device == Device::gamepad);

					if (_proxied.controlMap &&
						ShouldClearKeyboardMouseIgnore(
							Menu::Client().IsMenuVisible().value_or(false)))
					{
						_proxied.controlMap->SetIgnoreKeyboardMouse(false);
					}
				}

				return false;
			}

		private:
			using AddMessage_t = void(RE::UIMessageQueue&, const RE::BSFixedString&, RE::UI_MESSAGE_TYPE);
			using UpdateGamepadDependentButtonCodes_t = void(bool);

			DeviceSwapHandler() = default;

			void UpdateControls(bool a_gamepad)
			{
				if (_proxied.ui && _proxied.msgq)
				{
					RE::BSAutoReadLock _{ _proxied.menuMapRWLock };
					for (const auto& [menu, entry] : _proxied.ui->menuMap)
					{
						_proxied.addMessage(*_proxied.msgq, menu, RE::UI_MESSAGE_TYPE::kUpdateController);
					}
				}

				_proxied.updateGamepadDependentButtonCodes(a_gamepad);
			}

			struct
			{
				RE::UI*& ui{ *reinterpret_cast<RE::UI**>(RE::ID::UI::Singleton.address()) };
				RE::BSReadWriteLock& menuMapRWLock{ *reinterpret_cast<RE::BSReadWriteLock*>(RE::ID::UI::GetMenuMapRWLock.address()) };
				RE::UIMessageQueue*& msgq{ *reinterpret_cast<RE::UIMessageQueue**>(RE::ID::UIMessageQueue::Singleton.address()) };
				RE::ControlMap*& controlMap{ *reinterpret_cast<RE::ControlMap**>(RE::ID::ControlMap::Singleton.address()) };
				AddMessage_t* const addMessage{ reinterpret_cast<AddMessage_t*>(RE::ID::UIMessageQueue::AddMessage.address()) };
				UpdateGamepadDependentButtonCodes_t* const updateGamepadDependentButtonCodes{ reinterpret_cast<UpdateGamepadDependentButtonCodes_t*>(RE::ID::UIUtils::UpdateGamepadDependentButtonCodes.address()) };
			} _proxied;  // this runs on a per-frame basis, so try to optimize perfomance

			Device _applied{ Device::none };
		};

		inline void RefreshCursor(RE::PipboyMenu& a_self)
		{
			using UIMF = RE::UI_MENU_FLAGS;

			const auto controls = RE::ControlMap::GetSingleton();
			const auto cursor = RE::MenuCursor::GetSingleton();
			const auto manager = RE::PipboyManager::GetSingleton();

			bool cursorEnabled = false;
			if (REL::Relocation<RE::PIPBOY_PAGES*> curPage{ RE::ID::PipboyMenu::CurrentPage }; *curPage == RE::PIPBOY_PAGES::kMap)
			{
				cursorEnabled = !a_self.showingModalMessage;
			}

			const auto& tracker = DeviceTracker::GetSingleton();
			const auto gamepadMenuing = tracker.IsGamepadActiveDevice();
			if (!gamepadMenuing)
			{
				cursorEnabled = true;
			}

			const auto usedCursor = a_self.UsesCursor();
			const auto assignedCursor = a_self.AssignsCursorToRenderer();
			a_self.UpdateFlag(UIMF::kUsesCursor, cursorEnabled);
			a_self.UpdateFlag(UIMF::kAssignCursorToRenderer, gamepadMenuing);
			a_self.pipboyCursorEnabled = cursorEnabled;

			if (usedCursor != a_self.UsesCursor() && cursor)
			{
				if (a_self.UsesCursor())
				{
					cursor->RegisterCursor();
				}
				else
				{
					cursor->UnregisterCursor();
				}
			}

			if (controls)
			{
				using Context = RE::UserEvents::INPUT_CONTEXT_ID;
				const auto reset = [&](Context a_context, bool a_condition)
					{
						while (controls->PopInputContext(a_context)) {}
						if (a_condition)
						{
							controls->PushInputContext(a_context);
						}
					};

				reset(Context::kThumbNav, gamepadMenuing);
				reset(Context::kLThumbCursor, gamepadMenuing && cursorEnabled);
			}

			if (cursorEnabled && cursor && manager)
			{
				const auto swapped = assignedCursor != a_self.AssignsCursorToRenderer();
				if (gamepadMenuing)
				{
					// Constrains first: centering needs the Pip-Boy screen's bounds.
					manager->UpdateCursorConstraint(true);
					if (swapped)
					{
						cursor->CenterCursor();
					}
				}
				else
				{
					cursor->ClearConstraints();
					if (swapped)
					{
						// The two cursors use different spaces, so restart mid-screen.
						cursor->CenterCursor();
						// Main::Run_WindowsMessageLoop then moves the OS cursor to match.
						cursor->forceOSCursorPos = true;
					}

					if (const auto model = RE::FlatScreenModel::GetSingleton(); model)
					{
						constexpr enumeration flags{ UIMF::kAssignCursorToRenderer };
						RE::BSUIMessageData::SendUIStringUIntMessage(RE::CursorMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, model->customRendererName, flags.underlying());
					}
				}
			}

			if (const auto ui = RE::UI::GetSingleton(); ui)
			{
				ui->RefreshCursor();
			}
		}

		inline void InstallRefreshCursorPatch()
		{
			// PipboyMenu::RefreshCursor
			{
				const auto target = RE::ID::PipboyMenu::RefreshCursor.address();
				RELEX::DetourJump(target, reinterpret_cast<std::uintptr_t>(RefreshCursor));
			}

			// PipboyMenu::ProcessMessage - skip redundant input context switching
			{
				const auto target = RE::ID::PipboyMenu::ProcessMessage.address();
				REL::WriteSafeFill(target + REL::Offset{ 0x57C, 0x5FB }.offset(), REL::NOP, 0x64);
			}

			// PipboyMenu::PipboyMenu - skip assigning cursor to renderer
			{
				const auto target = RE::ID::PipboyMenu::Ctor.address();
				REL::WriteSafeFill(target + REL::Offset{ 0x132, 0x139 }.offset(), REL::NOP, 0x17);
			}
		}

		inline void DisableDisconnectHandler()
		{
			REL::WriteSafeFill(RE::ID::MenuControls::RegisterHandlers.address() + REL::Offset{ 0x98, 0x9C, 0x94 }.offset(), REL::NOP, 0x4E);
		}

		inline void DisableKBMIgnore()
		{
			const auto target = RE::ID::ControlMap::MapToUserEvent.address();
			REL::WriteSafeFill(target + REL::Offset{ 0x39, 0x40 }.offset(), REL::NOP, REL::Offset{ 0x6, 0x7 }.offset());
		}

		inline bool IsGamepadConnected(const RE::BSInputDeviceManager&)
		{
			return DeviceTracker::GetSingleton().IsGamepadActiveDevice();
		}

		inline void InstallGamepadConnectedPatch()
		{
			const auto target = RE::ID::BSInputDeviceManager::IsGamepadConnected.address();
			constexpr std::size_t size = 0x25;
			REL::WriteSafeFill(target, REL::INT3, size);
			RELEX::DetourJump(target, reinterpret_cast<std::uintptr_t>(IsGamepadConnected));
		}

		inline bool UsingGamepad(const RE::BSInputDeviceManager&)
		{
			return DeviceTracker::GetSingleton().IsGamepadActiveDevice();
		}

		inline void InstallUsingGamepadPatch()
		{
			const auto target = RE::ID::BSInputDeviceManager::QUsingGamepad.address();
			constexpr std::size_t size = 0x25;
			REL::WriteSafeFill(target, REL::INT3, size);
			RELEX::DetourJump(target, reinterpret_cast<std::uintptr_t>(UsingGamepad));
		}

		inline bool UsingGamepadLook(const RE::BSInputDeviceManager&)
		{
			return DeviceTracker::GetSingleton().IsGamepadActiveLooking();
		}

		inline void InstallGamepadLookPatches()
		{
			const auto patch = [](const REL::VariantID& a_base, REL::Offset a_offset)
				{
					auto& trampoline = REL::GetTrampoline();
					trampoline.write_call<5>(a_base.address() + a_offset.offset(), UsingGamepadLook);
				};

			patch(RE::ID::LevelUpMenu::ZoomGrid, REL::Offset{ 0x58 });
			patch(RE::ID::PlayerControls::ProcessLookInput, REL::Offset{ 0x43, 0x3C });
			patch(RE::ID::PlayerControlsUtils::ProcessLookControls, REL::Offset{ 0x56 });
			patch(RE::ID::FirstPersonState::CalculatePitchOffsetChaseValue, REL::Offset{ 0x1F });
		}

		static void PipboyMenuPreDtor()
		{
			if (const auto controls = RE::ControlMap::GetSingleton(); controls)
			{
				using RE::UserEvents::INPUT_CONTEXT_ID::kLThumbCursor;
				while (controls->PopInputContext(kLThumbCursor)) {}
			}
		}

		struct DtorPatch : RELEX::Stub
		{
			DtorPatch(std::uintptr_t a_ret)
			{
				push(rcx);
				sub(rsp, 0x8);   // alignment
				sub(rsp, 0x20);  // function call
				mov(rax, reinterpret_cast<std::uintptr_t>(PipboyMenuPreDtor));
				call(rax);
				add(rsp, 0x20);
				add(rsp, 0x8);
				pop(rcx);

				// restore
				mov(ptr[rsp + 0x8], rbx);
				mov(ptr[rsp + 0x10], rbp);

				mov(rax, a_ret);
				jmp(rax);
			}
		};

		inline void InstallPipboyMenuStatePatches()
		{
			const auto patch = []<class T>(std::in_place_type_t<T>, REL::VariantID a_func, std::size_t a_size)
			{
				AdAssert(a_size >= 6);

				const auto target = a_func.address();
				REL::WriteSafeFill(target, REL::NOP, a_size);

				T p{ target + a_size };
				p.ready();

				auto& trampoline = REL::GetTrampoline();
				trampoline.write_jmp<6>(target, trampoline.allocate(p));
			};

			patch(std::in_place_type<DtorPatch>, RE::ID::PipboyMenu::Dtor, 0xA);
		}

		static int GetMappedKey([[maybe_unused]] std::monostate a_base, RE::BSFixedString a_control, int a_deviceType = 0xFF) noexcept
		{
			const auto controls = RE::ControlMap::GetSingleton();
			if (!controls)
				return -1;

			int key = 0xFF;

			if (a_deviceType != 0xFF)
				// Manually Selected Device
				key = controls->GetMappedKey(a_control, static_cast<RE::INPUT_DEVICE>(a_deviceType));
			else
			{
				// Auto-Select Current Device
				if (DeviceTracker::GetSingleton().IsGamepadActiveDevice())
				{
					// Gamepad
					a_deviceType = (int)RE::INPUT_DEVICE::kGamepad;
					key = controls->GetMappedKey(a_control, RE::INPUT_DEVICE::kGamepad);
				}
				else
				{
					// Keyboard
					a_deviceType = (int)RE::INPUT_DEVICE::kKeyboard;
					key = controls->GetMappedKey(a_control, RE::INPUT_DEVICE::kKeyboard);

					if (key == 0xFF)
					{
						// Mouse
						a_deviceType = (int)RE::INPUT_DEVICE::kMouse;
						key = controls->GetMappedKey(a_control, RE::INPUT_DEVICE::kMouse);
					}
				}
			}

			if (key == 0xFF)
				return -1;

			if (a_deviceType == (int)RE::INPUT_DEVICE::kGamepad)
			{
				int mappedKey = F4SE::InputMap::GamepadMaskToKeycode(key);
				return mappedKey != F4SE::InputMap::kMaxMacros ? mappedKey : -1;
			}
			else if (a_deviceType == (int)RE::INPUT_DEVICE::kMouse)
			{
				return key + F4SE::InputMap::kMacro_MouseButtonOffset;
			}
			else
				return key;
		}
	}

	ModuleInputSwitch::ModuleInputSwitch() :
		Module("Input Switch", &bPatchesInputSwitch, {}, true)
	{}

	bool ModuleInputSwitch::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kGameLoaded)
		{
			if (const auto Controls = RE::MenuControls::GetSingleton(); Controls)
			{
				Controls->handlers.insert(Controls->handlers.begin(), inputSwitchDetail::DeviceSwapHandler::GetSingleton());
				return true;
			}
		}
		else
		{
			inputSwitchDetail::InstallDeviceTracker();
			inputSwitchDetail::DisableDisconnectHandler();
			inputSwitchDetail::DisableKBMIgnore();
			inputSwitchDetail::InstallRefreshCursorPatch();
			inputSwitchDetail::InstallGamepadConnectedPatch();
			inputSwitchDetail::InstallUsingGamepadPatch();
			inputSwitchDetail::InstallGamepadLookPatches();
			inputSwitchDetail::InstallPipboyMenuStatePatches();
		}

		return true;
	}

	bool ModuleInputSwitch::DoPapyrusListener([[maybe_unused]] RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		a_vm->BindNativeMethod("Input"sv, "GetMappedKey"sv, inputSwitchDetail::GetMappedKey);

		return true;
	}
}