#include <Modules\AdModuleEncounterZoneReset.h>
#include <AdUtils.h>

#include <RE/B/BGSEncounterZone.h>
#include <RE/C/Calendar.h>
#include <RE/C/CellAttachDetachEventSource.h>
#include <RE/T/TESCellAttachDetachEvent.h>
#include <RE/T/TESObjectCELL.h>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesEncounterZoneReset{ "Fixes"sv, "bEncounterZoneReset"sv, true };

	class Sink : public RE::BSTEventSink<RE::CellAttachDetachEvent>
	{
	public:
		[[nodiscard]] static Sink* GetSingleton()
		{
			static Sink singleton;
			return std::addressof(singleton);
		}

	private:
		Sink() = default;
		Sink(const Sink&) = delete;
		Sink(Sink&&) = delete;
		~Sink() = default;
		Sink& operator=(const Sink&) = delete;
		Sink& operator=(Sink&&) = delete;

		RE::BSEventNotifyControl ProcessEvent(const RE::CellAttachDetachEvent& a_event, RE::BSTEventSource<RE::CellAttachDetachEvent>*) override
		{
			switch (*a_event.type)
			{
			case RE::CellAttachDetachEvent::EVENT_TYPE::kPreDetach:
			{
				const auto cell = a_event.cell;
				const auto ez = cell ? cell->GetEncounterZone() : nullptr;
				const auto calendar = RE::Calendar::GetSingleton();
				if (ez && calendar)
				{
					ez->SetDetachTime(static_cast<std::uint32_t>(calendar->GetHoursPassed()));
				}
			}
			break;
			default:
				break;
			}

			return RE::BSEventNotifyControl::kContinue;
		}
	};

	ModuleEncounterZoneReset::ModuleEncounterZoneReset() :
		Module("Module Encounter Zone Reset", &bFixesEncounterZoneReset)
	{}

	bool ModuleEncounterZoneReset::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleEncounterZoneReset::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		auto& Cells = RE::CellAttachDetachEventSource::CellAttachDetachEventSourceSingleton::GetSingleton();
		Cells.source.RegisterSink(Sink::GetSingleton());

		return true;
	}

	bool ModuleEncounterZoneReset::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}

	bool ModuleEncounterZoneReset::DoPapyrusListener(RE::BSScript::IVirtualMachine* a_vm) noexcept
	{
		return true;
	}
}