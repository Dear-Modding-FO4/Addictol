#include <Modules/AdModuleFasterWorkshop.h>
#include <Core/AdUtils.h>

#include <RE/B/BGSConstructibleObject.h>
#include <RE/B/BGSDefaultObjectManager.h>
#include <RE/B/BGSKeyword.h>
#include <RE/B/BGSListForm.h>
#include <RE/K/KeywordType.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESFormUtil.h>
#include <RE/W/Workshop.h>

#include <xbyak/xbyak.h>

#define AD_NOMESSAGE_FASTERWORKSHOP 1

namespace Addictol
{
	namespace fasterWorkshopDetail
	{
		// resolved lazily, the offset is OG-only
		static std::uintptr_t LeafNodeTargetOG() noexcept
		{
			static REL::Relocation target{ RE::ID::Workshop::WorkshopMenuNode::BuildChildNodes, REL::Offset{ 0x1EF } };
			return target.address();
		}

		// Constructible Object Map
		std::unordered_map<const RE::BGSKeyword*, std::vector<const RE::BGSConstructibleObject*>> g_cobjMap;
		// COBJs without filter keywords; vanilla lists them under the default filter
		std::vector<const RE::BGSConstructibleObject*> g_uncategorized;
		const RE::TESForm* g_defaultFilter{};

		// ---- Game Functions ---- //

		inline static const RE::BGSKeyword* GetKeywordByIndex(RE::KeywordType a_type, std::uint16_t a_index) noexcept
		{
			using func_t = decltype(&GetKeywordByIndex);
			static REL::Relocation<func_t> func{ RE::ID::BGSKeyword::GetTypedKeywordByIndex };
			return func(a_type, a_index);
		}

		inline static void TryAddLeafNodeOG(void* unk1, const RE::BGSConstructibleObject* cobj) noexcept
		{
			using func_t = decltype(&TryAddLeafNodeOG);
			static REL::Relocation<func_t> func{ RE::ID::Workshop::WorkshopMenuNode::TryAddRecipe };
			return func(unk1, cobj);
		}

		// the filter vanilla maps to null, which selects keywordless recipes
		static const RE::TESForm* GetDefaultFilter() noexcept
		{
			constexpr auto object = RE::DEFAULT_OBJECT::kWorkshopMiscItemKeyword;
			if (RELEX::IsRuntimeOG())
				return RE::BGSDefaultObjectManager::GetSingleton()->GetDefaultObject(object);

			// NG/AE shifted this default object up by one
			using func_t = RE::TESForm* (*)(std::uint32_t);
			static REL::Relocation<func_t> func{ RE::ID::BGSDefaultObjectManager::GetDefaultObject };
			return func(std::to_underlying(object) + 1);
		}

		// ---- Map Functions ---- //

		inline static bool ClearBuiltMap() noexcept
		{
#if !AD_NOMESSAGE_FASTERWORKSHOP
			REX::INFO("FasterWorkshop: Clearing COBJ Map"sv);
#endif

			g_cobjMap.clear();
			g_cobjMap.reserve(3000);
			g_uncategorized.clear();
			g_defaultFilter = nullptr;

			return true;
		}

