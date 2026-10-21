// Ported with permission from my goat doodlum (github.com/doodlum/FO4LightingFixes).
#include <Modules/AdModuleLightOrder.h>
#include <Core/AdUtils.h>

#include <RE/B/BSFadeNode.h>
#include <RE/B/BSLight.h>
#include <RE/B/BSPreCulledObjects.h>
#include <RE/B/BSShaderPropertyLightData.h>
#include <RE/S/ShadowSceneNode.h>

// Previs discards a fade node's light list right after building it, so lit
// objects (most visibly glass) render with only the sun.
//
// Each frame BSShaderPropertyLightData is rebuilt and every light is torn out of
// every node list it is in. With previs off the teardown runs first; with previs
// on the rebuild is flushed before ShadowSceneNode::UpdateQueuedLight, so the
// teardown discards the list that was just built.
//
// Fix: snapshot a node's list when the teardown first touches it, then once the
// driver returns re-add the removed lights and restore the snapshot order. The
// gather takes only the first few entries, so order matters as much as
// membership. Removals are queued and drained on one thread, so state is
// thread-local and unlocked.

namespace Addictol
{
	namespace lightOrderDetail
	{
		using RemoveLight_t = uintptr_t (*)(RE::BSShaderPropertyLightData*, RE::BSLight*);
		using UpdateQueuedLight_t = uintptr_t (*)(RE::ShadowSceneNode*, RE::BSLight*);
		using AddFadeNodeLocked_t = void (*)(RE::BSLight*, RE::BSFadeNode*);

		constexpr size_t kMaxLights = 32;
		constexpr size_t kMaxPending = 512;

		struct Pending
		{
			RE::BSFadeNode* node{ nullptr };
			std::array<RE::BSLight*, kMaxLights> lights{};
			uint32_t count{ 0 };
			uint32_t removed{ 0 };  // bitmask over lights
		};

		struct PendingSet
		{
			std::array<Pending, kMaxPending> items{};
			size_t count{ 0 };
		};

		static RemoveLight_t s_removeLight{ nullptr };
		static UpdateQueuedLight_t s_updateQueuedLight{ nullptr };
		static AddFadeNodeLocked_t s_addFadeNodeLocked{ nullptr };
		static std::atomic<bool> s_warnedFull{ false };

		// Only the thread running the teardown driver pays for the ~140 KB set.
		static thread_local std::unique_ptr<PendingSet> s_pending;
		static thread_local uint32_t s_depth{ 0 };

		// Every list the teardown walks is embedded in a BSFadeNode.
		[[nodiscard]] static RE::BSFadeNode* NodeOf(RE::BSShaderPropertyLightData* a_list) noexcept
		{
			return reinterpret_cast<RE::BSFadeNode*>(
				reinterpret_cast<std::byte*>(a_list) - offsetof(RE::BSFadeNode, lightData));
		}

		[[nodiscard]] static Pending* Find(const RE::BSFadeNode* a_node) noexcept
		{
			auto& set = *s_pending;
			for (size_t i = 0; i < set.count; ++i)
			{
				if (set.items[i].node == a_node)
					return &set.items[i];
			}
			return nullptr;
		}

		[[nodiscard]] static Pending* Open(RE::BSFadeNode* a_node) noexcept
		{
			auto& set = *s_pending;
			if (set.count >= kMaxPending)
			{
				if (!s_warnedFull.exchange(true))
					REX::WARN("Light Order: a light touched more than {} nodes; the rest keep vanilla behavior"sv, kMaxPending);
				return nullptr;
			}

			const auto& list = a_node->lightData.lightList;
			if (list.empty())
				return nullptr;

			// Both remove call sites test refCount == 1 afterward, so nodes can die
			// mid-walk; only hold nodes at 2+ so that test is unchanged.
			if (a_node->refCount <= 1)
				return nullptr;

			auto& p = set.items[set.count];
			p = {};
			p.node = a_node;
			p.count = static_cast<uint32_t>(std::min<size_t>(list.size(), kMaxLights));
			std::copy_n(list.begin(), p.count, p.lights.begin());

			a_node->IncRefCount();
			++set.count;
			return &p;
		}

		// Engine adds append to a closed list; permute back to snapshot order.
		static void RestoreOrder(const Pending& a_pending) noexcept
		{
			auto& list = a_pending.node->lightData.lightList;
			const auto count = list.size();
			if (count == 0 || count > kMaxLights)
				return;

			std::array<RE::BSLight*, kMaxLights> ordered{};
			size_t n = 0;
			const auto has = [](auto a_begin, auto a_end, RE::BSLight* a_light) {
				return std::find(a_begin, a_end, a_light) != a_end;
			};

			for (uint32_t i = 0; i < a_pending.count; ++i)
			{
				auto* light = a_pending.lights[i];
				if (light && has(list.begin(), list.end(), light))
					ordered[n++] = light;
			}
			for (auto* light : list)
			{
				if (light && n < kMaxLights && !has(ordered.begin(), ordered.begin() + n, light))
					ordered[n++] = light;
			}
			if (n != count)
				return;  // not a full permutation; leave it alone

			std::copy_n(ordered.begin(), n, list.begin());
		}

