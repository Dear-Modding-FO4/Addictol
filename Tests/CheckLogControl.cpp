#include "../Addictol/Include/Core/AdLogControl.h"
#include "Harness.h"

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

		runner.test("log control levels round-trip through names", [] {
			require(LevelName(Level::kTrace) == "trace", "trace level has the wrong name");
			require(LevelName(Level::kDebug) == "debug", "debug level has the wrong name");
			require(LevelName(Level::kInfo) == "info", "info level has the wrong name");
			require(LevelName(Level::kWarn) == "warn", "warn level has the wrong name");
			require(LevelName(Level::kError) == "error", "error level has the wrong name");
			require(LevelName(Level::kCritical) == "critical", "critical level has the wrong name");
			require(LevelName(Level::kOff) == "off", "off level has the wrong name");
			require(ParseLevel("trace") == Level::kTrace, "trace did not parse");
			require(ParseLevel("debug") == Level::kDebug, "debug did not parse");
			require(ParseLevel("info") == Level::kInfo, "info did not parse");
			require(ParseLevel("warn") == Level::kWarn, "warn did not parse");
			require(ParseLevel("error") == Level::kError, "error did not parse");
			require(ParseLevel("critical") == Level::kCritical, "critical did not parse");
			require(ParseLevel("off") == Level::kOff, "off did not parse");
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

		runner.test("log control counting sink counts writes and flushes", [] {
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
		});

		runner.test("log control stats copies are non-destructive", [] {
			const auto first = CopyStats();
			const auto second = CopyStats();
			require(first.written > 0, "stats fixture has no counted write");
			require(second.written == first.written, "reading stats reset the written count");
			require(second.flushed == first.flushed, "reading stats reset the flushed count");
		});
	}
}
