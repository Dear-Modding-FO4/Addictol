#pragma once

#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Addictol
{
	inline constexpr std::string_view kFacegenExceptionsPath{
		"Data\\F4SE\\Plugins\\Addictol_FacegenExceptions.ini"
	};

	enum class FacegenExceptionStatus : uint8_t
	{
		kResolved,
		kPluginNotFound,
		kMissingPluginName,
		kFatalError,
		kEmptyValue
	};

	struct FacegenExceptionRecord
	{
		std::string key;
		std::string rawValue;
		std::optional<std::string> pluginName;
		std::optional<uint32_t> resolvedFormID;
		FacegenExceptionStatus status{ FacegenExceptionStatus::kEmptyValue };
	};

	struct FacegenExceptionSnapshot
	{
		bool readAttempted{ false };
		bool iniFound{ false };
		bool sectionFound{ false };
		size_t effectiveExceptionCount{ 0 };
		std::vector<FacegenExceptionRecord> entries;
	};

	struct FacegenPrimaryException
	{
		uint32_t formID;
		std::string_view name;
	};

	inline constexpr std::array kFacegenPrimaryExceptions{
		FacegenPrimaryException{ 0x26F0A, "MQ102PlayerSpouseCorpseMale" },
		FacegenPrimaryException{ 0x26F36, "MQ102PlayerSpouseCorpseFemale" },
		FacegenPrimaryException{ 0xA7D34, "MQ101PlayerSpouseMale" },
		FacegenPrimaryException{ 0xA7D35, "MQ101PlayerSpouseFemale" },
		FacegenPrimaryException{ 0x246BF0, "MQ101PlayerSpouseMale_NameOnly" },
		FacegenPrimaryException{ 0x246BF1, "MQ101PlayerSpouseFemale_NameOnly" }
	};

	struct ParsedFacegenExceptionValue
	{
		std::string formID;
		std::optional<std::string> pluginName;
	};

	inline void TrimFacegenExceptionField(std::string& a_value)
	{
		constexpr std::string_view whitespace{ " \t\n\r\f\v" };
		const auto first = a_value.find_first_not_of(whitespace);
		if (first == std::string::npos)
		{
			a_value.clear();
			return;
		}
		a_value.erase(a_value.find_last_not_of(whitespace) + 1);
		a_value.erase(0, first);
	}

	[[nodiscard]] inline ParsedFacegenExceptionValue ParseFacegenExceptionValue(
		std::string_view a_value)
	{
		ParsedFacegenExceptionValue parsed;
		const auto separator = a_value.find_first_of(':');
		if (separator == std::string_view::npos)
		{
			parsed.formID = a_value;
			return parsed;
		}

		parsed.formID = a_value.substr(0, separator);
		parsed.pluginName = std::string{ a_value.substr(separator + 1) };
		TrimFacegenExceptionField(parsed.formID);
		TrimFacegenExceptionField(*parsed.pluginName);
		return parsed;
	}

	[[nodiscard]] inline uint32_t ParseFacegenFormID(std::string_view a_value)
	{
		const std::string value{ a_value };
		return static_cast<uint32_t>(
			value.find_first_of("0x") == 0 ?
				std::strtoul(value.c_str() + 2, nullptr, 16) :
				std::strtoul(value.c_str(), nullptr, 10));
	}

	[[nodiscard]] FacegenExceptionSnapshot GetFacegenExceptionSnapshot();
}
