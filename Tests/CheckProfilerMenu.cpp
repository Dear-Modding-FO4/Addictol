#include "../Addictol/Include/AdProfilerAllocator.h"
#include "../Addictol/Include/AdProfilerBA2Schema.h"
#include "../Addictol/Include/AdProfilerMenu.h"
#include "../Addictol/Include/AdProfilerRuntimeChannel.h"
#include "../Addictol/Include/AdTextureOneShot.h"
#include "Harness.h"

#include <filesystem>
#include <initializer_list>
#include <string>
#include <type_traits>
#include <vector>

namespace vmm_tests
{
	namespace
	{
		using namespace Addictol;

		struct ExpectedToggleKey
		{
			std::string_view name;
			uint32_t virtualKey;
		};

		struct ExpectedLogLevel
		{
			LogControl::Level level;
			std::string_view name;
		};

		inline constexpr std::initializer_list<std::string_view> kExpectedTabNames{
			"Overview"sv,
			"Frame Hitch"sv,
			"Decompression"sv,
			"Allocator"sv,
			"Memory"sv,
			"Modules"sv,
			"Texture Decode"sv
		};

		inline constexpr std::initializer_list<ExpectedToggleKey> kExpectedToggleKeys{
			{ "F1"sv, 0x70 },
			{ "F2"sv, 0x71 },
			{ "F3"sv, 0x72 },
			{ "F4"sv, 0x73 },
			{ "F5"sv, 0x74 },
			{ "F6"sv, 0x75 },
			{ "F7"sv, 0x76 },
			{ "F8"sv, 0x77 },
			{ "F9"sv, 0x78 },
			{ "F10"sv, 0x79 },
			{ "F11"sv, 0x7A },
			{ "F12"sv, 0x7B },
			{ "Home"sv, 0x24 },
			{ "End"sv, 0x23 },
			{ "Insert"sv, 0x2D },
			{ "Delete"sv, 0x2E }
		};

		inline constexpr std::initializer_list<ExpectedLogLevel> kExpectedLogLevels{
			{ LogControl::Level::kTrace, "trace"sv },
			{ LogControl::Level::kDebug, "debug"sv },
			{ LogControl::Level::kInfo, "info"sv },
			{ LogControl::Level::kWarn, "warn"sv },
			{ LogControl::Level::kError, "error"sv },
			{ LogControl::Level::kCritical, "critical"sv },
			{ LogControl::Level::kOff, "off"sv }
		};

		static_assert(std::is_trivially_copyable_v<AllocatorProfileEntry>);
		static_assert(std::is_trivially_copyable_v<AllocatorBucketEntry>);
		static_assert(std::is_trivially_copyable_v<TextureOneShot::CountersSnapshot>);
		static_assert(std::is_trivially_copyable_v<BA2PublishedSnapshot>);

		static_assert(kProfilerMenuDefaultToggleKey == 0x7A);
		static_assert(kProfilerMenuMinRefreshMs == 100);
		static_assert(kProfilerMenuMaxRefreshMs == 2000);
		static_assert(kAllocatorProfileEntryCapacity == 64);
		static_assert(BA2PublishedSnapshot::kReasonCapacity == 32);
		static_assert(ParseProfilerMenuToggleKey("F11"sv).virtualKey == 0x7A);
		static_assert(ParseProfilerMenuToggleKey("F11"sv).recognized);
		static_assert(!ParseProfilerMenuToggleKey("Q"sv).recognized);
		static_assert(ParseProfilerMenuToggleKey("Q"sv).virtualKey == 0x7A);
		static_assert(TextureOneShot::Describe(TextureOneShot::InstallState::Indeterminate) == "indeterminate"sv);
		static_assert(ClampProfilerMenuFormattedLength(53, 48) == 47);

		void write_header(std::ostream& a_file)
		{
			a_file << "Value\n";
		}

		void write_entry(std::ostream& a_file, const int& a_entry, const RuntimeRowMetadata&)
		{
			a_file << a_entry << "\n";
		}

		[[nodiscard]] std::filesystem::path temporary_directory(std::string_view a_name)
		{
			auto path = std::filesystem::temp_directory_path();
			path /= "addictol-menu-checks";
			path /= a_name;
			return path;
		}
	}

