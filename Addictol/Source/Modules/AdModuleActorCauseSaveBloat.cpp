#include <Modules/AdModuleActorCauseSaveBloat.h>
#include <AdUtils.h>

#include <RE/C/CellAttachDetachEventSource.h>
#include <RE/T/TESBoundObject.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/T/TESObjectREFR.h>

#define AD_NOMESSAGE_ACTORCAUSESAVEBLOAT 1

namespace Addictol
{
	static REX::TOML::Bool<> bFixesActorCauseSaveBloat{ "Fixes"sv, "bActorCauseSaveBloat"sv, true };

	namespace actorCauseSaveBloatDetail
	{
		std::vector<RE::TESObjectREFR*> GetProjectilesInCell(RE::TESObjectCELL* a_cell) noexcept
		{
			auto references_vector = std::vector<RE::TESObjectREFR*>();

			if (!a_cell)
				return references_vector;

			const RE::BSAutoLock lock{ a_cell->spinLock };

			for (auto& reference_pointer : a_cell->references)
			{
				RE::TESObjectREFR* reference = reference_pointer.get();

				if (!reference)
					continue;

				RE::TESBoundObject* baseObj = reference->data.objectReference;

				if (!baseObj)
					continue;
				
				if (baseObj->formType == RE::ENUM_FORM_ID::kPROJ)
					references_vector.push_back(reference);
			}

			return references_vector;
		}

		class CellAttachDetachEventHandler : public RE::BSTEventSink<RE::CellAttachDetachEvent>
		{
		public:
			[[nodiscard]] static CellAttachDetachEventHandler* GetSingleton()
			{
				static CellAttachDetachEventHandler singleton;
				return std::addressof(singleton);
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::CellAttachDetachEvent& a_event, RE::BSTEventSource<RE::CellAttachDetachEvent>*) override
			{
				if (*a_event.type == RE::CellAttachDetachEvent::EVENT_TYPE::kPreDetach)
				{
#if !AD_NOMESSAGE_ACTORCAUSESAVEBLOAT
					REX::INFO("ActorCauseSaveBloat: Event recieved."sv);
#endif

					if (!a_event.cell)
					{
#if !AD_NOMESSAGE_ACTORCAUSESAVEBLOAT
						REX::WARN("ActorCauseSaveBloat: a_event.cell was nullptr. Skipping this cell."sv);
#endif
						return RE::BSEventNotifyControl::kContinue;
					}

					std::vector<RE::TESObjectREFR*> projectiles = GetProjectilesInCell(a_event.cell);
#if !AD_NOMESSAGE_ACTORCAUSESAVEBLOAT
					REX::INFO("ActorCauseSaveBloat: Processing projectiles vector. Size: {}."sv, projectiles.size());
#endif

					if (projectiles.size() == 0)
						return RE::BSEventNotifyControl::kContinue;

					for (RE::TESObjectREFR* projectile : projectiles)
					{
						if (projectile)
						{
							if (projectile->GetActorCause() != nullptr)
								projectile->SetActorCause(nullptr);
						}
					}
				}

				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			CellAttachDetachEventHandler() = default;
			CellAttachDetachEventHandler(const CellAttachDetachEventHandler&) = delete;
			CellAttachDetachEventHandler(CellAttachDetachEventHandler&&) = delete;
			~CellAttachDetachEventHandler() = default;
			CellAttachDetachEventHandler& operator=(const CellAttachDetachEventHandler&) = delete;
			CellAttachDetachEventHandler& operator=(CellAttachDetachEventHandler&&) = delete;
		};
	}

	ModuleActorCauseSaveBloat::ModuleActorCauseSaveBloat() :
		Module("Actor Cause Save Bloat", &bFixesActorCauseSaveBloat, { F4SE::MessagingInterface::kPostLoadGame })
	{}

	bool ModuleActorCauseSaveBloat::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleActorCauseSaveBloat::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg && a_msg->type == F4SE::MessagingInterface::kPostLoadGame)
		{
			auto& Cells = RE::CellAttachDetachEventSource::CellAttachDetachEventSourceSingleton::GetSingleton();
			Cells.source.RegisterSink(actorCauseSaveBloatDetail::CellAttachDetachEventHandler::GetSingleton());
		}
			
		return true;
	}

}