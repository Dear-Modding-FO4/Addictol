#include <AdPlugin.h>
#include <REX\REX\Singleton.h>

namespace Addictol
{
	bool Plugin::Init(const F4SE::LoadInterface* a_f4se)
	{
		if (isInit)
			return true;

		F4SE::Init(a_f4se);
		REX::INFO("" _PluginName " mod (ver: " VER_FILE_VERSION_STR ") Initializing...");

		// TODO

		isInit = true;
		return isInit;
	}
}