#include <Modules\AdModuleInputSwitch.h>
#include <AdUtils.h>
#include <AdAssert.h>

#include <RE/B/BSInputDeviceManager.h>
#include <RE/B/BSInputEventUser.h>
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
	static REX::TOML::Bool<> bPatchesInputSwitch{ "Patches"sv, "bInputSwitch"sv, true };

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

	namespace detail
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

					if (_proxied.controlMap)
					{
						_proxied.controlMap->SetIgnoreKeyboardMouse(false);
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
				RE::UI*& ui{ *reinterpret_cast<RE::UI**>(REL::ID{ 548587, 2689028, 4796314 }.address()) };
				RE::BSReadWriteLock& menuMapRWLock{ *reinterpret_cast<RE::BSReadWriteLock*>(REL::ID{ 578487, 2707105 }.address()) };
				RE::UIMessageQueue*& msgq{ *reinterpret_cast<RE::UIMessageQueue**>(REL::ID{ 82123, 2689091, 4796377 }.address()) };
				RE::ControlMap*& controlMap{ *reinterpret_cast<RE::ControlMap**>(REL::ID{ 325206, 2692014, 4799307 }.address()) };
				AddMessage_t* const addMessage{ reinterpret_cast<AddMessage_t*>(REL::ID{ 1182019, 2284929 }.address()) };
				UpdateGamepadDependentButtonCodes_t* const updateGamepadDependentButtonCodes{ reinterpret_cast<UpdateGamepadDependentButtonCodes_t*>(REL::ID{ 190238, 2249714, 4483350 }.address()) };
			} _proxied;  // this runs on a per-frame basis, so try to optimize perfomance

			struct
			{
				RE::BSFixedString look{ "Look" };
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
				const auto target = REL::ID{ 1533778, 2224196 }.address();
				RELEX::DetourJump(target, reinterpret_cast<std::uintptr_t>(RefreshCursor));
			}

			// PipboyMenu::ProcessMessage - skip redundant input context switching
			{
				const auto target = REL::ID{ 643948, 2224181 }.address();
				REL::WriteSafeFill(target + REL::Offset{ 0x57C, 0x5FB }.offset(), REL::NOP, 0x64);
			}

			// PipboyMenu::PipboyMenu - skip assigning cursor to renderer
			{
				const auto target = REL::ID{ 712982, 2224179 }.address();
				REL::WriteSafeFill(target + REL::Offset{ 0x132, 0x139 }.offset(), REL::NOP, 0x17);
			}
		}

		inline void DisableDisconnectHandler()
		{
			REL::ID target{ 548136, 2249389 };
			REL::WriteSafeFill(target.address() + REL::Offset{ 0x98, 0x9C, 0x94 }.offset(), REL::NOP, 0x4E);
		}

		inline void DisableKBMIgnore()
		{
			const auto target = REL::ID{ 647956, 2268334 }.address();
			REL::WriteSafeFill(target + REL::Offset{ 0x39, 0x40 }.offset(), REL::NOP, REL::Offset{ 0x6, 0x7 }.offset());
		}

		inline bool IsGamepadConnected(const RE::BSInputDeviceManager&)
		{
			const auto handler = DeviceSwapHandler::GetSingleton();
			return handler->IsGamepadActiveDevice();
		}

		inline void InstallGamepadConnectedPatch()
		{
			const auto target = REL::ID{ 609928, 2268387 }.address();
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
			const auto target = REL::ID{ 875683, 2268386 }.address();
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
			const auto patch = [](REL::ID a_base, REL::Offset a_offset)
				{
					auto& trampoline = REL::GetTrampoline();
					trampoline.write_call<5>(a_base.address() + a_offset.offset(), UsingGamepadLook);
				};

			patch(REL::ID{ 1349441, 2223308 }, REL::Offset{ 0x58 });		// LevelUpMenu::ZoomGrid
			patch(REL::ID{ 455462, 2234801 }, REL::Offset{ 0x43, 0x3C });	// PlayerControls::ProcessLookInput
			patch(REL::ID{ 53721, 2234829 }, REL::Offset{ 0x56 });			// PlayerControlsUtils::ProcessLookControls
			patch(REL::ID{ 1262531, 2248276 }, REL::Offset{ 0x1F });		// FirstPersonState::CalculatePitchOffsetChaseValue
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
			const auto patch = []<class T>(std::in_place_type_t<T>, REL::ID a_func, std::size_t a_size)
			{
				AdAssert(a_size >= 6);

				const auto target = a_func.address();
				REL::WriteSafeFill(target, REL::NOP, a_size);

				T p{ target + a_size };
				p.ready();

				auto& trampoline = REL::GetTrampoline();
				trampoline.write_jmp<6>(target, trampoline.allocate(p));
			};

			patch(std::in_place_type<DtorPatch>, REL::ID{ 405150, 2224180 }, 0xA);
		}
	}

	ModuleInputSwitch::ModuleInputSwitch() :
		Module("Module Input Switch", &bPatchesInputSwitch)
	{}

	bool ModuleInputSwitch::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleInputSwitch::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kGameLoaded)
		{
			if (const auto Controls = RE::MenuControls::GetSingleton(); Controls)
			{
				Controls->handlers.insert(Controls->handlers.begin(), detail::DeviceSwapHandler::GetSingleton());
				return true;
			}
		}
		else
		{
			detail::DisableDisconnectHandler();
			detail::DisableKBMIgnore();
			detail::InstallRefreshCursorPatch();
			detail::InstallGamepadConnectedPatch();
			detail::InstallUsingGamepadPatch();
			detail::InstallGamepadLookPatches();
			detail::InstallPipboyMenuStatePatches();
		}

		return true;
	}

	bool ModuleInputSwitch::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleInputSwitch::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}