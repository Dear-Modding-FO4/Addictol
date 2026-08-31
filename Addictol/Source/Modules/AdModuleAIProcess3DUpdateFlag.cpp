#include <Modules/AdModuleAIProcess3DUpdateFlag.h>
#include <Core/AdUtils.h>

#include <RE/A/AIProcess.h>
#include <RE/M/MiddleHighProcessData.h>

namespace Addictol
{

	namespace Detail
	{
		// Fixes an error where there is no check for nullptr when AI for a character is disabled.

		class __declspec(novtable) AIProcess :
			public RE::AIProcess
		{
		public:
			void Set3DUpdateFlag(uint16_t a_update3DModel) noexcept
			{
				if (!this || !this->middleHigh)
					return;

				this->middleHigh->update3DModel |= a_update3DModel;
			}

			void Clear3DUpdateFlag(uint16_t a_update3DModel) noexcept
			{
				if (!this || !this->middleHigh)
					return;

				this->middleHigh->update3DModel &= ~a_update3DModel;
			}

			void ClearAll3DUpdateFlags() noexcept
			{
				if (!this || !this->middleHigh)
					return;

				this->middleHigh->update3DModel = 0;
			}

			[[nodiscard]] bool Get3DUpdateFlag(uint16_t a_update3DModel) const noexcept
			{
				if (!this || !this->middleHigh)
					return false;

				return (this->middleHigh->update3DModel & a_update3DModel) != 0;
			}

			[[nodiscard]] uint16_t GetAll3DUpdateFlags() const noexcept
			{
				if (!this || !this->middleHigh)
					return 0;

				return this->middleHigh->update3DModel;
			}
		};
	}

	ModuleAIProcess3DUpdateFlag::ModuleAIProcess3DUpdateFlag() :
		Module("AIProcess 3DUpdateFlag", &bFixesAIProcess3DUpdateFlag)
	{}

	bool ModuleAIProcess3DUpdateFlag::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (a_msg)
			return false;

		const auto targetSet		= REL::ID{ 236542,	2232389 }.address();
		const auto targetClear		= REL::ID{ 1113891,	2232390 }.address();
		const auto targetClearAll	= REL::ID{ 409001,	2232391 }.address();
		const auto targetGet		= REL::ID{ 1286688,	2232392 }.address();
		const auto targetGetAll		= REL::ID{ 582098,	2232393 }.address();
		const auto checkCode		= std::initializer_list<uint8_t>{ 0x48, 0x8B, 0x41, 0x08 };

		if (!RELEX::Validate(targetSet,			checkCode) ||
			!RELEX::Validate(targetClear,		checkCode) ||
			!RELEX::Validate(targetClearAll,	checkCode) ||
			!RELEX::Validate(targetGet,			checkCode) ||
			!RELEX::Validate(targetGetAll,		checkCode))
			return false;

		return 
			(RELEX::DetourClassJump(targetSet,		&Detail::AIProcess::Set3DUpdateFlag			) != 0) &&
			(RELEX::DetourClassJump(targetClear,	&Detail::AIProcess::Clear3DUpdateFlag		) != 0) &&
			(RELEX::DetourClassJump(targetClearAll,	&Detail::AIProcess::ClearAll3DUpdateFlags	) != 0) &&
			(RELEX::DetourClassJump(targetGet,		&Detail::AIProcess::Get3DUpdateFlag			) != 0) &&
			(RELEX::DetourClassJump(targetGetAll,	&Detail::AIProcess::GetAll3DUpdateFlags		) != 0);
	}

}