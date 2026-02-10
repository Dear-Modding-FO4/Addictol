#include <Modules\AdModuleBSPreCulledObjects.h>
#include <AdUtils.h>

#include <Voltek.MemoryManager.h>
#include <unordered_dense/unordered_dense.h>

#include <RE/N/NiAVObject.h>
#include <RE/B/BSTObjectArena.h>
#include <RE/B/BSPreCulledObjects.h>
#include <RE/S/Setting.h>

namespace Addictol
{
	static REX::TOML::Bool<> bPatchesBSPreCulledObjects{ "Patches", "bBSPreCulledObjects", true };

	class BSCullingGroup;

	struct BSPreCulledObjects
	{
	public:
		struct ObjectRecord
		{
		public:
			// members
			RE::NiAVObject* obj{ nullptr };  // 00
			std::uint32_t flags{ 0 };      // 08
		};
		static_assert(sizeof(ObjectRecord) == 0x10);
	};
	static_assert(std::is_empty_v<BSPreCulledObjects>);

	class IDTo3DHandler;

	[[nodiscard]] inline auto SplitID(std::uint32_t a_id) noexcept
		-> std::pair<
		std::uint32_t,  // form id w/o file index
		std::uint8_t>   // file index
	{
		return {
			a_id & 0x00FFFFFF,
			static_cast<std::uint8_t>((a_id >> (8 * 3)) & 0xFF)
		};
	}

	class IDTo3DMap
	{
	public:
		using value_type =
			std::pair<
			std::uint32_t,                   // full form id
			RE::NiPointer<RE::NiAVObject>>;  // object 3D
		using container_type =
			std::vector<
			value_type,
			voltek::allocator<value_type>>;
		// TODO: test Allocator, seems to be fine.

		[[nodiscard]] auto find(std::uint32_t a_id) const noexcept
			-> std::span<const value_type>
		{
			const auto [base, idx] = SplitID(a_id);
			if (idx == 0x00 || idx == 0xFD) [[likely]] {
				const auto it = _special.find(a_id);
				if (it != _special.end()) {
					return { std::addressof(*it), 1 };
				}
			}
			else {
				const auto it = _generic.find(base);
				if (it != _generic.end()) {
					return { it->second.begin(), it->second.end() };
				}
			}

			return {};
		}

		void insert_or_assign(std::uint32_t a_id, RE::NiAVObject* a_obj) noexcept
		{
			const auto idx = SplitID(a_id).second;
			if (idx == 0x00 || idx == 0xFD) [[likely]] {
				insert_special(a_id, a_obj);
			}
			else {
				insert_generic(a_id, a_obj);
			}
		}

		void erase(std::uint32_t a_id) noexcept
		{
			const auto idx = SplitID(a_id).second;
			if (idx == 0x00 || idx == 0xFD) [[likely]] {
				erase_special(a_id);
			}
			else {
				erase_generic(a_id);
			}
		}

	protected:
		friend class IDTo3DHandler;

		IDTo3DMap() = default;
		IDTo3DMap(const IDTo3DMap&) = default;
		IDTo3DMap(IDTo3DMap&&) = default;
		~IDTo3DMap() = default;
		IDTo3DMap& operator=(const IDTo3DMap&) = default;
		IDTo3DMap& operator=(IDTo3DMap&&) = default;

	private:
		struct equal_t
		{
			[[nodiscard]] bool operator()(const value_type& a_value) noexcept
			{
				return a_value.first == val;
			}

			const std::uint32_t val;
		};

		struct less_t
		{
			[[nodiscard]] bool operator()(const value_type& a_lhs, const value_type& a_rhs) noexcept
			{
				return a_lhs.first < a_rhs.first;
			}

			[[nodiscard]] bool operator()(const value_type& a_lhs, std::uint32_t a_rhs) noexcept
			{
				return a_lhs.first < a_rhs;
			}
		};

