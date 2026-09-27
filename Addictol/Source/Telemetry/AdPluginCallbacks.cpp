#include <Telemetry/AdPluginCallbacks.h>
#include <Core/AdClock.h>
#include <Core/AdIAT.h>
#include <REX/REX.h>

#include <utility>
#include <cstring>

namespace Addictol
{
	using namespace std::literals;

	namespace
	{
		constexpr std::array<ImageSeriesKind, PluginCallbacks::kKindCount> s_kinds{
			ImageSeriesKind{ "plugin.message.post_load" }, { "plugin.message.post_post_load" }, { "plugin.message.pre_load_game" },
			{ "plugin.message.post_load_game" }, { "plugin.message.pre_save_game" }, { "plugin.message.post_save_game" },
			{ "plugin.message.delete_game" }, { "plugin.message.input_loaded" }, { "plugin.message.new_game" },
			{ "plugin.message.game_loaded" }, { "plugin.message.game_data_ready" }, { "plugin.message.other" },
			{ "plugin.cosave.load" }, { "plugin.cosave.save" }, { "plugin.cosave.revert" },
			{ "plugin.cosave.form_delete" }, { "plugin.papyrus.register" }
		};

		bool PrepareInterface(const Image& a_f4se, std::span<RELEX::PointerPatch> a_patches) noexcept
		{
			if (!RELEX::PreparePointers(a_patches))
				return false;
			for (const auto& patch : a_patches)
			{
				const auto target = reinterpret_cast<uintptr_t>(patch.previous);
				if (target < a_f4se.base || target >= a_f4se.end)
				{
					RELEX::RestorePointersProtection(a_patches);
					return false;
				}
			}
			return true;
		}
	}

	PluginCallbacks::PluginCallbacks() noexcept :
		m_rows(s_kinds, kSeriesCapacity)
	{}

	void PluginCallbacks::Install(const F4SE::Impl::F4SEInterface& a_load, const Image& a_f4se) noexcept
	{
		if (m_attempted)
			return;
		m_attempted = true;
		s_instance = this;
		if (!a_load.QueryInterface)
			return;
		const auto messaging = static_cast<F4SE::Impl::F4SEMessagingInterface*>(a_load.QueryInterface(F4SE::LoadInterface::kMessaging));
		RELEX::PointerPatch messagePatch{};
		if (messaging && messaging->interfaceVersion == F4SE::MessagingInterface::kVersion)
			messagePatch = { reinterpret_cast<void**>(&messaging->RegisterListener), reinterpret_cast<void*>(&RegisterMessage) };
		if (messagePatch.address && PrepareInterface(a_f4se, { &messagePatch, 1 }))
		{
			m_message = reinterpret_cast<decltype(m_message)>(messagePatch.previous);
			RELEX::PublishPointers({ &messagePatch, 1 });
			REX::INFO("Plugin Timing: messaging callbacks installed"sv);
		}
		else
			REX::WARN("Plugin Timing: messaging interface skipped (version, foreign pointer or protection)"sv);

		const auto serialization = static_cast<F4SE::Impl::F4SESerializationInterface*>(a_load.QueryInterface(F4SE::LoadInterface::kSerialization));
		if (serialization && serialization->version == F4SE::SerializationInterface::kVersion)
		{
			std::array patches{
				RELEX::PointerPatch{ reinterpret_cast<void**>(&serialization->SetLoadCallback), reinterpret_cast<void*>(&RegisterCosave<kLoad>) },
				RELEX::PointerPatch{ reinterpret_cast<void**>(&serialization->SetSaveCallback), reinterpret_cast<void*>(&RegisterCosave<kSave>) },
				RELEX::PointerPatch{ reinterpret_cast<void**>(&serialization->SetRevertCallback), reinterpret_cast<void*>(&RegisterCosave<kRevert>) },
				RELEX::PointerPatch{ reinterpret_cast<void**>(&serialization->SetFormDeleteCallback), reinterpret_cast<void*>(&RegisterCosave<kFormDelete>) }
			};
			if (PrepareInterface(a_f4se, patches))
			{
				for (size_t index = 0; index < patches.size(); ++index)
					m_cosave[index] = reinterpret_cast<decltype(serialization->SetLoadCallback)>(patches[index].previous);
				RELEX::PublishPointers(patches);
				REX::INFO("Plugin Timing: serialization callbacks installed"sv);
			}
			else
				REX::WARN("Plugin Timing: serialization interface skipped (foreign pointer or protection)"sv);
		}
		else
			REX::WARN("Plugin Timing: serialization interface skipped (version)"sv);

		const auto papyrus = static_cast<F4SE::Impl::F4SEPapyrusInterface*>(a_load.QueryInterface(F4SE::LoadInterface::kPapyrus));
		RELEX::PointerPatch papyrusPatch{};
		if (papyrus && papyrus->interfaceVersion == F4SE::PapyrusInterface::kVersion)
			papyrusPatch = { reinterpret_cast<void**>(&papyrus->Register), reinterpret_cast<void*>(&RegisterPapyrus) };
		if (papyrusPatch.address && PrepareInterface(a_f4se, { &papyrusPatch, 1 }))
		{
			m_papyrus = reinterpret_cast<decltype(m_papyrus)>(papyrusPatch.previous);
			RELEX::PublishPointers({ &papyrusPatch, 1 });
			REX::INFO("Plugin Timing: Papyrus callbacks installed"sv);
		}
		else
			REX::WARN("Plugin Timing: Papyrus interface skipped (version, foreign pointer or protection)"sv);
	}