	void run_profiler_menu_checks(Runner& runner)
	{
		runner.test("menu tabs match the pinned order", [] {
			require(kProfilerMenuTabNames.size() == kExpectedTabNames.size(), "tab count changed");

			size_t index = 0;
			for (const auto expected : kExpectedTabNames)
			{
				require(kProfilerMenuTabNames[index] == expected, "tab name or order changed");
				require(
					Describe(static_cast<ProfilerMenuTab>(index)) == expected,
					"tab description disagrees with its pinned name");
				++index;
			}
			require(
				Describe(static_cast<ProfilerMenuTab>(index)) == "Unknown"sv,
				"an out-of-range tab was described as valid");
		});

		runner.test("toggle key table matches pinned virtual keys", [] {
			require(kProfilerMenuToggleKeys.size() == kExpectedToggleKeys.size(), "toggle key count changed");

			size_t index = 0;
			for (const auto& expected : kExpectedToggleKeys)
			{
				const auto& actual = kProfilerMenuToggleKeys[index++];
				require(actual.name == expected.name, "toggle key name or order changed");
				require(actual.virtualKey == expected.virtualKey, "toggle virtual key changed");
			}
		});

		runner.test("log level combo matches the public levels", [] {
			require(kProfilerMenuLogLevels.size() == kExpectedLogLevels.size(), "log level count changed");

			size_t index = 0;
			for (const auto& expected : kExpectedLogLevels)
			{
				const auto actual = kProfilerMenuLogLevels[index++];
				require(actual == expected.level, "log level order changed");
				require(LogControl::LevelName(actual) == expected.name, "public log level name changed");
			}
		});

		runner.test("toggle key parser accepts every supported name", [] {
			for (const auto& key : kExpectedToggleKeys)
			{
				const auto parsed = ParseProfilerMenuToggleKey(key.name);
				require(parsed.recognized, std::string("rejected ") + std::string(key.name));
				require(parsed.virtualKey == key.virtualKey, "wrong virtual key");
				require(
					ProfilerMenuToggleKeyName(parsed.virtualKey) == key.name,
					"virtual key did not round trip to its name");
			}
		});

		runner.test("toggle key parser ignores case", [] {
			require(ParseProfilerMenuToggleKey("f11"sv).virtualKey == 0x7A, "lower case name rejected");
			require(ParseProfilerMenuToggleKey("hOmE"sv).virtualKey == 0x24, "mixed case name rejected");
			require(ParseProfilerMenuToggleKey("DELETE"sv).virtualKey == 0x2E, "upper case name rejected");
		});

		runner.test("unsupported toggle keys fall back to F11", [] {
			for (const auto name : { ""sv, "F0"sv, "F13"sv, "Escape"sv, "PageUp"sv, "F1 "sv }) {
				const auto parsed = ParseProfilerMenuToggleKey(name);
				require(!parsed.recognized, "unsupported name was accepted");
				require(parsed.virtualKey == 0x7A, "fallback is not F11");
			}
		});

		runner.test("refresh interval clamps to its documented range", [] {
			require(ClampProfilerMenuRefreshMs(0) == 100, "zero did not clamp up");
			require(ClampProfilerMenuRefreshMs(99) == 100, "below range did not clamp up");
			require(ClampProfilerMenuRefreshMs(100) == 100, "lower bound moved");
			require(ClampProfilerMenuRefreshMs(250) == 250, "in-range value changed");
			require(ClampProfilerMenuRefreshMs(2000) == 2000, "upper bound moved");
			require(ClampProfilerMenuRefreshMs(5000) == 2000, "above range did not clamp down");
		});

		runner.test("formatted text length never exceeds its buffer", [] {
			require(ClampProfilerMenuFormattedLength(-1, 48) == 0, "format failure returned a length");
			require(ClampProfilerMenuFormattedLength(0, 48) == 0, "empty output returned a length");
			require(ClampProfilerMenuFormattedLength(20, 48) == 20, "short output was truncated");
			require(ClampProfilerMenuFormattedLength(47, 48) == 47, "exact output was truncated");
			require(ClampProfilerMenuFormattedLength(48, 48) == 47, "terminator was counted as text");
			require(ClampProfilerMenuFormattedLength(80, 48) == 47, "long output escaped the buffer");
			require(ClampProfilerMenuFormattedLength(1, 0) == 0, "zero-capacity output returned a length");
		});

		runner.test("a closed menu never refreshes", [] {
			constexpr uint64_t frequency = 10'000'000;
			require(
				!ShouldRefreshPanel(false, true, false, frequency * 10, 0, frequency, 250),
				"a closed menu refreshed with no cached data");
			require(
				!ShouldRefreshPanel(false, true, true, frequency * 10, 0, frequency, 250),
				"a closed menu refreshed stale data");
		});

		runner.test("an open panel refreshes once per cadence", [] {
			constexpr uint64_t frequency = 10'000'000;
			constexpr uint64_t last = frequency;
			require(
				ShouldRefreshPanel(true, true, false, last, last, frequency, 250),
				"the first open did not populate the cache");
			require(
				!ShouldRefreshPanel(true, true, true, last + frequency / 100, last, frequency, 250),
				"10 ms was enough to refresh a 250 ms cadence");
			require(
				!ShouldRefreshPanel(true, true, true, last + (frequency * 249) / 1000, last, frequency, 250),
				"249 ms was enough to refresh a 250 ms cadence");
			require(
				ShouldRefreshPanel(true, true, true, last + frequency / 4, last, frequency, 250),
				"250 ms did not refresh");
			require(
				!ShouldRefreshPanel(true, true, true, last, last, frequency, 250),
				"an unchanged counter refreshed");
			require(
				!ShouldRefreshPanel(true, true, true, last + (frequency * 75) / 1000, last, frequency, 50),
				"an out-of-range cadence was not clamped to 100 ms");
			require(
				ShouldRefreshPanel(true, true, true, last + frequency * 3, last, frequency, 5000),
				"an out-of-range cadence was not clamped to 2000 ms");
		});

		runner.test("only the active panel decides to refresh", [] {
			constexpr uint64_t frequency = 10'000'000;
			size_t refreshes = 0;
			auto active = ProfilerMenuTab::kOverview;
			std::array<uint64_t, kProfilerMenuTabCount> refreshedAt{};
			std::array<bool, kProfilerMenuTabCount> hasData{};

			const auto step = [&](uint64_t a_nowQpc) {
				for (size_t index = 0; index < kProfilerMenuTabCount; ++index)
				{
					const auto tab = static_cast<ProfilerMenuTab>(index);
					if (!ShouldRefreshPanel(
							true,
							tab == active,
							hasData[index],
							a_nowQpc,
							refreshedAt[index],
							frequency,
							250))
						continue;

					hasData[index] = true;
					refreshedAt[index] = a_nowQpc;
					++refreshes;
				}
			};

			step(frequency);
			step(frequency + frequency / 100);
			require(refreshes == 1, "the active panel refreshed twice inside one cadence");

			active = ProfilerMenuTab::kAllocator;
			step(frequency + frequency / 50);
			require(refreshes == 2, "switching panels did not populate the new panel");
			require(!hasData[static_cast<size_t>(ProfilerMenuTab::kMemory)], "an inactive panel was refreshed");
		});

		runner.test("runtime channel copies without draining or exporting", [] {
			const auto directory = temporary_directory("runtime-channel");
			std::error_code ec;
			std::filesystem::remove_all(directory, ec);

			RuntimeSessionContext session;
			session.Start("session", directory.string() + "\\");
			RuntimeChannel<int> channel{ session, 4, "copy_check"sv, write_header, write_entry };

			std::vector<int> entries;
			int latest = -1;
			require(!channel.CopyLatest(latest), "an empty channel reported a latest entry");
			channel.CopyEntries(entries);
			require(entries.empty(), "an empty channel copied entries");
			std::vector<int> emptyProjection{ 99 };
			require(
				!channel.CopyLatestAndProject(
					latest,
					emptyProjection,
					[](int a_value) noexcept { return a_value; }),
				"an empty channel reported projected entries");
			require(emptyProjection.empty(), "an empty projected copy kept stale entries");

			for (int value = 1; value <= 6; ++value)
				channel.Record(int{ value }, false);

			require(channel.RetainedCount() == 4, "the channel retained more than its capacity");
			require(channel.CopyLatest(latest), "the latest entry is missing");
			require(latest == 6, "the latest entry is not the newest record");

			channel.CopyEntries(entries);
			require(entries.size() == 4, "the copy does not match the retained count");
			require(
				entries[0] == 3 && entries[1] == 4 && entries[2] == 5 && entries[3] == 6,
				"the copy is not in record order");

			// A copy is not a drain: repeating it must return the same rows.
			std::vector<int> second;
			channel.CopyEntries(second);
			require(second == entries, "a second copy differed from the first");
			require(channel.RetainedCount() == 4, "copying removed retained entries");
			require(channel.CopyLatest(latest) && latest == 6, "copying changed the latest entry");

			require(
				!std::filesystem::exists(directory, ec),
				"copying wrote CSV output while export was disabled");

			int projectedLatest = -1;
			std::vector<int> projected;
			require(
				channel.CopyLatestAndProject(
					projectedLatest,
					projected,
					[](int a_value) noexcept { return a_value * 10; }),
				"projected copy reported an empty channel");
			require(projectedLatest == 6, "projected copy did not return the latest entry");
			require(
				projected == std::vector<int>{ 30, 40, 50, 60 },
				"projected copy did not preserve retained order");
			projectedLatest = 0;
			projected.clear();
			require(
				channel.CopyLatestAndProject(
					projectedLatest,
					projected,
					[](int a_value) noexcept { return a_value * 10; }),
				"repeated projected copy drained the channel");
			require(projectedLatest == 6 && projected.back() == 60, "projected copy mutated retained entries");
		});

		runner.test("BA2 published store copies without draining", [] {
			require(!ShouldPublishBA2Interval(0, false), "an empty boundary became a published interval");
			require(ShouldPublishBA2Interval(1, false), "a measured interval was not published");
			require(ShouldPublishBA2Interval(0, true), "the final empty interval was not published");

			BA2PublishedStore store;
			BA2PublishedSnapshot empty;
			require(!store.CopyLatest(empty), "an empty store reported a publication");

			BA2Profile::ShardAggregate totals;
			totals.callsSeen = 12;
			totals.rowsWritten = 9;
			totals.rowsDropped = 3;
			totals.totalQpc = 4096;
			totals.unservedCalls = 1;
			totals.reasonCounts[BA2Profile::kReasonDecode] = 2;
			if (auto* backend = totals.backends.Get(BA2Profile::kBackendLibDeflate))
			{
				backend->servedCalls = 8;
				backend->servedQpc = 2048;
				backend->servedBucketCalls[3] = 8;
			}

			BA2Profile::Reconciliation reconciliation;
			reconciliation.rowsTruncated = true;

			const BA2Profile::SummaryContext context{
				"session"sv,
				"GameDataReady"sv,
				10'000'000,
				7,
				2,
				1000,
				5000,
				true,
				false
			};

			const auto snapshot = MakeBA2PublishedSnapshot(context, totals, reconciliation, 5, 1, 4);
			require(snapshot.valid, "the snapshot is not marked valid");
			require(snapshot.Reason() == "GameDataReady"sv, "the publish reason was not copied");
			require(snapshot.publishSequence == 7, "the publish sequence was not copied");
			require(snapshot.saveLoadEpoch == 2, "the epoch was not copied");
			require(snapshot.qpcFrequency == 10'000'000, "the qpc frequency was not copied");
			require(snapshot.IntervalMicroseconds() == 4000, "the interval length is wrong");
			require(snapshot.leasedShards == 5, "the leased shard count was not copied");
			require(snapshot.overflowedThreads == 1, "the overflowed thread count was not copied");
			require(snapshot.spillCalls == 4, "the spill call count was not copied");
			require(snapshot.totals.callsSeen == 12, "the totals were not copied");
			require(
				snapshot.totals.reasonCounts[BA2Profile::kReasonDecode] == 2,
				"the reason partition was not copied");
			require(snapshot.reconciliation.rowsTruncated, "the reconciliation flags were not copied");
			require(!snapshot.reconciliation.Ok(), "an unreconciled interval reported as reconciled");

			const auto* served = snapshot.totals.backends.Find(BA2Profile::kBackendLibDeflate);
			require(served && served->servedCalls == 8, "the backend partition was not copied");
			require(served && served->servedBucketCalls[3] == 8, "the output-size buckets were not copied");

			store.Retain(snapshot);
			BA2PublishedSnapshot firstRead;
			BA2PublishedSnapshot secondRead;
			require(store.CopyLatest(firstRead), "retained publication was not readable");
			require(store.CopyLatest(secondRead), "a read drained the retained publication");
			require(firstRead.publishSequence == 7, "the retained sequence changed");
			require(secondRead.totals.callsSeen == 12, "the retained aggregate changed between reads");
			firstRead.publishSequence = 0;
			firstRead.totals.callsSeen = 0;
			require(store.CopyLatest(secondRead), "mutating a copy drained the retained publication");
			require(
				secondRead.publishSequence == 7 && secondRead.totals.callsSeen == 12,
				"mutating a copy changed the retained publication");
			require(snapshot.totals.callsSeen == 12, "retaining mutated the source snapshot");
			require(totals.callsSeen == 12, "building a snapshot mutated the aggregate");

			auto longReason = MakeBA2PublishedSnapshot(
				BA2Profile::SummaryContext{
					"session"sv,
					"a-publish-reason-longer-than-the-fixed-snapshot-buffer"sv,
					1,
					0,
					0,
					0,
					0,
					false,
					false },
				totals,
				reconciliation,
				0,
				0,
				0);
			require(
				longReason.Reason().size() == 31,
				"an oversized reason was not truncated to the fixed buffer");
		});
	}
}
