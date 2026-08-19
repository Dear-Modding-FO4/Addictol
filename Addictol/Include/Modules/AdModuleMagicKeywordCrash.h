#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleMagicKeywordCrash :
		public Module
	{
	public:
		ModuleMagicKeywordCrash();
		virtual ~ModuleMagicKeywordCrash() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}
