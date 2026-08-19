#include <Core/AdAssert.h>
#include <windows.h>
#include <memory.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

static constexpr auto SIZE_ASSERT_BUFFERS = 4 * 1024;
static char AdAssertBuffer[SIZE_ASSERT_BUFFERS]{};
static char AdAssertMessage[SIZE_ASSERT_BUFFERS]{};

static int ReportAssertion([[maybe_unused]] const char* SourceFile, [[maybe_unused]] int SourceLine, 
    [[maybe_unused]] const char* Function, const char* Message) noexcept
{
    return MessageBoxA(nullptr, Message, "Assertion", MB_ABORTRETRYIGNORE | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
}

void AdAssertMsg(const char* SourceFile, int SourceLine, const char* Function, const char* FormattedMessage, ...) noexcept
{
    memset(AdAssertBuffer, 0, 1024);
    memset(AdAssertMessage, 0, 1024);

    va_list ap;
    va_start(ap, FormattedMessage);
    vsnprintf(AdAssertBuffer, SIZE_ASSERT_BUFFERS, FormattedMessage, ap);
    sprintf(AdAssertMessage, "%s(%d) %s():\n\n%s", SourceFile, SourceLine, Function, AdAssertBuffer);
    va_end(ap);

    while (1)
    {
        const auto sdl_assert_state = ReportAssertion(SourceFile, SourceLine, Function, AdAssertMessage);
        
        if (sdl_assert_state == IDRETRY) 
            // Fuck user
            continue;
        else if (sdl_assert_state == IDABORT)
        {
            // For debugger
            __debugbreak();
            // For Wine
            abort();
            // AGAIN!!!
            TerminateProcess(GetCurrentProcess(), EXIT_FAILURE);
            // CTD
            *((int*)0) = 0;
        }
            
        // Skip
        break;
    }
}