#include <AdAnimSubGraphRuntime.h>
#include <AdUtils.h>

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstring>

namespace Addictol::AnimSubGraphRuntime
{
	static TGetMatchedSubGraphData g_original;
	static std::atomic<Handler> g_cacheHandler;
	static std::atomic<Handler> g_profilerHandler;
	static std::atomic<RequestCallback> g_cacheRequestBegin;
	static std::atomic<RequestCallback> g_cacheRequestEnd;
	static std::atomic<RequestCallback> g_profilerRequestBegin;
	static std::atomic<RequestCallback> g_profilerRequestEnd;
	static std::atomic<CacheOutcomeCallback> g_cacheOutcomeCallback;
	static std::atomic<std::uint64_t> g_projectionGeneration;
	static std::atomic<std::size_t> g_projectionCapacity{ 32768 };
	static std::atomic<bool> g_projectionReady{ true };
	static std::atomic<bool> g_cacheActive;
	static std::atomic<bool> g_installed;
	static std::atomic<bool> g_requestInstalled;
	static std::atomic<std::uint32_t> g_requestOwners;
	static std::uintptr_t g_requestOriginal;

	using TGeneric = std::uintptr_t(*)(
		std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,
		std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,
		std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);

	std::size_t MatchKeyHash::operator()(const MatchKey& a_key) const noexcept
	{
		std::size_t hash = a_key.raceFormID;
		hash ^= std::hash<bool>{}(a_key.subIndex) + 0x9E3779B97F4A7C15ull + (hash << 6) + (hash >> 2);
		for (std::size_t index = 0; index < a_key.keywordCount; ++index)
			hash ^= std::hash<std::uintptr_t>{}(a_key.actorKeywords[index]) + 0x9E3779B97F4A7C15ull + (hash << 6) + (hash >> 2);
		return hash;
	}

	static bool CallCache(
		void* a_singleton, unsigned a_raceFormID, Role& a_role,
		RE::IKeywordFormBase& a_actorKeywordForm,
		RE::BSScrapArray<RE::IKeywordFormBase*>& a_targetKeywordForms,
		bool a_subIndex, RE::SubgraphIdentifier& a_outID, RE::BSFixedString& a_outRoot,
		PathArena& a_outPaths) noexcept
	{
		const auto cache = g_cacheHandler.load(std::memory_order_acquire);
		return cache ?
			cache(a_singleton, a_raceFormID, a_role, a_actorKeywordForm, a_targetKeywordForms,
				a_subIndex, a_outID, a_outRoot, a_outPaths, g_original) :
			g_original(a_singleton, a_raceFormID, a_role, a_actorKeywordForm, a_targetKeywordForms,
				a_subIndex, a_outID, a_outRoot, a_outPaths);
	}

	static bool Dispatch(
		void* a_singleton, unsigned a_raceFormID, Role& a_role,
		RE::IKeywordFormBase& a_actorKeywordForm,
		RE::BSScrapArray<RE::IKeywordFormBase*>& a_targetKeywordForms,
		bool a_subIndex, RE::SubgraphIdentifier& a_outID, RE::BSFixedString& a_outRoot,
		PathArena& a_outPaths) noexcept
	{
		const auto profiler = g_profilerHandler.load(std::memory_order_acquire);
		return profiler ?
			profiler(a_singleton, a_raceFormID, a_role, a_actorKeywordForm, a_targetKeywordForms,
				a_subIndex, a_outID, a_outRoot, a_outPaths, CallCache) :
			CallCache(a_singleton, a_raceFormID, a_role, a_actorKeywordForm, a_targetKeywordForms,
				a_subIndex, a_outID, a_outRoot, a_outPaths);
	}

	static std::uintptr_t RequestAnimationSubGraph(
		std::uintptr_t a_1, std::uintptr_t a_2, std::uintptr_t a_3, std::uintptr_t a_4,
		std::uintptr_t a_5, std::uintptr_t a_6, std::uintptr_t a_7, std::uintptr_t a_8,
		std::uintptr_t a_9, std::uintptr_t a_10, std::uintptr_t a_11, std::uintptr_t a_12) noexcept
	{
		auto* role = reinterpret_cast<Role*>(a_4);
		if (const auto callback = g_cacheRequestBegin.load(std::memory_order_acquire))
			callback(role);
		if (const auto callback = g_profilerRequestBegin.load(std::memory_order_acquire))
			callback(role);
		const auto result = reinterpret_cast<TGeneric>(g_requestOriginal)(
			a_1, a_2, a_3, a_4, a_5, a_6, a_7, a_8, a_9, a_10, a_11, a_12);
		if (const auto callback = g_profilerRequestEnd.load(std::memory_order_acquire))
			callback(role);
		if (const auto callback = g_cacheRequestEnd.load(std::memory_order_acquire))
			callback(role);
		return result;
	}

