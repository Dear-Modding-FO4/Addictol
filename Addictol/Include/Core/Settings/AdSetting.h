#pragma once

#include <REX/REX.h>

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace Addictol
{
	enum class SettingValueType : uint8_t
	{
		kBoolean,
		kFloat32,
		kInt32,
		kUInt32,
		kString
	};

	enum class SettingApplyTiming : uint8_t
	{
		kImmediate,
		kNextLaunch
	};

	struct SettingNumericRange
	{
		std::optional<double> minimum;
		std::optional<double> maximum;
	};

	using SettingValue = std::variant<bool, double, int64_t, uint64_t, std::string>;

	class SettingEntry
	{
	public:
		[[nodiscard]] std::string_view Section() const noexcept { return m_section; }
		[[nodiscard]] std::string_view Key() const noexcept { return m_key; }
		[[nodiscard]] SettingValueType Type() const noexcept { return m_type; }
		[[nodiscard]] SettingValue DefaultValue() const;
		[[nodiscard]] SettingValue Value() const;
		[[nodiscard]] bool SetValue(const SettingValue& a_value) const;
		[[nodiscard]] std::string_view Description() const noexcept { return m_description; }
		[[nodiscard]] SettingApplyTiming ApplyTiming() const noexcept { return m_applyTiming; }
		[[nodiscard]] const std::optional<SettingNumericRange>& NumericRange() const noexcept
		{
			return m_numericRange;
		}

	private:
		template <class>
		friend class TomlSetting;

		using ReadFunction = SettingValue (*)(const void*);
		using WriteFunction = bool (*)(void*, const SettingValue&);

		SettingEntry(
			std::string_view a_section,
			std::string_view a_key,
			SettingValueType a_type,
			std::string_view a_description,
			SettingApplyTiming a_applyTiming,
			std::optional<SettingNumericRange> a_numericRange,
			void* a_setting,
			ReadFunction a_readDefault,
			ReadFunction a_read,
			WriteFunction a_write);

		std::string m_section;
		std::string m_key;
		SettingValueType m_type;
		std::string m_description;
		SettingApplyTiming m_applyTiming;
		std::optional<SettingNumericRange> m_numericRange;
		void* m_setting;
		ReadFunction m_readDefault;
		ReadFunction m_read;
		WriteFunction m_write;
	};

	class SettingRegistry
	{
	public:
		[[nodiscard]] static SettingRegistry& GetSingleton() noexcept;

		[[nodiscard]] std::span<const SettingEntry* const> Settings() const noexcept;
		[[nodiscard]] const SettingEntry* Find(
			std::string_view a_section,
			std::string_view a_key) const noexcept;
		[[nodiscard]] bool ContainsSection(std::string_view a_section) const noexcept;

	private:
		template <class>
		friend class TomlSetting;

		void Register(SettingEntry a_entry);

		std::vector<std::unique_ptr<SettingEntry>> m_storage;
		std::vector<const SettingEntry*> m_settings;
		mutable std::atomic_bool m_enumerated{ false };
	};

	template <class T>
	class TomlSetting final :
		public REX::TTomlSetting<T>
	{
		using Base = REX::TTomlSetting<T>;

		static constexpr SettingValueType ValueType() noexcept
		{
			if constexpr (std::is_same_v<T, bool>)
				return SettingValueType::kBoolean;
			else if constexpr (std::is_same_v<T, float>)
				return SettingValueType::kFloat32;
			else if constexpr (std::is_same_v<T, int32_t>)
				return SettingValueType::kInt32;
			else if constexpr (std::is_same_v<T, uint32_t>)
				return SettingValueType::kUInt32;
			else
			{
				static_assert(std::is_same_v<T, std::string>);
				return SettingValueType::kString;
			}
		}

		[[nodiscard]] static SettingValue Encode(T a_value)
		{
			if constexpr (std::is_same_v<T, bool>)
				return a_value;
			else if constexpr (std::is_same_v<T, float>)
				return static_cast<double>(a_value);
			else if constexpr (std::is_same_v<T, int32_t>)
				return static_cast<int64_t>(a_value);
			else if constexpr (std::is_same_v<T, uint32_t>)
				return static_cast<uint64_t>(a_value);
			else
				return a_value;
		}

		[[nodiscard]] static SettingValue ReadDefault(const void* a_setting)
		{
			return Encode(static_cast<const Base*>(a_setting)->GetValueDefault());
		}

		[[nodiscard]] static SettingValue Read(const void* a_setting)
		{
			return Encode(static_cast<const Base*>(a_setting)->GetValue());
		}

		[[nodiscard]] static bool Write(void* a_setting, const SettingValue& a_value)
		{
			auto* setting = static_cast<Base*>(a_setting);
			if constexpr (std::is_same_v<T, bool>)
			{
				const auto* value = std::get_if<bool>(&a_value);
				if (!value)
					return false;
				setting->SetValue(*value);
			}
			else if constexpr (std::is_same_v<T, float>)
			{
				const auto* value = std::get_if<double>(&a_value);
				if (!value)
					return false;
				setting->SetValue(static_cast<float>(*value));
			}
			else if constexpr (std::is_same_v<T, int32_t>)
			{
				const auto* value = std::get_if<int64_t>(&a_value);
				if (!value ||
					*value < (std::numeric_limits<int32_t>::min)() ||
					*value > (std::numeric_limits<int32_t>::max)())
					return false;
				setting->SetValue(static_cast<int32_t>(*value));
			}
			else if constexpr (std::is_same_v<T, uint32_t>)
			{
				const auto* value = std::get_if<uint64_t>(&a_value);
				if (!value || *value > (std::numeric_limits<uint32_t>::max)())
					return false;
				setting->SetValue(static_cast<uint32_t>(*value));
			}
			else
			{
				const auto* value = std::get_if<std::string>(&a_value);
				if (!value)
					return false;
				setting->SetValue(*value);
			}
			return true;
		}

	public:
		TomlSetting(
			std::string_view a_section,
			std::string_view a_key,
			T a_default,
			std::string_view a_description,
			SettingApplyTiming a_applyTiming,
			std::optional<SettingNumericRange> a_numericRange = std::nullopt) :
			Base(a_section, a_key, std::move(a_default))
		{
			SettingRegistry::GetSingleton().Register({
				a_section,
				a_key,
				ValueType(),
				a_description,
				a_applyTiming,
				std::move(a_numericRange),
				static_cast<Base*>(this),
				&ReadDefault,
				&Read,
				&Write
			});
		}
	};

	using BoolSetting = TomlSetting<bool>;
	using F32Setting = TomlSetting<float>;
	using I32Setting = TomlSetting<int32_t>;
	using U32Setting = TomlSetting<uint32_t>;
	using StrSetting = TomlSetting<std::string>;
}