		static void fixup_container(container_type& a_container) noexcept
		{
			// vanilla iterates from lowest to highest index
			std::sort(a_container.begin(), a_container.end(), less_t{});
		}

		void erase_generic(std::uint32_t a_id) noexcept
		{
			const auto [base, idx] = SplitID(a_id);
			const auto it = _generic.find(base);
			if (it != _generic.end()) [[likely]] {
				auto& objs = it->second;
				const auto rm = std::lower_bound(objs.begin(), objs.end(), a_id, less_t{});
				if (rm != objs.end() && rm->first == a_id) {
					assert(std::count_if(objs.begin(), objs.end(), equal_t{ a_id }) == 1);
					objs.erase(rm);
					if (objs.empty()) {
						_generic.erase(it);
					}
				}
			}
		}

		void erase_special(std::uint32_t a_id) noexcept { _special.erase(a_id); }

		void insert_generic(std::uint32_t a_id, RE::NiAVObject* a_obj)
		{
			const auto base = SplitID(a_id).first;
			auto& objs = [&]() -> container_type& {
				const auto it = _generic.find(base);
				if (it != _generic.end()) {
					return it->second;
				}
				else {
					return _generic
						.emplace(
							base,
							std::initializer_list<value_type>{})
						.first->second;
				}
				}();

			if (const auto it = std::lower_bound(objs.begin(), objs.end(), a_id, less_t{});
				it != objs.end() && it->first == a_id) [[unlikely]] {
				assert(std::count_if(objs.begin(), objs.end(), equal_t{ a_id }) == 1);
				it->second.reset(a_obj);
			}
			else {
				objs.emplace_back(a_id, a_obj);
			}

			fixup_container(objs);
		}

		void insert_special(std::uint32_t a_id, RE::NiAVObject* a_obj)
		{
			const auto it = _special.find(a_id);
			if (it != _special.end()) {
				it->second.reset(a_obj);
			}
			else {
				_special.emplace(a_id, a_obj);
			}
		}

		ankerl::unordered_dense::map<
			std::uint32_t,  // form id w/o file index
			container_type>
			_generic;
		ankerl::unordered_dense::map<
			std::uint32_t,  // full form id
			value_type::second_type>
			_special;
	};

	class IDTo3DHandler
	{
	public:
		using value_type = IDTo3DMap;
		using lock_type = std::mutex;

		class Accessor
		{
		public:
			[[nodiscard]] value_type& operator*() noexcept { return _proxy; }
			[[nodiscard]] const value_type& operator*() const noexcept { return _proxy; }
			[[nodiscard]] value_type* operator->() noexcept { return std::addressof(_proxy); }
			[[nodiscard]] const value_type* operator->() const noexcept { return std::addressof(_proxy); }

		private:
			friend IDTo3DHandler;

			Accessor(lock_type& a_lock, value_type& a_proxy) noexcept :
				_locker(a_lock),
				_proxy(a_proxy)
			{
			}

			std::lock_guard<lock_type> _locker;
			value_type& _proxy;
		};

		IDTo3DHandler(const IDTo3DHandler&) = delete;
		IDTo3DHandler(IDTo3DHandler&&) = delete;
		IDTo3DHandler& operator=(const IDTo3DHandler&) = delete;
		IDTo3DHandler& operator=(IDTo3DHandler&&) = delete;

		[[nodiscard]] static IDTo3DHandler& GetSingleton() noexcept
		{
			static IDTo3DHandler singleton;
			return singleton;
		}

		[[nodiscard]] Accessor Access() noexcept { return { _lock, _value }; }

		// BSCullingGroup::Add
		void AddToCullingGroup(BSCullingGroup& a_self, RE::NiAVObject* a_obj, const RE::NiBound& a_bound, std::uint32_t a_flags) const
		{
			return _addToCullingGroup(a_self, a_obj, a_bound, a_flags);
		}