	void PluginCallbacks::BeginLoad(F4SE::PluginHandle a_handle, ImageId a_image) noexcept
	{
		s_loadingImage = a_image;
		if (a_image == kInvalidImage)
			return;
		const uint64_t value = (uint64_t(a_handle) << 32) | (uint64_t(a_image) + 1);
		for (auto& handle : m_handles)
		{
			uint64_t empty{};
			if (handle.value.compare_exchange_strong(empty, value, std::memory_order_release) || empty == value)
				return;
		}
		m_overflow.fetch_add(1, std::memory_order_relaxed);
	}

	void PluginCallbacks::EndLoad() noexcept
	{
		s_loadingImage = kInvalidImage;
	}

	ImageId PluginCallbacks::FindHandle(uint32_t a_handle) const noexcept
	{
		for (const auto& handle : m_handles)
		{
			const auto value = handle.value.load(std::memory_order_acquire);
			if (value && (value >> 32) == a_handle)
				return static_cast<ImageId>((value & UINT32_MAX) - 1);
		}
		return kInvalidImage;
	}

	void* PluginCallbacks::Wrap(ImageId a_image, Kind a_kind, void* a_callback) noexcept
	{
		if (!a_callback || a_image == kInvalidImage)
			return a_callback;
		const auto index = m_nextSlot.fetch_add(1, std::memory_order_relaxed);
		if (index >= kSlotCapacity)
		{
			m_overflow.fetch_add(1, std::memory_order_relaxed);
			return a_callback;
		}
		m_slots[index] = { a_image, a_kind, a_callback };
		static constexpr auto messages = []<size_t... I>(std::index_sequence<I...>) {
			return std::array{ &MessageThunk<I>... };
		}(std::make_index_sequence<kSlotCapacity>{});
		static constexpr auto cosaves = []<size_t... I>(std::index_sequence<I...>) {
			return std::array{ &CosaveThunk<I>... };
		}(std::make_index_sequence<kSlotCapacity>{});
		static constexpr auto deletes = []<size_t... I>(std::index_sequence<I...>) {
			return std::array{ &DeleteThunk<I>... };
		}(std::make_index_sequence<kSlotCapacity>{});
		static constexpr auto papyrus = []<size_t... I>(std::index_sequence<I...>) {
			return std::array{ &PapyrusThunk<I>... };
		}(std::make_index_sequence<kSlotCapacity>{});
		switch (a_kind)
		{
		case kMessage: return reinterpret_cast<void*>(messages[index]);
		case kFormDelete: return reinterpret_cast<void*>(deletes[index]);
		case kPapyrus: return reinterpret_cast<void*>(papyrus[index]);
		default: return reinterpret_cast<void*>(cosaves[index]);
		}
	}

	void PluginCallbacks::Record(const Slot& a_slot, size_t a_kind, uint64_t a_start) noexcept
	{
		auto& counter = m_rows.At(a_slot.image, a_kind);
		counter.ticks.fetch_add(ReadQpc() - a_start, std::memory_order_relaxed);
		counter.calls.fetch_add(1, std::memory_order_relaxed);
	}

	template<size_t I>
	void PluginCallbacks::MessageThunk(F4SE::MessagingInterface::Message* a_message)
	{
		auto& self = *s_instance;
		const auto& slot = self.m_slots[I];
		const auto kind = a_message && a_message->sender && std::strcmp(a_message->sender, "F4SE") == 0 &&
			a_message->type < 11 ? a_message->type : 11;
		const auto start = ReadQpc();
		reinterpret_cast<F4SE::MessagingInterface::EventCallback*>(slot.callback)(a_message);
		self.Record(slot, kind, start);
	}

	template<size_t I>
	void PluginCallbacks::CosaveThunk(const F4SE::SerializationInterface* a_interface)
	{
		auto& self = *s_instance;
		const auto& slot = self.m_slots[I];
		const auto start = ReadQpc();
		reinterpret_cast<F4SE::SerializationInterface::EventCallback*>(slot.callback)(a_interface);
		self.Record(slot, slot.kind, start);
	}

	template<size_t I>
	void PluginCallbacks::DeleteThunk(uint64_t a_handle)
	{
		auto& self = *s_instance;
		const auto& slot = self.m_slots[I];
		const auto start = ReadQpc();
		reinterpret_cast<F4SE::SerializationInterface::FormDeleteCallback*>(slot.callback)(a_handle);
		self.Record(slot, slot.kind, start);
	}

	template<size_t I>
	bool PluginCallbacks::PapyrusThunk(RE::BSScript::IVirtualMachine* a_vm)
	{
		auto& self = *s_instance;
		const auto& slot = self.m_slots[I];
		const auto start = ReadQpc();
		const auto result = reinterpret_cast<bool (*)(RE::BSScript::IVirtualMachine*)>(slot.callback)(a_vm);
		self.Record(slot, slot.kind, start);
		return result;
	}

	bool PluginCallbacks::RegisterMessage(uint32_t a_handle, const char* a_sender, void* a_callback)
	{
		auto& self = *s_instance;
		return self.m_message(a_handle, a_sender, self.Wrap(self.FindHandle(a_handle), kMessage, a_callback));
	}

	template<PluginCallbacks::Kind K>
	void PluginCallbacks::RegisterCosave(uint32_t a_handle, void* a_callback)
	{
		auto& self = *s_instance;
		self.m_cosave[K - kLoad](a_handle, self.Wrap(self.FindHandle(a_handle), K, a_callback));
	}

	bool PluginCallbacks::RegisterPapyrus(void* a_callback)
	{
		auto& self = *s_instance;
		return self.m_papyrus(self.Wrap(s_loadingImage, kPapyrus, a_callback));
	}

	size_t PluginCallbacks::Drain(std::span<SeriesSample> a_out) noexcept
	{
		return m_rows.Drain(a_out);
	}

	uint64_t PluginCallbacks::TakeOverflow() noexcept
	{
		return m_overflow.exchange(0, std::memory_order_relaxed);
	}
}
