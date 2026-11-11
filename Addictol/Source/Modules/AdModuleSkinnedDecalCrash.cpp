// Thanks to Yuof for finding this bug in Skyrim: https://github.com/Yuof/CrashFixCompendium
#include <Modules/AdModuleSkinnedDecalCrash.h>
#include <Core/AdUtils.h>

#include <mutex>

namespace Addictol
{
	namespace skinnedDecalCrashDetail
	{
		using Traverse_t = uint32_t(void*, void*);

		static Traverse_t* s_traverse = nullptr;

		// Recursive because the walk is engine code; a nested AddDecal must not deadlock.
		static std::recursive_mutex s_lock;

		// Projectile updates run as parallel BSJobs, and each impact reaches AddDecal. For a skinned hit
		// the walk finds or creates the root's BGSDecalNode, whose constructor attaches it to the root,
		// appends it to the manager's node array, and attaches the decal's node to it, all unlocked.
		// Two pellets hitting one actor in a frame race NiNode child-array and manager-array growth.
		static uint32_t HKTraverse(void* a_root, void* a_visitor) noexcept
		{
			const std::scoped_lock lock{ s_lock };
			return s_traverse(a_root, a_visitor);
		}
	}

	ModuleSkinnedDecalCrash::ModuleSkinnedDecalCrash() :
		Module("Skinned Decal Crash", &bFixesSkinnedDecalCrash)
	{}

	bool ModuleSkinnedDecalCrash::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		using namespace skinnedDecalCrashDetail;

		// AddDecal's only call of the skinned-decal walk; NG/AE inline the per-shape find-or-create into it.
		const auto site = REL::Relocation<uintptr_t>{ RE::ID::BGSDecalManager::AddDecal, REL::Offset{ 0x118, 0x11F } }.address();
		const auto traverse = REL::Relocation<uintptr_t>{ RE::ID::BSVisit::TraverseScenegraphAVObjects_DoAddDecal }.address();

		if (!RELEX::IsCallTo(site, traverse))
		{
			Skip("AddDecal does not call the skinned-decal walk where expected; unsupported runtime or another plugin patched it"sv);
			return false;
		}

		s_traverse = reinterpret_cast<Traverse_t*>(RELEX::DetourCall(site, reinterpret_cast<uintptr_t>(&HKTraverse)));
		return s_traverse != nullptr;
	}
}