		// BSPreCulledObjects::CallVisibilityCallbacks
		void CallVisibilityCallbacks() const { return _callVisibilityCallbacks(); }

		// BSPreCulledObjects::pPreCulledDynamicObjectsA
		[[nodiscard]] auto PreCulledDynamicObjects() const noexcept { return _preCulledDynamicObjects; }

		// BSPreCulledObjects::pPreCulledDynamicRainObjectsA
		[[nodiscard]] auto PreCulledDynamicRainObjects() const noexcept { return _preCulledDynamicRainObjects; }

		// BSPreCulledObjects::pPreCulledDynamicShadowObjectsA
		[[nodiscard]] auto PreCulledDynamicShadowObjects() const noexcept { return _preCulledDynamicShadowObjects; }

		// BSPreCulledObjects::PreCulledIDA
		[[nodiscard]] decltype(auto) PreCulledIDs() const noexcept { return _preCulledIDs; }

		// BSPreCulledObjects::PreCulledRainIDA
		[[nodiscard]] decltype(auto) PreCulledRainIDs() const noexcept { return _preCulledRainIDs; }

		// BSPreCulledObjects::PreCulledShadowIDA
		[[nodiscard]] decltype(auto) PreCulledShadowIDs() const noexcept { return _preCulledShadowIDs; }

		// BSPreCulledObjects::RemoveVisibilityCallBackForID
		[[nodiscard]] void RemoveVisibilityCallBackForID(std::uint32_t a_id) const
		{
			return _removeVisibilityCallBackForID(a_id);
		}

		// BSPreCulledObjects::TrackVisibility
		void TrackVisibility(std::uint32_t a_id) const
		{
			return _trackVisibility(a_id);
		}

		// BSPreCulledObjects::bUsePreCulledObjects
		[[nodiscard]] bool UsePreCulledObjects() const noexcept { return _usePreCulledObjects.GetBinary(); }

	private:
		using AddToCullingGroup_t = void(BSCullingGroup&, RE::NiAVObject*, const RE::NiBound&, std::uint32_t);
		using CallVisibilityCallbacks_t = void();
		using RemoveVisibilityCallBackForID_t = void(std::uint32_t);
		using TrackVisibility_t = void(std::uint32_t);

		using PreCulledObjects_t = RE::BSTObjectArena<RE::BSPreCulledObjects::ObjectRecord, RE::BSTObjectArenaScrapAlloc, 512>;
		using PreCulledIDs_t = RE::BSTArray<std::uint32_t>;

		IDTo3DHandler() = default;
		~IDTo3DHandler() = default;

		AddToCullingGroup_t* const _addToCullingGroup{ reinterpret_cast<AddToCullingGroup_t*>(REL::ID{ 1175493, 2275917 }.address()) }; // OG: 1175493
		CallVisibilityCallbacks_t* const _callVisibilityCallbacks{ reinterpret_cast<CallVisibilityCallbacks_t*>(REL::ID{ 887475, 2317332 }.address()) }; // OG: 887475
		RemoveVisibilityCallBackForID_t* const _removeVisibilityCallBackForID{ reinterpret_cast<RemoveVisibilityCallBackForID_t*>(REL::ID{ 136788, 2317308 }.address()) }; // OG: 136788
		TrackVisibility_t* const _trackVisibility{ reinterpret_cast<TrackVisibility_t*>(REL::ID{ 1167260, 2317331 }.address()) }; // OG: 1167260

		const PreCulledObjects_t*& _preCulledDynamicObjects{ *reinterpret_cast<const PreCulledObjects_t**>(REL::ID{ 713154, 2712641 }.address()) }; // OG: 713154
		const PreCulledObjects_t*& _preCulledDynamicRainObjects{ *reinterpret_cast<const PreCulledObjects_t**>(REL::ID{ 274004, 2712643 }.address()) }; // OG: 274004
		const PreCulledObjects_t*& _preCulledDynamicShadowObjects{ *reinterpret_cast<const PreCulledObjects_t**>(REL::ID{ 932019, 2712642 }.address()) }; // OG: 932019