		static uintptr_t HKRemoveLight(RE::BSShaderPropertyLightData* a_list, RE::BSLight* a_light) noexcept
		{
			// Only teardown-driver removals under previs discard a fresh list;
			// the app-culled path also reaches here and must stay removed.
			if (s_depth != 0 && s_pending && a_list && a_light && RE::BSPreCulledObjects::QEnabled())
			{
				auto* node = NodeOf(a_list);
				auto* p = Find(node);
				if (!p)
					p = Open(node);
				if (p)
				{
					for (uint32_t i = 0; i < p->count; ++i)
					{
						if (p->lights[i] != a_light)
							continue;
						if ((p->removed & (1u << i)) == 0)
						{
							p->removed |= 1u << i;
							a_light->IncRefCount();
						}
						break;
					}
				}
			}
			return s_removeLight(a_list, a_light);
		}

		static uintptr_t HKUpdateQueuedLight(RE::ShadowSceneNode* a_scene, RE::BSLight* a_light) noexcept
		{
			if (!s_pending)
			{
				s_pending.reset(new (std::nothrow) PendingSet{});
				if (!s_pending)
					return s_updateQueuedLight(a_scene, a_light);
			}

			++s_depth;
			const auto result = s_updateQueuedLight(a_scene, a_light);
			if (--s_depth != 0)
				return result;  // an outer frame owns the drain

			// Re-adding inside the driver would append to the list it is walking.
			auto& set = *s_pending;
			for (size_t i = 0; i < set.count; ++i)
			{
				const auto& p = set.items[i];
				for (uint32_t j = 0; j < p.count; ++j)
				{
					if ((p.removed & (1u << j)) == 0)
						continue;
					s_addFadeNodeLocked(p.lights[j], p.node);
					p.lights[j]->DecRefCount();
				}
				if (p.removed != 0)
					RestoreOrder(p);
				p.node->DecRefCount();
			}
			set.count = 0;
			return result;
		}

		[[nodiscard]] static bool ValidateTargets(uintptr_t a_remove, uintptr_t a_update, uintptr_t a_add, uintptr_t a_qEnabled) noexcept
		{
			const bool og = RELEX::IsRuntimeOG();
			const bool remove = og ?
				RELEX::Validate(a_remove, { 0x48, 0x89, 0x54, 0x24, 0x10, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x83, 0x39, 0xFF, 0x48, 0x8B, 0xD9 }) :
				RELEX::Validate(a_remove, { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x56, 0x48, 0x83, 0xEC, 0x30, 0xBE, 0xFF, 0xFF, 0xFF, 0xFF, 0x4C });
			const bool update = og ?
				RELEX::Validate(a_update, { 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x41, 0x54, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x44, 0x8B }) :
				RELEX::Validate(a_update, { 0x48, 0x89, 0x54, 0x24, 0x10, 0x56, 0x57, 0x41, 0x54, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x38, 0x44 });
			// AddFadeNodeLocked: spin on light+0x160, then call AddFadeNode.
			const bool add = RELEX::Validate(a_add, {
				0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
				0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0xBE, 0x01, 0x00, 0x00, 0x00, 0x33, 0xC0, 0xF0, 0x0F, 0xB1, 0xB1,
				0x60, 0x01, 0x00, 0x00
			});
			// QEnabled: cmp byte ptr [rip+disp], 0; je; cmp byte ptr [rip+disp], 0
			const bool qEnabled =
				RELEX::Validate(a_qEnabled, { 0x80, 0x3D }) &&
				RELEX::Validate(a_qEnabled + 0x6, { 0x00, 0x74, 0x15, 0x80, 0x3D });
			return remove && update && add && qEnabled;
		}
	}

	ModuleLightOrder::ModuleLightOrder() :
		Module("Light Order", &bFixesLightOrder)
	{}

	bool ModuleLightOrder::DoQuery() const noexcept
	{
		if (IsModDLLPresent("LightingFixes.dll"))
		{
			Skip("standalone 'LightingFixes.dll' is installed"sv);
			return false;
		}

		return true;
	}

	bool ModuleLightOrder::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		// BSShaderPropertyLightData::RemoveLight: OG 0x28B9B50, NG 0x20E3220, AE 0x2240330.
		const auto remove = RE::ID::BSShaderPropertyLightData::RemoveLight.address();
		// ShadowSceneNode::UpdateQueuedLight: OG 0x2810960, NG 0x205DA80, AE 0x21BAC20.
		const auto update = RE::ID::ShadowSceneNode::UpdateQueuedLight.address();
		// BSLight::AddFadeNodeLocked: OG 0x285B860, NG 0x20A0F70, AE 0x21FE110.
		const auto add = RE::ID::BSLight::AddFadeNodeLocked.address();
		// BSPreCulledObjects::QEnabled: OG 0x2809E30, NG 0x2051380, AE 0x21AE520.
		const auto qEnabled = RE::ID::BSPreCulledObjects::QEnabled.address();

		if (!lightOrderDetail::ValidateTargets(remove, update, add, qEnabled))
		{
			Skip("unexpected bytes or conflicting patch"sv);
			return false;
		}

		lightOrderDetail::s_addFadeNodeLocked = reinterpret_cast<lightOrderDetail::AddFadeNodeLocked_t>(add);
		lightOrderDetail::s_removeLight = reinterpret_cast<lightOrderDetail::RemoveLight_t>(
			RELEX::DetourJump(remove, reinterpret_cast<uintptr_t>(&lightOrderDetail::HKRemoveLight)));
		// The remove hook only queues under a teardown frame, so it is inert alone.
		lightOrderDetail::s_updateQueuedLight = reinterpret_cast<lightOrderDetail::UpdateQueuedLight_t>(
			RELEX::DetourJump(update, reinterpret_cast<uintptr_t>(&lightOrderDetail::HKUpdateQueuedLight)));

		return lightOrderDetail::s_removeLight && lightOrderDetail::s_updateQueuedLight;
	}
}
