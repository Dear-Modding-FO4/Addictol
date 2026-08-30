#include <Core/Settings/AdSetting.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <tuple>

namespace Addictol
{
	namespace
	{
		[[nodiscard]] std::string BuildDisplayName(std::string_view a_key)
		{
			if (a_key.size() > 1 &&
				(a_key.front() == 'b' || a_key.front() == 'f' ||
					a_key.front() == 'n' || a_key.front() == 's' ||
					a_key.front() == 'u') &&
				std::isupper(static_cast<unsigned char>(a_key[1])))
				a_key.remove_prefix(1);

			std::string result;
			result.reserve(a_key.size() + 8);
			for (size_t index = 0; index < a_key.size(); ++index)
			{
				const auto current =
					static_cast<unsigned char>(a_key[index]);
				const auto previous = index > 0 ?
					static_cast<unsigned char>(a_key[index - 1]) :
					0;
				const auto next = index + 1 < a_key.size() ?
					static_cast<unsigned char>(a_key[index + 1]) :
					0;
				const auto boundary = index > 0 &&
					((std::isupper(current) &&
						(std::islower(previous) || std::isdigit(previous) ||
							(std::isupper(previous) && std::islower(next)))) ||
						(std::isdigit(current) && !std::isdigit(previous)) ||
						(!std::isdigit(current) && std::isdigit(previous)));
				if (boundary)
					result.push_back(' ');
				result.push_back(static_cast<char>(current));
			}
			return result;
		}
	}

	SettingEntry::SettingEntry(
		std::string_view a_section,
		std::string_view a_key,
		SettingDisplayCategory a_displayCategory,
		SettingValueType a_type,
		std::string_view a_description,
		SettingApplyTiming a_applyTiming,
		std::optional<SettingNumericRange> a_numericRange,
		void* a_setting,
		ReadFunction a_readDefault,
		ReadFunction a_read,
		WriteFunction a_write) :
		m_section(a_section),
		m_key(a_key),
		m_displayName(BuildDisplayName(a_key)),
		m_displayCategory(a_displayCategory),
		m_type(a_type),
		m_description(a_description),
		m_applyTiming(a_applyTiming),
		m_numericRange(std::move(a_numericRange)),
		m_setting(a_setting),
		m_readDefault(a_readDefault),
		m_read(a_read),
		m_write(a_write)
	{}

	SettingValue SettingEntry::DefaultValue() const
	{
		return m_readDefault(m_setting);
	}

	SettingValue SettingEntry::Value() const
	{
		return m_read(m_setting);
	}

	bool SettingEntry::SetValue(const SettingValue& a_value) const
	{
		return m_write(m_setting, a_value);
	}

	SettingRegistry& SettingRegistry::GetSingleton() noexcept
	{
		static SettingRegistry singleton;
		return singleton;
	}

	std::span<const SettingEntry* const> SettingRegistry::Settings() const noexcept
	{
		m_enumerated.store(true, std::memory_order_release);
		return m_settings;
	}

	const SettingEntry* SettingRegistry::Find(
		std::string_view a_section,
		std::string_view a_key) const noexcept
	{
		const auto position = std::ranges::lower_bound(
			m_settings,
			std::tuple{ a_section, a_key },
			{},
			[](const SettingEntry* a_entry) {
				return std::tuple{ a_entry->Section(), a_entry->Key() };
			});
		if (position == m_settings.end() ||
			(*position)->Section() != a_section ||
			(*position)->Key() != a_key)
			return nullptr;
		return *position;
	}

	bool SettingRegistry::ContainsSection(std::string_view a_section) const noexcept
	{
		const auto position = std::ranges::lower_bound(
			m_settings,
			a_section,
			{},
			[](const SettingEntry* a_entry) {
				return a_entry->Section();
			});
		return position != m_settings.end() && (*position)->Section() == a_section;
	}

	void SettingRegistry::Register(SettingEntry a_entry)
	{
		if (m_enumerated.load(std::memory_order_acquire))
			throw std::logic_error("setting registered after enumeration");
		if (a_entry.DisplayCategory() >= SettingDisplayCategory::kCount)
			throw std::logic_error("setting has no display category");
		if (Find(a_entry.Section(), a_entry.Key()))
			throw std::logic_error("duplicate setting");

		auto entry = std::make_unique<SettingEntry>(std::move(a_entry));
		const auto position = std::ranges::lower_bound(
			m_settings,
			std::tuple{ entry->Section(), entry->Key() },
			{},
			[](const SettingEntry* a_item) {
				return std::tuple{ a_item->Section(), a_item->Key() };
			});
		m_settings.insert(position, entry.get());
		m_storage.push_back(std::move(entry));
	}
}