		const PreCulledIDs_t& _preCulledIDs{ *reinterpret_cast<PreCulledIDs_t*>(REL::ID{ 1370583, 2712646 }.address()) }; // OG: 1370583
		const PreCulledIDs_t& _preCulledRainIDs{ *reinterpret_cast<PreCulledIDs_t*>(REL::ID{ 1104804, 2712651 }.address()) }; // OG: 1104804
		const PreCulledIDs_t& _preCulledShadowIDs{ *reinterpret_cast<PreCulledIDs_t*>(REL::ID{ 446402, 2712649 }.address()) }; // OG: 446402

		const RE::Setting& _usePreCulledObjects{ *reinterpret_cast<RE::Setting*>(REL::ID{ 1252712, 2677863, 4784531 }.address()) }; // OG: 1252712, NG: 2677863

		lock_type _lock;    // replaces BSPreCulledObjects::IDMapLock
		value_type _value;  // replaces BSPreCulledObjects::IDto3Dmap
	};

	template <class IDCallback, class ObjectCallback>
	__forceinline void DoAddToCullingGroup(
		const RE::BSTArray<std::uint32_t>& a_ids,
		IDCallback a_idFn,
		const RE::BSTObjectArena<RE::BSPreCulledObjects::ObjectRecord, RE::BSTObjectArenaScrapAlloc, 512>* a_objects,
		ObjectCallback a_objFn)  //
		requires((
	std::invocable<IDCallback, RE::NiAVObject&, std::uint32_t>&&
		std::invocable<ObjectCallback, const RE::BSPreCulledObjects::ObjectRecord&>))
	{
		{
			const auto token = IDTo3DHandler::GetSingleton().Access();
			for (const auto needle : a_ids) {
				for (const auto& [full, obj] : token->find(needle)) {
					assert(obj != nullptr);
					a_idFn(*obj, full);
				}
			}
		}

		if (a_objects) {
			for (const auto& record : *a_objects) {
				a_objFn(record);
			}
		}
	}

	inline void AddToCullingGroup(BSCullingGroup& a_cullingGroup, BSCullingGroup& a_nonShadowCullingGroup)
	{
		auto& handler = IDTo3DHandler::GetSingleton();
		DoAddToCullingGroup(
			handler.PreCulledIDs(),
			[&](RE::NiAVObject& a_obj, std::uint32_t a_id) {
				if (!a_obj.GetAppCulled()) {
					auto& group = a_obj.ShadowCaster() ? a_cullingGroup : a_nonShadowCullingGroup;
					handler.AddToCullingGroup(group, std::addressof(a_obj), a_obj.worldBound, 0);
					handler.TrackVisibility(a_id);
				}
			},
			handler.PreCulledDynamicObjects(),
			[&](const RE::BSPreCulledObjects::ObjectRecord& a_record) {
				if (a_record.obj->ShadowCaster()) {
					handler.AddToCullingGroup(
						a_cullingGroup,
						a_record.obj,
						a_record.obj->worldBound,
						a_record.flags);
				}
			});
		handler.CallVisibilityCallbacks();
	}

	inline void AddToRainCullingGroup(BSCullingGroup& a_cullingGroup)
	{
		auto& handler = IDTo3DHandler::GetSingleton();
		DoAddToCullingGroup(
			handler.PreCulledRainIDs(),
			[&](RE::NiAVObject& a_obj, std::uint32_t) {
				if (!a_obj.GetAppCulled()) {
					handler.AddToCullingGroup(a_cullingGroup, std::addressof(a_obj), a_obj.worldBound, 0);
				}
			},
			handler.PreCulledDynamicRainObjects(),
			[&](const RE::BSPreCulledObjects::ObjectRecord& a_record) {
				handler.AddToCullingGroup(
					a_cullingGroup,
					a_record.obj,
					a_record.obj->worldBound,
					a_record.flags);
			});
	}

