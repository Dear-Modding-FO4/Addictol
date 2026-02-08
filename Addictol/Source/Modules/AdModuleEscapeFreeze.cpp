#include <Modules\AdModuleEscapeFreeze.h>
#include <AdUtils.h>

#include <thread>
#include <atomic>

namespace Addictol
{
	static REX::TOML::Bool<> bFixesEscapeFreeze{ "Fixes", "bEscapeFreeze", true };
	static REX::TOML::I32<> nAdditionalSleepTimer{ "Additional", "nSleepTimer", 125 };
	static REX::TOML::I32<> nAdditionalMaxLockCount{ "Additional", "nMaxLockCount", 8 };

	REL::Relocation<int*> ConditionLockCountPointer{ REL::ID{ 998070, 2692050, 4799342 } };

	static void FreezeWatcherThread()
	{
		REX::INFO("Started FreezeWatcher Thread");

		int LoopCounter = 0;
		bool Escaped = false;

		while (true)
		{
			// Are there any Locks?
			if (*ConditionLockCountPointer == 0)
			{
				if (Escaped)
				{
					// There was and we Escaped
					REX::INFO("Successfully Escaped from Freezing!");
				}

				// Reset
				LoopCounter = 0;
				Escaped = false;

				// Sleep
				std::this_thread::sleep_for(std::chrono::milliseconds(nAdditionalSleepTimer.GetValue()));
				continue;
			}

			// Lock Detected!
			REX::INFO("Lock Detected! Lock Count: {}, Loop Count: {}", *ConditionLockCountPointer, LoopCounter++);

			// Exceeded the Threshold
			if (LoopCounter > nAdditionalMaxLockCount.GetValue())
			{
				REX::INFO("Exceeded Threshold, Unlocking...");

				*ConditionLockCountPointer = 0;
				Escaped = true;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(nAdditionalSleepTimer.GetValue()));
		}
	}

	ModuleEscapeFreeze::ModuleEscapeFreeze() :
		Module("Module Escape Freeze", &bFixesEscapeFreeze)
	{}

	bool ModuleEscapeFreeze::DoQuery() const noexcept
	{
		return true;
	}

	bool ModuleEscapeFreeze::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		REX::INFO("Starting FreezeWatcher Thread");
		std::thread(FreezeWatcherThread).detach();

		return true;
	}

	bool ModuleEscapeFreeze::DoListener([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		return true;
	}
}