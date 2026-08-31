#pragma once

#include <cstdint>

namespace Addictol::DearModdingUI::CarrierMenu
{
	enum class Event : uint32_t
	{
		kOpen,
		kClose,
		kShutdown,
		kBackendFailure,
		kRetarget,
		kGameTransition,
		kOverlayOnly
	};

	enum class Action : uint32_t
	{
		kNone,
		kShow,
		kHide
	};

	struct State
	{
		bool open{ false };
	};

	[[nodiscard]] constexpr Action Transition(
		State& a_state,
		Event a_event) noexcept
	{
		const auto nextOpen = a_event == Event::kOpen;
		if (a_state.open == nextOpen)
			return Action::kNone;
		a_state.open = nextOpen;
		return nextOpen ? Action::kShow : Action::kHide;
	}

	[[nodiscard]] bool Register() noexcept;
	void Handle(Event a_event) noexcept;
}
