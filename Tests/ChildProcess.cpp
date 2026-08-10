#include "Harness.h"

#include <Windows.h>

#include <sstream>
#include <string>
#include <vector>

namespace
{
	std::wstring executable_path()
	{
		std::wstring path(32768, L'\0');
		const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
		vmm_tests::require(length != 0 && length < path.size(), "GetModuleFileNameW failed");
		path.resize(length);
		return path;
	}

	std::wstring widen(std::string_view value)
	{
		return { value.begin(), value.end() };
	}
}

namespace vmm_tests
{
	ChildProcessResult run_child_process(std::string_view argument)
	{
		constexpr DWORD child_timeout_ms = 120000;
		const auto executable = executable_path();
		std::wstring command = L"\"" + executable + L"\" " + widen(argument);
		std::vector<wchar_t> command_line(command.begin(), command.end());
		command_line.push_back(L'\0');

		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		require(
			CreateProcessW(
				executable.c_str(),
				command_line.data(),
				nullptr,
				nullptr,
				TRUE,
				0,
				nullptr,
				nullptr,
				&startup,
				&process) != FALSE,
			"CreateProcessW failed");

		const DWORD wait_result = WaitForSingleObject(process.hProcess, child_timeout_ms);
		if (wait_result == WAIT_TIMEOUT)
		{
			const BOOL terminated = TerminateProcess(process.hProcess, 1);
			if (terminated != FALSE)
				WaitForSingleObject(process.hProcess, 5000);
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);

			std::ostringstream stream;
			stream << "child process '" << argument << "' timed out after " << child_timeout_ms / 1000 << " seconds";
			if (terminated == FALSE)
				stream << " and could not be terminated";
			throw Failure(stream.str());
		}
		if (wait_result != WAIT_OBJECT_0)
		{
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);
			throw Failure("child process wait failed for '" + std::string(argument) + "'");
		}

		DWORD exit_code = 1;
		const BOOL got_exit_code = GetExitCodeProcess(process.hProcess, &exit_code);
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);

		require(got_exit_code != FALSE, "child process exit code was unavailable for '" + std::string(argument) + "'");
		return { exit_code };
	}
}