		inline static auto TryBuildMap() noexcept -> void
		{
			// Only Build if necessary
			if (g_cobjMap.empty())
			{
#if !AD_NOMESSAGE_FASTERWORKSHOP
				REX::INFO("FasterWorkshop: Building Map..."sv);
#endif

				// Data Handler
				const auto dataHandler = RE::TESDataHandler::GetSingleton();

				if (!dataHandler)
				{
#if !AD_NOMESSAGE_FASTERWORKSHOP
					REX::INFO("FasterWorkshop: Invalid DataHandler"sv);
#endif
					return;
				}

				// Objects Array
				auto& objectArray = dataHandler->GetFormArray<RE::BGSConstructibleObject>();

				if (objectArray.empty())
				{
#if !AD_NOMESSAGE_FASTERWORKSHOP
					REX::INFO("FasterWorkshop: Empty Objects Array."sv);
#endif
					return;
				}

#if !AD_NOMESSAGE_FASTERWORKSHOP
				REX::INFO("FasterWorkshop: Objects Array Size is {}"sv, objectArray.size());
#endif

				if (objectArray.size() > 50000)
					REX::WARN("FasterWorkshop: Warning, Objects Array is larger than 50,000!"sv);

				// Objects Loop
				for (const auto& object : objectArray)
				{
					if (!object || !object->createdItem)
						continue;

					// Use only 1 Keyword from Possibly Corrupt Objects
					uint32_t count = object->filterKeywords.size;
					if (count > 32)
						count = 1;

					// Recipe Filter
					auto recipeFiltered{ std::span{object->filterKeywords.array, count} };

					if (recipeFiltered.empty())
					{
						g_uncategorized.push_back(object);
						continue;
					}

					for (int i{ 0 }; i < recipeFiltered.size(); ++i)
					{
						if ((recipeFiltered.data() + i) == nullptr)
						{
#if !AD_NOMESSAGE_FASTERWORKSHOP
							REX::INFO("FasterWorkshop: recipeFiltered.data() + {} is Null."sv, i);
#endif
							continue;
						}

						auto& keywordValue{ recipeFiltered[i] };
						if (const RE::BGSKeyword* keyword{ GetKeywordByIndex(RE::KeywordType::kRecipeFilter, keywordValue.keywordIndex) }; keyword)
						{
							// a repeated keyword would list the recipe twice
							auto& bucket = g_cobjMap[keyword];
							if (bucket.empty() || bucket.back() != object)
								bucket.push_back(object);
						}
					}
				}

				g_defaultFilter = GetDefaultFilter();

#if !AD_NOMESSAGE_FASTERWORKSHOP
				REX::INFO("FasterWorkshop: Map Built. {} Map Key Elements. {} Total COBJ Elements Processed."sv, g_cobjMap.size(), objectArray.size());
#endif

				std::size_t total{};
				for (const auto& [key, value] : g_cobjMap)
				{
					total += value.size();
				}

#if !AD_NOMESSAGE_FASTERWORKSHOP
				REX::INFO("FasterWorkshop: Total COBJ Elements in Map: {}"sv, total);
#endif
			}
		}

		// ---- Recipe Functions ---- //

		// visits the recipes vanilla tests for a_filter, with the keyword it tests them against
		template <class F>
		static void ForEachCandidate(const RE::TESForm* a_filter, F&& a_func) noexcept
		{
			TryBuildMap();

			if (a_filter && a_filter == g_defaultFilter)
			{
				for (auto* cobj : g_uncategorized)
				{
					if (!a_func(cobj, nullptr))
						return;
				}
				return;
			}

			auto it = g_cobjMap.find(static_cast<const RE::BGSKeyword*>(a_filter));
			if (it == g_cobjMap.end())
				return;

			for (auto* cobj : it->second)
			{
				if (!a_func(cobj, const_cast<RE::BGSKeyword*>(it->first)))
					return;
			}
		}

		// a_node is the WorkshopMenuNode, whose first member is its filter keyword
		bool CachedCheckForValidChildren(void* a_node, const RE::BGSListForm* a_formList) noexcept
		{
			if (!a_formList)
				return false;

			const auto nodeFilter = *static_cast<RE::TESForm**>(a_node);
			for (auto form : a_formList->arrayOfForms)
			{
				if (!form)
					continue;

				if (form->formType == RE::ENUM_FORM_ID::kFLST)
				{
					if (CachedCheckForValidChildren(a_node, reinterpret_cast<RE::BGSListForm*>(form)))
						return true;
				}
				else if (form->formType == RE::ENUM_FORM_ID::kKYWD)
				{
					if (form == nodeFilter || form->IsDeleted())
						continue;

					bool found{};
					ForEachCandidate(form, [&](const RE::BGSConstructibleObject* a_cobj, RE::BGSKeyword* a_keyword) {
						found = RE::Workshop::WorkshopCanShowRecipe(const_cast<RE::BGSConstructibleObject*>(a_cobj), a_keyword);
						return !found;
					});

					if (found)
						return true;
				}
			}

			return false;
		}

