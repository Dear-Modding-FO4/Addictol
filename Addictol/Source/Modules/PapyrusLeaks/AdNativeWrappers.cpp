#include <Modules/PapyrusLeaks/AdNativeWrappers.h>
#include <Core/AdUtils.h>

#include <RE/G/GameScript.h>

namespace Addictol::PapyrusLeaks
{
	namespace
	{
		bool (*s_shouldAttemptToCleanHandle)(RE::GameScript::HandlePolicy*, uint64_t){ nullptr };
		void (*s_objectNoLongerNeeded)(RE::GameScript::ObjectBindPolicy*, uint64_t){ nullptr };

		[[nodiscard]] uintptr_t ShouldAttemptToCleanHandleEntry() noexcept
		{
			return REL::Relocation{ RE::ID::GameScript::HandlePolicy::ShouldAttemptToCleanHandle }.address();
		}

		[[nodiscard]] uintptr_t ObjectNoLongerNeededEntry() noexcept
		{
			return REL::Relocation{ RE::ID::GameScript::ObjectBindPolicy::ObjectNoLongerNeeded }.address();
		}

		// Vanilla reaps created forms only, so wrappers on plugin forms are never freed
		[[nodiscard]] bool IsPluginFormHandle(uint64_t a_handle) noexcept
		{
			return (a_handle >> 48) == NativeWrappers::kKind && (static_cast<RE::TESFormID>(a_handle) >> 24) != 0xFF;
		}

		// The reaper removes only unreferenced native-type Objects, never user scripts
		bool HKShouldAttemptToCleanHandle(RE::GameScript::HandlePolicy* a_policy, uint64_t a_handle) noexcept
		{
			return (IsPluginFormHandle(a_handle) && a_handle != a_policy->EmptyHandle()) || s_shouldAttemptToCleanHandle(a_policy, a_handle);
		}

		// Keep the retained-ID flag (OnLoad tracking) and any contained ref's unique ID
		void HKObjectNoLongerNeeded(RE::GameScript::ObjectBindPolicy* a_policy, uint64_t a_handle) noexcept
		{
			if (!IsPluginFormHandle(a_handle))
				s_objectNoLongerNeeded(a_policy, a_handle);
		}
	}

	std::string_view NativeWrappers::CheckSites() noexcept
	{
		// sub rsp, 28h; mov rax, rdx; shr rax, 30h
		if (!RELEX::Validate(ShouldAttemptToCleanHandleEntry(), { 0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0xC2, 0x48, 0xC1, 0xE8, 0x30 }))
			return "unexpected HandlePolicy::ShouldAttemptToCleanHandle prologue"sv;

		// sub rsp, 68h; lea rcx, [rsp+40h]
		if (!RELEX::Validate(ObjectNoLongerNeededEntry(), { 0x48, 0x83, 0xEC, 0x68, 0x48, 0x8D, 0x4C, 0x24, 0x40 }))
			return "unexpected ObjectBindPolicy::ObjectNoLongerNeeded prologue"sv;

		return {};
	}

	bool NativeWrappers::Install() noexcept
	{
		// guard first, so no reaped plugin form ever reaches the unguarded callback
		s_objectNoLongerNeeded = reinterpret_cast<decltype(s_objectNoLongerNeeded)>(
			RELEX::DetourJump(ObjectNoLongerNeededEntry(), reinterpret_cast<uintptr_t>(&HKObjectNoLongerNeeded)));
		if (!s_objectNoLongerNeeded)
			return false;

		s_shouldAttemptToCleanHandle = reinterpret_cast<decltype(s_shouldAttemptToCleanHandle)>(
			RELEX::DetourJump(ShouldAttemptToCleanHandleEntry(), reinterpret_cast<uintptr_t>(&HKShouldAttemptToCleanHandle)));
		return s_shouldAttemptToCleanHandle != nullptr;
	}
}
