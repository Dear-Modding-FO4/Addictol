#pragma once

#include <array>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <span>
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
		kEmptyValue,
		kMalformedFormID,
		kDataNotReady
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
		uint64_t revision{ 0 };
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

	struct FacegenExceptionDraft
	{
		std::string key;
		std::string formID;
		std::optional<std::string> pluginName;

		[[nodiscard]] bool operator==(const FacegenExceptionDraft&) const = default;
	};

	enum class FacegenExceptionValidationIssue : uint8_t
	{
		kNone,
		kEmptyKey,
		kMalformedKey,
		kDuplicateKey,
		kEmptyFormID,
		kMalformedFormID
	};

	struct FacegenExceptionFieldValidation
	{
		FacegenExceptionValidationIssue issue{ FacegenExceptionValidationIssue::kNone };
		std::optional<uint32_t> parsedFormID;

		[[nodiscard]] bool Valid() const noexcept
		{
			return issue == FacegenExceptionValidationIssue::kNone;
		}
	};

	struct FacegenExceptionValidation
	{
		bool valid{ false };
		FacegenExceptionStatus status{ FacegenExceptionStatus::kEmptyValue };
		std::optional<uint32_t> resolvedFormID;
		std::string message;
	};

	struct FacegenExceptionOperationResult
	{
		bool success{ false };
		std::string error;
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
			TrimFacegenExceptionField(parsed.formID);
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
		const auto hexadecimal =
			value.size() >= 2 &&
			value[0] == '0' &&
			(value[1] == 'x' || value[1] == 'X');
		return static_cast<uint32_t>(
			hexadecimal ?
				std::strtoul(value.c_str() + 2, nullptr, 16) :
				std::strtoul(value.c_str(), nullptr, 10));
	}

	[[nodiscard]] inline bool TryParseFacegenFormID(
		std::string_view a_value,
		uint32_t& a_formID) noexcept
	{
		std::string value{ a_value };
		TrimFacegenExceptionField(value);
		if (value.empty())
			return false;

		const auto hexadecimal =
			value.size() >= 2 &&
			value[0] == '0' &&
			(value[1] == 'x' || value[1] == 'X');
		const auto digits = std::string_view{ value }.substr(hexadecimal ? 2 : 0);
		if (digits.empty())
			return false;
		for (const auto character : digits)
		{
			const auto byte = static_cast<unsigned char>(character);
			if (hexadecimal ? !std::isxdigit(byte) : !std::isdigit(byte))
				return false;
		}

		errno = 0;
		char* end = nullptr;
		const auto parsed = std::strtoull(
			digits.data(),
			&end,
			hexadecimal ? 16 : 10);
		if (errno == ERANGE ||
			end != digits.data() + digits.size() ||
			parsed > (std::numeric_limits<uint32_t>::max)())
			return false;

		a_formID = ParseFacegenFormID(value);
		return true;
	}

	[[nodiscard]] inline bool FacegenExceptionKeysEqual(
		std::string_view a_left,
		std::string_view a_right) noexcept
	{
		if (a_left.size() != a_right.size())
			return false;
		for (size_t index = 0; index < a_left.size(); ++index)
		{
			if (std::tolower(static_cast<unsigned char>(a_left[index])) !=
				std::tolower(static_cast<unsigned char>(a_right[index])))
				return false;
		}
		return true;
	}

	[[nodiscard]] inline bool HasDuplicateFacegenExceptionKey(
		std::span<const FacegenExceptionDraft> a_entries,
		std::string_view a_key,
		std::optional<size_t> a_ignoredIndex = std::nullopt) noexcept
	{
		for (size_t index = 0; index < a_entries.size(); ++index)
		{
			if (a_ignoredIndex == index)
				continue;
			auto existing = a_entries[index].key;
			TrimFacegenExceptionField(existing);
			if (FacegenExceptionKeysEqual(existing, a_key))
				return true;
		}
		return false;
	}

	[[nodiscard]] inline FacegenExceptionFieldValidation
	ValidateFacegenExceptionFields(
		const FacegenExceptionDraft& a_entry,
		std::span<const FacegenExceptionDraft> a_entries,
		std::optional<size_t> a_ignoredIndex = std::nullopt) noexcept
	{
		auto key = a_entry.key;
		TrimFacegenExceptionField(key);
		if (key.empty())
			return { FacegenExceptionValidationIssue::kEmptyKey, std::nullopt };
		if (key.find_first_of("=\r\n") != std::string::npos)
			return { FacegenExceptionValidationIssue::kMalformedKey, std::nullopt };
		if (HasDuplicateFacegenExceptionKey(a_entries, key, a_ignoredIndex))
			return { FacegenExceptionValidationIssue::kDuplicateKey, std::nullopt };

		auto formID = a_entry.formID;
		TrimFacegenExceptionField(formID);
		if (formID.empty())
			return { FacegenExceptionValidationIssue::kEmptyFormID, std::nullopt };
		uint32_t parsedFormID = 0;
		if (!TryParseFacegenFormID(formID, parsedFormID))
			return { FacegenExceptionValidationIssue::kMalformedFormID, std::nullopt };
		return { FacegenExceptionValidationIssue::kNone, parsedFormID };
	}

	[[nodiscard]] inline std::string SerializeFacegenExceptionValue(
		std::string_view a_formID,
		const std::optional<std::string>& a_pluginName)
	{
		std::string value{ a_formID };
		TrimFacegenExceptionField(value);
		if (a_pluginName)
		{
			auto pluginName = *a_pluginName;
			TrimFacegenExceptionField(pluginName);
			if (!pluginName.empty())
			{
				value.push_back(':');
				value += pluginName;
			}
		}
		return value;
	}

	[[nodiscard]] inline std::string SerializeFacegenExceptionEntry(
		const FacegenExceptionDraft& a_entry)
	{
		auto key = a_entry.key;
		TrimFacegenExceptionField(key);
		key.push_back('=');
		key += SerializeFacegenExceptionValue(a_entry.formID, a_entry.pluginName);
		return key;
	}

	[[nodiscard]] inline std::string ExtractFacegenExceptionLeadingComments(
		std::string_view a_ini)
	{
		constexpr std::string_view section{ "[FacegenException]" };
		auto position = a_ini.find(section);
		if (position == std::string_view::npos)
			return {};
		position = a_ini.find_first_of("\r\n", position + section.size());
		if (position == std::string_view::npos)
			return {};
		while (position < a_ini.size() &&
			(a_ini[position] == '\r' || a_ini[position] == '\n'))
			++position;

		std::string comments;
		bool foundComment = false;
		while (position < a_ini.size())
		{
			auto end = a_ini.find_first_of("\r\n", position);
			if (end == std::string_view::npos)
				end = a_ini.size();
			auto line = a_ini.substr(position, end - position);
			const auto first = line.find_first_not_of(" \t");
			if (first == std::string_view::npos)
			{
				if (foundComment)
					comments.push_back('\n');
			}
			else if (line[first] == ';' || line[first] == '#')
			{
				foundComment = true;
				comments.append(line.substr(first));
				comments.push_back('\n');
			}
			else
				break;

			position = end;
			while (position < a_ini.size() &&
				(a_ini[position] == '\r' || a_ini[position] == '\n'))
				++position;
		}
		while (!comments.empty() && comments.back() == '\n')
			comments.pop_back();
		return comments;
	}

	[[nodiscard]] FacegenExceptionSnapshot GetFacegenExceptionSnapshot();
	[[nodiscard]] FacegenExceptionValidation ValidateFacegenException(
		const FacegenExceptionDraft& a_entry,
		std::span<const FacegenExceptionDraft> a_entries,
		std::optional<size_t> a_ignoredIndex = std::nullopt) noexcept;
	[[nodiscard]] FacegenExceptionOperationResult SaveFacegenExceptions(
		std::span<const FacegenExceptionDraft> a_entries) noexcept;
	[[nodiscard]] FacegenExceptionOperationResult ReloadFacegenExceptions() noexcept;
}
