#pragma once

#include <Core/AdModule.h>

namespace Addictol
{
	class ModuleSaveAddedSoundCategories :
		public Module
	{
	public:
		ModuleSaveAddedSoundCategories();
		virtual ~ModuleSaveAddedSoundCategories() = default;

		[[nodiscard]] virtual bool DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg = nullptr) noexcept override;
	};
}