	TGetMatchedSubGraphData GetOriginal() noexcept
	{
		if (!g_original)
			g_original = reinterpret_cast<TGetMatchedSubGraphData>(
				REL::Relocation<>{ REL::ID{ 801415, 2188857, 2188857 } }.address());
		return g_original;
	}

	std::array<std::uintptr_t, 2> GetRuntimeCallSites() noexcept
	{
		// Call offsets are OG 0x7E/0xBC and NG/AE 0x157/0xB4 after subtracting each containing-function start.
		return {
			REL::Relocation<>{ REL::ID{ 915678, 2236409, 2236409 }, REL::Offset{ 0x7E, 0x157, 0x157 } }.address(),
			REL::Relocation<>{ REL::ID{ 929639, 2236417, 2236417 }, REL::Offset{ 0xBC, 0xB4, 0xB4 } }.address()
		};
	}

	bool ValidateUniqueSignature(std::uintptr_t a_target, std::span<const std::uint8_t> a_signature) noexcept
	{
		if (a_signature.empty() ||
			std::memcmp(reinterpret_cast<const void*>(a_target), a_signature.data(), a_signature.size()) != 0)
			return false;

		const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("Fallout4.exe"));
		if (!base)
			return false;
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
		const auto* section = IMAGE_FIRST_SECTION(nt);
		for (std::uint16_t index = 0; index < nt->FileHeader.NumberOfSections; ++index)
		{
			if (std::memcmp(section[index].Name, ".text", 5) != 0)
				continue;
			const auto* begin = reinterpret_cast<const std::uint8_t*>(base + section[index].VirtualAddress);
			const auto size = static_cast<std::size_t>(section[index].Misc.VirtualSize);
			std::size_t matches = 0;
			for (std::size_t offset = 0; offset + a_signature.size() <= size; ++offset)
			{
				if (std::memcmp(begin + offset, a_signature.data(), a_signature.size()) == 0)
					++matches;
			}
			return matches == 1;
		}
		return false;
	}

	bool ValidateCallSites() noexcept
	{
		if (g_installed.load(std::memory_order_acquire))
			return true;

		const auto expected = reinterpret_cast<std::uintptr_t>(GetOriginal());
		static constexpr std::uint8_t og[]{
			0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
			0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8D, 0x59, 0x50, 0x48, 0x8B, 0xF9, 0x4D,
			0x8B, 0xF1, 0x48, 0x8B, 0xCB, 0x4D, 0x8B, 0xF8, 0x8B, 0xF2, 0x40, 0x32, 0xED, 0xE8, 0x4E, 0x01
		};
		static constexpr std::uint8_t ng[]{
			0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
			0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8D, 0x59, 0x50, 0x48, 0x8B, 0xF9, 0x48,
			0x8B, 0xCB, 0x4D, 0x8B, 0xF1, 0x4D, 0x8B, 0xF8, 0x8B, 0xF2, 0x40, 0x32, 0xED, 0xE8, 0xDE, 0x35
		};
		static constexpr std::uint8_t ae[]{
			0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
			0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8D, 0x59, 0x50, 0x48, 0x8B, 0xF9, 0x48,
			0x8B, 0xCB, 0x4D, 0x8B, 0xF1, 0x4D, 0x8B, 0xF8, 0x8B, 0xF2, 0x40, 0x32, 0xED, 0xE8, 0x9E, 0x47
		};
		const auto signature = RELEX::IsRuntimeOG() ? std::span{ og } :
			RELEX::IsRuntimeNG() ? std::span{ ng } : std::span{ ae };
		if (!ValidateUniqueSignature(expected, signature))
		{
			REX::WARN("Animation Subgraph runtime: matcher at {:X} failed unique signature validation; installing nothing."sv,
				expected);
			return false;
		}
		for (const auto site : GetRuntimeCallSites())
		{
			if (*reinterpret_cast<const std::uint8_t*>(site) != 0xE8)
			{
				REX::WARN("Animation Subgraph runtime: call site {:X} is not E8; installing nothing."sv, site);
				return false;
			}
			const auto displacement = *reinterpret_cast<const std::int32_t*>(site + 1);
			const auto target = site + 5 + static_cast<std::intptr_t>(displacement);
			if (target != expected)
			{
				REX::WARN("Animation Subgraph runtime: call site {:X} targets {:X}, expected {:X}; installing nothing."sv,
					site, target, expected);
				return false;
			}
		}
		return true;
	}

	bool Install() noexcept
	{
		if (g_installed.load(std::memory_order_acquire))
			return true;
		if (!ValidateCallSites())
			return false;

		const auto sites = GetRuntimeCallSites();
		std::array<std::array<std::uint8_t, 5>, 2> originals{};
		for (std::size_t index = 0; index < sites.size(); ++index)
			std::memcpy(originals[index].data(), reinterpret_cast<const void*>(sites[index]), originals[index].size());

		auto& trampoline = REL::GetTrampoline();
		for (std::size_t index = 0; index < sites.size(); ++index)
		{
			if (trampoline.write_call<5>(sites[index], Dispatch) != reinterpret_cast<std::uintptr_t>(GetOriginal()))
			{
				for (std::size_t rollback = 0; rollback <= index; ++rollback)
					REL::WriteSafe(sites[rollback], originals[rollback].data(), originals[rollback].size());
				REX::WARN("Animation Subgraph runtime: call-site patch failed; changes rolled back."sv);
				return false;
			}
		}

		g_installed.store(true, std::memory_order_release);
		return true;
	}

	bool ValidateRequestHook() noexcept
	{
		if (g_requestInstalled.load(std::memory_order_acquire))
			return true;
		// 16 bytes matches 25 sites in .text, so the uniqueness scan needs 32+; NG and AE share these bytes.
		static constexpr std::uint8_t og[]{
			0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x4C,
			0x89, 0x74, 0x24, 0x20, 0x55, 0x48, 0x8D, 0x6C, 0x24, 0xD1, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00,
			0x00, 0x4C, 0x8B, 0xF1, 0x48, 0x8D, 0x4D, 0x9F, 0x49, 0x8B, 0xF9, 0x49, 0x8B, 0xF0, 0x48, 0x8B
		};
		static constexpr std::uint8_t ng[]{
			0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x48,
			0x89, 0x4C, 0x24, 0x08, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C,
			0x24, 0xF1, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xF1, 0x49, 0x8B, 0xF9, 0x48
		};
		const auto target = REL::Relocation<>{ REL::ID{ 969017, 2236383, 2236383 } }.address();
		return ValidateUniqueSignature(target, RELEX::IsRuntimeOG() ? std::span{ og } : std::span{ ng });
	}

	bool InstallRequestHook() noexcept
	{
		if (g_requestInstalled.load(std::memory_order_acquire))
		{
			g_requestOwners.fetch_add(1, std::memory_order_relaxed);
			return true;
		}
		if (!ValidateRequestHook())
			return false;
		const auto target = REL::Relocation<>{ REL::ID{ 969017, 2236383, 2236383 } }.address();
		g_requestOriginal = RELEX::DetourJump(target, reinterpret_cast<std::uintptr_t>(&RequestAnimationSubGraph));
		if (!g_requestOriginal)
			return false;
		g_requestOwners.store(1, std::memory_order_release);
		g_requestInstalled.store(true, std::memory_order_release);
		return true;
	}

	void ReleaseRequestHook() noexcept
	{
		if (!g_requestInstalled.load(std::memory_order_acquire) ||
			g_requestOwners.fetch_sub(1, std::memory_order_acq_rel) != 1)
			return;
		// DetourRemove is broken (reads the header at the wrong offset), so pin the owner count instead.
		g_requestOwners.store(1, std::memory_order_release);
	}

	void SetCacheHandler(Handler a_handler) noexcept
	{
		g_cacheHandler.store(a_handler, std::memory_order_release);
	}

	void SetProfilerHandler(Handler a_handler) noexcept
	{
		g_profilerHandler.store(a_handler, std::memory_order_release);
	}

	void SetCacheRequestCallbacks(RequestCallback a_begin, RequestCallback a_end) noexcept
	{
		g_cacheRequestBegin.store(a_begin, std::memory_order_release);
		g_cacheRequestEnd.store(a_end, std::memory_order_release);
	}

	void SetProfilerRequestCallbacks(RequestCallback a_begin, RequestCallback a_end) noexcept
	{
		g_profilerRequestBegin.store(a_begin, std::memory_order_release);
		g_profilerRequestEnd.store(a_end, std::memory_order_release);
	}

	void SetCacheOutcomeCallback(CacheOutcomeCallback a_callback) noexcept
	{
		g_cacheOutcomeCallback.store(a_callback, std::memory_order_release);
	}

	void ReportCacheOutcome(Role a_role, bool a_hit) noexcept
	{
		if (const auto callback = g_cacheOutcomeCallback.load(std::memory_order_acquire))
			callback(a_role, a_hit);
	}

	void SetProjectionState(std::uint64_t a_generation, bool a_ready, std::size_t a_capacity) noexcept
	{
		g_projectionCapacity.store(a_capacity, std::memory_order_release);
		g_projectionReady.store(a_ready, std::memory_order_release);
		g_projectionGeneration.store(a_generation, std::memory_order_release);
	}

	void SetCacheActive(bool a_active) noexcept
	{
		g_cacheActive.store(a_active, std::memory_order_release);
	}

	std::uint64_t GetProjectionGeneration() noexcept
	{
		return g_projectionGeneration.load(std::memory_order_acquire);
	}

	bool IsProjectionReady() noexcept
	{
		return g_projectionReady.load(std::memory_order_acquire);
	}

	bool IsCacheActive() noexcept
	{
		return g_cacheActive.load(std::memory_order_acquire);
	}

	std::size_t GetProjectionCapacity() noexcept
	{
		return g_projectionCapacity.load(std::memory_order_acquire);
	}
}
