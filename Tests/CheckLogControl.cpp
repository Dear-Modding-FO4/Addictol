#include "../Addictol/Include/Core/AdLogControl.h"
#include "Harness.h"

#include <Menu/AdMenuTargets.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include <memory>

namespace vmm_tests
{
	void run_log_control_checks(Runner& runner)
	{
		using Addictol::LogControl::Level;
		using namespace Addictol::LogControl;

		runner.test("log levels preserve public names, parsing, and menu order", [] {
			struct ExpectedLevel
			{
				Level level;
				std::string_view name;
			};
			constexpr std::array expected{
				ExpectedLevel{ Level::kTrace, "trace" },
				ExpectedLevel{ Level::kDebug, "debug" },
				ExpectedLevel{ Level::kInfo, "info" },
				ExpectedLevel{ Level::kWarn, "warn" },
				ExpectedLevel{ Level::kError, "error" },
				ExpectedLevel{ Level::kCritical, "critical" },
				ExpectedLevel{ Level::kOff, "off" }
			};
			const auto& menuLevels = Addictol::kMenuLogLevels;
			require(menuLevels.size() == expected.size(), "log level count changed");
			for (size_t index = 0; index < expected.size(); ++index)
			{
				require(menuLevels[index] == expected[index].level, "log level order changed");
				require(LevelName(expected[index].level) == expected[index].name,
					"public log level name changed");
				require(ParseLevel(expected[index].name) == expected[index].level,
					"public log level name did not parse");
			}
		});

		runner.test("log control parsing is case-insensitive and rejects unknown names", [] {
			require(ParseLevel("TrAcE") == Level::kTrace, "mixed-case trace did not parse");
			require(ParseLevel("WARN") == Level::kWarn, "uppercase warn did not parse");
			require(ParseLevel("Critical") == Level::kCritical, "title-case critical did not parse");
			require(!ParseLevel("warning"), "unsupported warning alias parsed");
			require(!ParseLevel("verbose"), "unknown verbose level parsed");
			require(!ParseLevel(""), "empty level parsed");
		});

		const auto nullSink = std::make_shared<spdlog::sinks::null_sink_mt>();
		spdlog::set_default_logger(std::make_shared<spdlog::logger>("log-control-tests", nullSink));
		Install();

		runner.test("log control applies runtime levels", [] {
			SetLevel(Level::kDebug);
			require(GetLevel() == Level::kDebug, "runtime log level did not change");
			SetFlushLevel(Level::kError);
			require(GetFlushLevel() == Level::kError, "runtime flush level did not change");
			SetLevel(Level::kInfo);
			SetFlushLevel(Level::kInfo);
		});

		runner.test("log control counts writes and flushes without destructive reads", [] {
			SetLevel(Level::kInfo);
			SetFlushLevel(Level::kOff);
			const auto before = CopyStats();
			spdlog::default_logger()->info("counted test line");
			const auto afterWrite = CopyStats();
			require(afterWrite.written == before.written + 1, "counting sink missed a written line");
			require(afterWrite.flushed == before.flushed, "write unexpectedly counted as a flush");

			spdlog::default_logger()->flush();
			const auto afterFlush = CopyStats();
			require(afterFlush.written == afterWrite.written, "flush unexpectedly counted as a write");
			require(afterFlush.flushed == afterWrite.flushed + 1, "counting sink missed a flush");
			const auto copied = CopyStats();
			require(afterFlush.written > 0, "stats fixture has no counted write");
			require(copied.written == afterFlush.written, "reading stats reset the written count");
			require(copied.flushed == afterFlush.flushed, "reading stats reset the flushed count");
		});
	}
}
