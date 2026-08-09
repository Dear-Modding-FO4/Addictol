#pragma once

#include <RE/B/BSFixedString.h>
#include <RE/B/BSTObjectArena.h>
#include <RE/I/IKeywordFormBase.h>
#include <RE/S/SubgraphIdentifier.h>

#include <array>
#include <cstdint>
#include <span>

namespace Addictol::AnimSubGraphRuntime
{
	enum class Role : std::uint32_t
	{
		kLocomotion = 0,
		kWeapon = 1,
		kThirdPerson = 2
	};

	using PathArena = RE::BSTObjectArena<RE::BSFixedString, RE::BSTObjectArenaScrapAlloc, 32>;
	using TGetMatchedSubGraphData = bool(*)(
		void*, unsigned, Role&, RE::IKeywordFormBase&, RE::BSScrapArray<RE::IKeywordFormBase*>&,
		bool, RE::SubgraphIdentifier&, RE::BSFixedString&, PathArena&);
	using Handler = bool(*)(
		void*, unsigned, Role&, RE::IKeywordFormBase&, RE::BSScrapArray<RE::IKeywordFormBase*>&,
		bool, RE::SubgraphIdentifier&, RE::BSFixedString&, PathArena&, TGetMatchedSubGraphData);
	inline constexpr std::size_t kMaxActorKeywords = 128;
	inline constexpr std::size_t kKeywordScratchCapacity = 4096;
	inline constexpr std::size_t kMaxPendingMatches = 32;
	inline constexpr std::size_t kMaxCachedPaths = 64;

	struct MatchKey
	{
		std::uint32_t raceFormID{};
		std::uint16_t keywordCount{};
		bool subIndex{};
		std::array<std::uintptr_t, kMaxActorKeywords> actorKeywords{};

		[[nodiscard]] bool operator==(const MatchKey&) const noexcept = default;
	};

	struct MatchKeyHash
	{
		[[nodiscard]] std::size_t operator()(const MatchKey& a_key) const noexcept;
	};

	using RequestCallback = void(*)(Role*);
	using CacheOutcomeCallback = void(*)(Role, bool);

	[[nodiscard]] TGetMatchedSubGraphData GetOriginal() noexcept;
	[[nodiscard]] std::array<std::uintptr_t, 2> GetRuntimeCallSites() noexcept;
	[[nodiscard]] bool ValidateUniqueSignature(std::uintptr_t a_target, std::span<const std::uint8_t> a_signature) noexcept;
	[[nodiscard]] bool ValidateCallSites() noexcept;
	[[nodiscard]] bool Install() noexcept;
	[[nodiscard]] bool ValidateRequestHook() noexcept;
	[[nodiscard]] bool InstallRequestHook() noexcept;
	void ReleaseRequestHook() noexcept;
	void SetCacheHandler(Handler a_handler) noexcept;
	void SetProfilerHandler(Handler a_handler) noexcept;
	void SetCacheRequestCallbacks(RequestCallback a_begin, RequestCallback a_end) noexcept;
	void SetProfilerRequestCallbacks(RequestCallback a_begin, RequestCallback a_end) noexcept;
	void SetCacheOutcomeCallback(CacheOutcomeCallback a_callback) noexcept;
	void ReportCacheOutcome(Role a_role, bool a_hit) noexcept;
	void SetProjectionState(std::uint64_t a_generation, bool a_ready, std::size_t a_capacity) noexcept;
	void SetCacheActive(bool a_active) noexcept;
	[[nodiscard]] std::uint64_t GetProjectionGeneration() noexcept;
	[[nodiscard]] bool IsProjectionReady() noexcept;
	[[nodiscard]] bool IsCacheActive() noexcept;
	[[nodiscard]] std::size_t GetProjectionCapacity() noexcept;
}