		// ---- Leaf Node Functions ---- //

		// -- OG -- //

		inline static void HandlerLeafNode(void* unk1, const RE::BGSKeyword* keyword)
		{
			// the OG callback maps the default filter to null itself
			ForEachCandidate(keyword, [&](const RE::BGSConstructibleObject* a_cobj, RE::BGSKeyword*) {
				TryAddLeafNodeOG(unk1, a_cobj);
				return true;
			});
		}

		struct LeafNodePatch : Xbyak::CodeGenerator
		{
			LeafNodePatch() : Xbyak::CodeGenerator()
			{
				Xbyak::Label continueLabel, handler;

				lea(rcx, ptr[rbp - 0x28]);
				mov(rdx, ptr[rbp - 0x28]);
				mov(rdx, ptr[rdx]);
				call(ptr[rip + handler]);
				jmp(ptr[rip + continueLabel]);

				L(continueLabel);
				dq(LeafNodeTargetOG() + 0x23);

				L(handler);
				dq((uintptr_t)&HandlerLeafNode);
			}
		};

		// -- NG / AE -- //

		struct Workshop__Workbench__Node
		{
			int64_t* a1;
			int64_t* a2;
		};

		struct Workshop__Workbench__ListNode
		{
			Workshop__Workbench__Node* a1;
			const RE::BGSConstructibleObject* recipe;
		};

		struct Workshop__Workbench__FilterNode
		{
			RE::BGSKeyword* keyword;
		};

		struct Workshop__Workbench__MenuNode
		{
			Workshop__Workbench__FilterNode* filterNode;
			bool* a2;	// initialized ???
			Workshop__Workbench__Node node;
		};

		using TWorkshop__Workbench__AddRecipe = void (*)(Workshop__Workbench__Node*, const RE::BGSConstructibleObject*,
			RE::TESForm*, const RE::BGSConstructibleObject*, RE::TESForm*);
		TWorkshop__Workbench__AddRecipe Workshop__Workbench__AddRecipe;

		static void Workshop__Workbench__ForEachInListAddRecipe(Workshop__Workbench__ListNode& a_node, const RE::BGSListForm* a_formList) noexcept
		{
			auto createdRecipe = a_node.recipe;
			for (auto createdItem : a_formList->arrayOfForms)
			{
				if (createdItem->GetFormType() != RE::ENUM_FORM_ID::kKYWD)
					Workshop__Workbench__AddRecipe(a_node.a1, createdRecipe, nullptr, createdRecipe, createdItem);
				else
					Workshop__Workbench__AddRecipe(a_node.a1, createdRecipe, createdItem, createdRecipe, nullptr);
			}
		}

		static void Workshop__Workbench__TryAddRecipe(Workshop__Workbench__MenuNode* a_menuNode, const RE::BGSConstructibleObject* a_formRecipe,
			RE::BGSKeyword* a_keyword) noexcept
		{
			auto filter = a_menuNode->filterNode->keyword;
			if (!a_formRecipe || !filter || filter->IsDeleted())
				return;

			if (RE::Workshop::WorkshopCanShowRecipe(const_cast<RE::BGSConstructibleObject*>(a_formRecipe), a_keyword))
			{
				*a_menuNode->a2 = true;
				Workshop__Workbench__Node node{ a_menuNode->node };
				auto createdItem = a_formRecipe->createdItem;
				if (createdItem && createdItem->formType == RE::ENUM_FORM_ID::kFLST)
				{
					Workshop__Workbench__ListNode listNode{ &node, a_formRecipe };
					Workshop__Workbench__ForEachInListAddRecipe(listNode, reinterpret_cast<RE::BGSListForm*>(createdItem));
				}
				else
					Workshop__Workbench__AddRecipe(&node, a_formRecipe, nullptr, nullptr, createdItem);
			}
		}

