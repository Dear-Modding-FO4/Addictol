#include <Windows.h>
#include "AdUtils.h"

std::string GetRuntimePath() noexcept
{
    static char	appPath[4096] = { 0 };

    if (appPath[0])
        return appPath;

    GetModuleFileName(GetModuleHandle(NULL), appPath, sizeof(appPath));
    return appPath;
}

std::string GetRuntimeDirectory() noexcept
{
    std::string runtimeDirectory;

    if (runtimeDirectory.empty())
    {
        std::string	runtimePath = GetRuntimePath();
        // truncate at last slash
        std::string::size_type	lastSlash = runtimePath.rfind('\\');
        if (lastSlash != std::string::npos)
            // if we don't find a slash something is VERY WRONG
            runtimeDirectory = runtimePath.substr(0, lastSlash + 1);
    }

    return runtimeDirectory;
}