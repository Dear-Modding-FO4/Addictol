#include <Modules/AdModuleInputSwitch.h>
#include <Menu/AdMenu.h>
#include <Core/AdUtils.h>
#include <Core/AdAssert.h>

#include <RE/B/BSInputDeviceManager.h>
#include <RE/B/BSInputEventUser.h>
#include <RE/B/BSScriptUtil.h>
#include <RE/B/BSUIMessageData.h>
#include <RE/C/ControlMap.h>
#include <RE/C/CursorMenu.h>
#include <RE/F/FlatScreenModel.h>
#include <RE/I/IDEvent.h>
#include <RE/I/INPUT_DEVICE.h>
#include <RE/I/InputEvent.h>
#include <RE/M/MenuControls.h>
#include <RE/M/MenuCursor.h>
#include <RE/M/MouseMoveEvent.h>
#include <RE/P/PIPBOY_PAGES.h>
#include <RE/P/PipboyManager.h>
#include <RE/P/PipboyMenu.h>
#include <RE/U/UI.h>
#include <RE/U/UIMessageQueue.h>

#include <xbyak/xbyak.h>

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

			bool ShouldHandleEvent(const RE::InputEvent* a_event) override
			{
				if (a_event)
				{
					const auto& event = *a_event;
					auto input = Device::none;
					switch (*event.device)
					{
					case RE::INPUT_DEVICE::kKeyboard:
					case RE::INPUT_DEVICE::kMouse:
						input = Device::kbm;
						break;
					case RE::INPUT_DEVICE::kGamepad:
						input = Device::gamepad;
						break;
					}

					if (input != Device::none && _active.device != input)
					{
						_active.device = input;
						UpdateControls();

						if (_proxied.controlMap &&
							ShouldClearKeyboardMouseIgnore(
								Menu::Client().IsMenuVisible().value_or(false)))
						{
							_proxied.controlMap->SetIgnoreKeyboardMouse(false);
						}
					}

					if (const auto id = event.As<RE::IDEvent>(); id)
					{
						auto& control = id->strUserEvent;
						const auto trySet = [&](auto& a_device) noexcept
							{
								if (const auto mouse = event.As<RE::MouseMoveEvent>(); mouse && (mouse->mouseInputX != 0 || mouse->mouseInputY != 0))
								{
									a_device = input;
								}
								else
								{
									a_device = input;
								}
							};

						if (control == _strings.look)
						{
							trySet(_active.looking);
						}
					}
				}

				return false;
			}

			[[nodiscard]] bool IsGamepadActiveDevice() const noexcept { return _active.device == Device::gamepad; }
			[[nodiscard]] bool IsGamepadActiveLooking() const noexcept { return _active.looking == Device::gamepad; }

		private:
			using AddMessage_t = void(RE::UIMessageQueue&, const RE::BSFixedString&, RE::UI_MESSAGE_TYPE);
			using UpdateGamepadDependentButtonCodes_t = void(bool);

			DeviceSwapHandler() = default;

			enum class Device
			{
				none,
				kbm,
				gamepad
			};

			void UpdateControls()
			{
				if (_proxied.ui && _proxied.msgq)
				{
					RE::BSAutoReadLock _{ _proxied.menuMapRWLock };
					for (const auto& [menu, entry] : _proxied.ui->menuMap)
					{
						_proxied.addMessage(*_proxied.msgq, menu, RE::UI_MESSAGE_TYPE::kUpdateController);
					}
				}

				_proxied.updateGamepadDependentButtonCodes(IsGamepadActiveDevice());
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

			struct
			{
				RE::BSFixedString look{ "Look"sv };
			} _strings;  // use optimized pointer comparison instead of slow string comparison

			struct
			{
				std::atomic<Device> device{ Device::none };
				std::atomic<Device> looking{ Device::none };
			} _active;
		};

		inline void RefreshCursor(RE::PipboyMenu& a_self)
		{
			using UIMF = RE::UI_MENU_FLAGS;

			const auto controls = RE::ControlMap::GetSingleton();
			const auto cursor = RE::MenuCursor::GetSingleton();
			const auto manager = RE::PipboyManager::GetSingleton();

			bool cursorEnabled = false;
			if (REL::Relocation<RE::PIPBOY_PAGES*> curPage{ REL::ID{ 1287022, 2696944, 4804316 } }; *curPage == RE::PIPBOY_PAGES::kMap) // OG: 1287022, NG: 2696944
			{
				cursorEnabled = !a_self.showingModalMessage;
			}

			const auto gamepadMenuing = DeviceSwapHandler::GetSingleton()->IsGamepadActiveDevice();
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
				if (gamepadMenuing)
				{
					if (assignedCursor != a_self.AssignsCursorToRenderer())
					{
						cursor->CenterCursor();
					}

					manager->UpdateCursorConstraint(true);
				}
				else
				{
					cursor->ClearConstraints();
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
			const auto handler = DeviceSwapHandler::GetSingleton();
			return handler->IsGamepadActiveDevice();
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
			const auto handler = DeviceSwapHandler::GetSingleton();
			return handler->IsGamepadActiveDevice();
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
			const auto handler = DeviceSwapHandler::GetSingleton();
			return handler->IsGamepadActiveLooking();
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

		struct DtorPatch : Xbyak::CodeGenerator
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
				const auto handler = DeviceSwapHandler::GetSingleton();

				if (handler && handler->IsGamepadActiveDevice())
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