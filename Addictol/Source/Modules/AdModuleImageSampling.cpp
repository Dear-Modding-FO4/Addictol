#include <Modules/AdModuleImageSampling.h>
#include <Core/AdUtils.h>

namespace Addictol
{
	ModuleImageSampling::ModuleImageSampling() :
		Module("Image Sampling", &bTelemetryImageSampling)
	{}

	bool ModuleImageSampling::DoInstall([[maybe_unused]] F4SE::MessagingInterface::Message* a_msg) noexcept
	{
		if (UserUseWine())
		{
			Skip("CPU image sampling is unavailable under Wine"sv);
			return false;
		}
		return Start(uTelemetryImageSampleHz.GetValue(), GetCurrentThreadId());
	}
}