	inline void AddToShadowCullingGroup(BSCullingGroup& a_cullingGroup)
	{
		auto& handler = IDTo3DHandler::GetSingleton();
		DoAddToCullingGroup(
			handler.PreCulledShadowIDs(),
			[&](RE::NiAVObject& a_obj, std::uint32_t) {
				// !a_obj.GetAppCulled() && a_obj.ShadowCaster()
				if ((a_obj.GetFlags() & 0x100'0000'0001) == 0) {
					handler.AddToCullingGroup(a_cullingGroup, std::addressof(a_obj), a_obj.worldBound, 0);
				}
			},
			handler.PreCulledDynamicShadowObjects(),
			[&](const RE::BSPreCulledObjects::ObjectRecord& a_record) {
				if (a_record.obj->ShadowCaster()) {
					handler.AddToCullingGroup(
						a_cullingGroup,
						a_record.obj,
						a_record.obj->worldBound,
						a_record.flags);
				}
			});
	}

	inline void UpdateIDto3DMap(std::uint32_t a_id, RE::NiAVObject* a_obj)
	{
		auto& handler = IDTo3DHandler::GetSingleton();
		if (handler.UsePreCulledObjects()) {
			auto token = handler.Access();
			if (a_obj) {
				token->insert_or_assign(a_id, a_obj);
			}
			else {
				token->erase(a_id);
				handler.RemoveVisibilityCallBackForID(a_id);
			}
		}
	}

	inline void WriteAddToCullingGroup()
	{
		const auto target = REL::ID{ 997287, 2317327 }.address(); // OG: 997287, Size: 0x278
		const auto size = REL::Offset{ 0x278, 0x4D9 }.offset();
		REL::WriteSafeFill(target, REL::INT3, size);
		RELEX::DetourJump(target, reinterpret_cast<std::uintptr_t>(AddToCullingGroup));
	}

	inline void WriteAddToRainCullingGroup()
	{
		const auto target = REL::ID{ 410310, 2317329 }.address(); // OG: 410310, Size: 0x218
		const auto size = REL::Offset{ 0x218, 0x25F }.offset();
		REL::WriteSafeFill(target, REL::INT3, size);
		RELEX::DetourJump(target, reinterpret_cast<std::uintptr_t>(AddToRainCullingGroup));
	}

	inline void WriteAddToShadowCullingGroup()
	{
		const auto target = REL::ID{ 1142692, 2317328 }.address(); // OG: 1142692, Size: 0x259
		const auto size = REL::Offset{ 0x259, 0x290 }.offset();
		REL::WriteSafeFill(target, REL::INT3, size);
		RELEX::DetourJump(target, reinterpret_cast<std::uintptr_t>(AddToShadowCullingGroup));
	}

	inline void WriteUpdateIDto3DMap()
	{
		const auto target = REL::ID{ 1162647, 2317305 }.address(); // OG: 1162647, Size: 0x1AA
		const auto size = REL::Offset{ 0x1AA, 0x4C3 }.offset();
		REL::WriteSafeFill(target, REL::INT3, size);
		RELEX::DetourJump(target, reinterpret_cast<std::uintptr_t>(UpdateIDto3DMap));
	}

	ModuleBSPreCulledObjects::ModuleBSPreCulledObjects() :
		Module("Module BSPreCulledObjects", &bPatchesBSPreCulledObjects)
	{}

	bool ModuleBSPreCulledObjects::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleBSPreCulledObjects::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		WriteAddToCullingGroup();
		WriteAddToRainCullingGroup();
		WriteAddToShadowCullingGroup();
		WriteUpdateIDto3DMap();

		return true;
	}

	bool ModuleBSPreCulledObjects::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleBSPreCulledObjects::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}