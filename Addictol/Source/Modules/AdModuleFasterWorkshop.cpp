#include <Modules/AdModuleFasterWorkshop.h>
#include <AdUtils.h>

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
	static REX::TOML::Bool<> bPatchesFasterWorkshop{ "Patches"sv, "bFasterWorkshop"sv, true };

	namespace fasterWorkshopDetail
	{
		// resolved lazily, the id is OG-only and would hard-fail at static init on NG/AE
		static std::uintptr_t LeafNodeTargetOG() noexcept
		{
			static REL::Relocation target{ REL::ID{ 934716 }, REL::Offset{ 0x1EF } };
			return target.address();
		}

		// Constructible Object Map
		std::unordered_map<const RE::BGSKeyword*, std::vector<const RE::BGSConstructibleObject*>> g_cobjMap;

		// ---- Game Functions ---- //

		inline static const RE::BGSKeyword* GetKeywordByIndex(RE::KeywordType a_type, std::uint16_t a_index) noexcept
		{
			using func_t = decltype(&GetKeywordByIndex);
			static REL::Relocation<func_t> func{ REL::ID{ 952856, 2206423 } };
			return func(a_type, a_index);
		}

		inline static void TryAddLeafNodeOG(void* unk1, const RE::BGSConstructibleObject* cobj) noexcept
		{
			using func_t = decltype(&TryAddLeafNodeOG);
			static REL::Relocation<func_t> func{ REL::ID{ 281929 } };
			return func(unk1, cobj);
		}

		// ---- Map Functions ---- //

		inline static bool ClearBuiltMap() noexcept
		{
#if !AD_NOMESSAGE_FASTERWORKSHOP
			REX::INFO("FasterWorkshop: Clearing COBJ Map"sv);
#endif

			g_cobjMap.clear();
			g_cobjMap.reserve(3000);

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
						// Keyword Count is Empty
						continue;

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
							g_cobjMap[keyword].push_back(object);
					}
				}

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

		inline static auto CachedCanShowRecipe(RE::BGSConstructibleObject* obj, RE::BGSKeyword* keyword) noexcept -> bool
		{
			TryBuildMap();

			auto it = g_cobjMap.find(keyword);
			if (it != g_cobjMap.end())
			{
				for (auto* cobj : it->second)
				{
					if (obj == cobj && RE::Workshop::WorkshopCanShowRecipe(obj, keyword))
						return true;
				}
			}

			return false;
		}

		bool CachedCheckForValidChildren(void* ctx, const RE::BGSListForm* a_formList) noexcept
		{
			if (!a_formList)
				return false;

			for (auto form : a_formList->arrayOfForms)
			{
				if (!form)
					continue;

				if (form->formType == RE::ENUM_FORM_ID::kFLST)
				{
					if (CachedCheckForValidChildren(ctx, reinterpret_cast<RE::BGSListForm*>(form)))
						return true;
				}
				else if (form->formType == RE::ENUM_FORM_ID::kKYWD)
				{
					if (form == ctx)
						continue;

					TryBuildMap();

					auto keyword = reinterpret_cast<RE::BGSKeyword*>(form);
					auto it = g_cobjMap.find(keyword);
					if (keyword && it != g_cobjMap.end())
					{
						for (auto* cobj : it->second)
						{
							if (RE::Workshop::WorkshopCanShowRecipe(const_cast<RE::BGSConstructibleObject*>(cobj), keyword))
								return true;
						}
					}
				}
			}

			return false;
		}

		// ---- Leaf Node Functions ---- //

		// -- OG -- //

		inline static void HandlerLeafNode(void* unk1, const RE::BGSKeyword* keyword)
		{
			TryBuildMap();

			auto it = g_cobjMap.find(keyword);
			if (it != g_cobjMap.end())
			{
				for (auto* cobj : it->second)
				{
					TryAddLeafNodeOG(unk1, cobj);
				}
			}
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

		using TGetDefaultObjectFromDefaultManager = RE::TESForm* (*)(std::uint32_t);
		TGetDefaultObjectFromDefaultManager GetDefaultObjectFromDefaultManager;

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

		static void Workshop__Workbench__TryAddRecipe(Workshop__Workbench__MenuNode* a_menuNode, const RE::BGSConstructibleObject* a_formRecipe) noexcept
		{
			auto filter = a_menuNode->filterNode->keyword;
			if (!a_formRecipe || !filter || filter->IsDeleted())
				return;

			// for OG 0x17E, but AE 0x17F, clib wrong
			auto defaultObject = GetDefaultObjectFromDefaultManager(0x17F);
			if (CachedCanShowRecipe(const_cast<RE::BGSConstructibleObject*>(a_formRecipe), (defaultObject == filter ? nullptr : filter)))
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
			auto filter = a_menuNode->filterNode->keyword;
			TryBuildMap();

			auto it = g_cobjMap.find(filter);
			if (it != g_cobjMap.end())
			{
				for (auto* cobj : it->second)
				{
					Workshop__Workbench__TryAddRecipe(a_menuNode, cobj);
				}
			}

			/*auto& arrRecipeForm = a_dataHandler->GetFormArray<RE::BGSConstructibleObject>();
			for (auto formRecipe : arrRecipeForm)
				Workshop__Workbench__TryAddRecipe(a_menuNode, formRecipe);*/
		}
	}

	ModuleFasterWorkshop::ModuleFasterWorkshop() :
		Module("Faster Workshop", &bPatchesFasterWorkshop)
	{}

	bool ModuleFasterWorkshop::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kGameDataReady)
			fasterWorkshopDetail::ClearBuiltMap();
		else
		{
			REL::Relocation HookCheckForValidChildrenTarget{ REL::ID{ 934716, 2195480 }, REL::Offset{ 0x51, 0x4F } };
			REL::Relocation HookIconLoadLagTarget{ REL::ID{ 1280212, 2224975 }, REL::Offset{ 0x3A5, 0x3A0 } };

			// Workshop Lag Fix
			RELEX::DetourCall(HookCheckForValidChildrenTarget.address(), (uintptr_t)&fasterWorkshopDetail::CachedCheckForValidChildren);

			if (RELEX::IsRuntimeOG())
				RELEX::XbyakJump<fasterWorkshopDetail::LeafNodePatch>(fasterWorkshopDetail::LeafNodeTargetOG());
			else
			{
				*(uintptr_t*)&fasterWorkshopDetail::Workshop__Workbench__AddRecipe = REL::ID(2195494).address();
				*(uintptr_t*)&fasterWorkshopDetail::GetDefaultObjectFromDefaultManager = REL::ID(2192850).address();

				RELEX::DetourJump(REL::ID(2195247).address(), (uintptr_t)&fasterWorkshopDetail::Workshop__Workbench__StoreAll);
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