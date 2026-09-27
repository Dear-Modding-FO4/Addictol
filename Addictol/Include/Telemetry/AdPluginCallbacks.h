#pragma once

#include <REX/REX.h>
#include <F4SE/F4SE.h>
#include <Telemetry/AdImageRegistry.h>
#include <Telemetry/AdTelemetry.h>
#include <Telemetry/AdImageSeries.h>

namespace Addictol
{
	class PluginCallbacks
	{
	public:
		static constexpr size_t kKindCount{ 17 };
		static constexpr size_t kSlotCapacity{ 1024 };
		static constexpr size_t kSeriesCapacity{ kImageSeriesCapacity - kBurstSeriesDrainCapacity };

		PluginCallbacks() noexcept;
		void Install(const F4SE::Impl::F4SEInterface& a_load, const Image& a_f4se) noexcept;
		void BeginLoad(F4SE::PluginHandle a_handle, ImageId a_image) noexcept;
		void EndLoad() noexcept;
		[[nodiscard]] size_t Drain(std::span<SeriesSample> a_out) noexcept;
		[[nodiscard]] uint64_t TakeOverflow() noexcept;
		[[nodiscard]] uint64_t TakeSeriesOverflow() noexcept { return m_rows.TakeOverflow(); }
		void BeginInterval() noexcept { m_rows.BeginInterval(); }

	private:
		enum Kind : uint8_t
		{
			kMessage,
			kLoad = 12,
			kSave,
			kRevert,
			kFormDelete,
			kPapyrus
		};
		struct Slot
		{
			ImageId image{};
			Kind kind{};
			void* callback{};
		};
		struct Handle
		{
			std::atomic<uint64_t> value{};
		};
		template<size_t I> static void MessageThunk(F4SE::MessagingInterface::Message* a_message);
		template<size_t I> static void CosaveThunk(const F4SE::SerializationInterface* a_interface);
		template<size_t I> static void DeleteThunk(uint64_t a_handle);
		template<size_t I> static bool PapyrusThunk(RE::BSScript::IVirtualMachine* a_vm);
		static bool RegisterMessage(uint32_t a_handle, const char* a_sender, void* a_callback);
		template<Kind K> static void RegisterCosave(uint32_t a_handle, void* a_callback);
		static bool RegisterPapyrus(void* a_callback);
		void Record(const Slot& a_slot, size_t a_kind, uint64_t a_start) noexcept;
		[[nodiscard]] ImageId FindHandle(uint32_t a_handle) const noexcept;
		[[nodiscard]] void* Wrap(ImageId a_image, Kind a_kind, void* a_callback) noexcept;

		inline static PluginCallbacks* s_instance{};
		inline static thread_local ImageId s_loadingImage{ kInvalidImage };
		std::array<Slot, kSlotCapacity> m_slots{};
		std::atomic<size_t> m_nextSlot{};
		std::array<Handle, kImageCapacity> m_handles{};
		ImageSeriesTable<kKindCount> m_rows;
		std::atomic<uint64_t> m_overflow{};
		decltype(F4SE::Impl::F4SEMessagingInterface::RegisterListener) m_message{};
		std::array<decltype(F4SE::Impl::F4SESerializationInterface::SetLoadCallback), 4> m_cosave{};
		decltype(F4SE::Impl::F4SEPapyrusInterface::Register) m_papyrus{};
		bool m_attempted{};
	};
}