		// a_typeForm == always RE::ENUM_FORM_ID::kCOBJ
		static void Workshop__Workbench__StoreAll([[maybe_unused]] RE::TESDataHandler* a_dataHandler,
			[[maybe_unused]] const REX::EnumSet<RE::ENUM_FORM_ID, uint8_t> a_typeForm, Workshop__Workbench__MenuNode* a_menuNode) noexcept
		{
			ForEachCandidate(a_menuNode->filterNode->keyword, [&](const RE::BGSConstructibleObject* a_cobj, RE::BGSKeyword* a_keyword) {
				Workshop__Workbench__TryAddRecipe(a_menuNode, a_cobj, a_keyword);
				return true;
			});

			/*auto& arrRecipeForm = a_dataHandler->GetFormArray<RE::BGSConstructibleObject>();
			for (auto formRecipe : arrRecipeForm)
				Workshop__Workbench__TryAddRecipe(a_menuNode, formRecipe);*/
		}
	}

	ModuleFasterWorkshop::ModuleFasterWorkshop() :
		Module("Faster Workshop", &bPatchesFasterWorkshop)
	{}

	bool ModuleFasterWorkshop::DoQuery() const noexcept
	{
		if (IsModDLLPresent("FasterWorkshopAE.dll"))
		{
			Skip("standalone 'FasterWorkshopAE.dll' is installed"sv);
			return false;
		}

		if (IsModDLLPresent("FasterWorkshopNG_F4SE.dll"))
		{
			Skip("standalone 'FasterWorkshopNG_F4SE.dll' is installed"sv);
			return false;
		}

		if (IsModDLLPresent("faster_workshop.dll"))
		{
			Skip("standalone 'faster_workshop.dll' is installed"sv);
			return false;
		}

		return true;
	}

	bool ModuleFasterWorkshop::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kGameDataReady)
			fasterWorkshopDetail::ClearBuiltMap();
		else
		{
			REL::Relocation HookCheckForValidChildrenTarget{ RE::ID::Workshop::WorkshopMenuNode::BuildChildNodes, REL::Offset{ 0x51, 0x4F } };
			REL::Relocation HookIconLoadLagTarget{ RE::ID::WorkshopMenu::ProcessMessage, REL::Offset{ 0x3A5, 0x39F, 0x3A0 } };

			// Workshop Lag Fix
			RELEX::DetourCall(HookCheckForValidChildrenTarget.address(), (uintptr_t)&fasterWorkshopDetail::CachedCheckForValidChildren);

			if (RELEX::IsRuntimeOG())
				RELEX::XbyakJump<fasterWorkshopDetail::LeafNodePatch>(fasterWorkshopDetail::LeafNodeTargetOG());
			else
			{
				*(uintptr_t*)&fasterWorkshopDetail::Workshop__Workbench__AddRecipe = REL::Relocation{ RE::ID::Workshop::WorkshopMenuNode::AddRecipe }.address();

				RELEX::DetourJump(REL::Relocation{ RE::ID::Workshop::WorkshopMenuNode::AddRecipesForFilter }.address(),
					(uintptr_t)&fasterWorkshopDetail::Workshop__Workbench__StoreAll);
			}

			// Icon Lag Fix
			if (RELEX::IsRuntimeOG())
				RELEX::WriteSafe(HookIconLoadLagTarget.address(), { 0x90, 0x90, 0x90 });
			else
				RELEX::WriteSafe(HookIconLoadLagTarget.address(), { 0x66, 0x90 });
		}

		return true;
	}
}