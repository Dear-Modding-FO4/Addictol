#pragma once

#include <AdModule.h>

namespace Addictol
{
	class ModuleGreyMovie :
		public Module
	{
	public:
		ModuleGreyMovie();
		virtual ~ModuleGreyMovie() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}