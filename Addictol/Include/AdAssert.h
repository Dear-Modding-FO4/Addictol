#pragma once

void AdAssertMsg(const char* SourceFile, int SourceLine, const char* Function, const char* FormattedMessage, ...) noexcept;

#define AdAssert(Cond)									if(!(Cond)) AdAssertMsg(__FILE__, __LINE__, __FUNCTION__, #Cond)
#define AdAssertWithFormattedMessage(Cond, Msg, ...)	if(!(Cond)) AdAssertMsg(__FILE__, __LINE__, __FUNCTION__, "%s\n\n" Msg, #Cond, ##__VA_ARGS__)
#define AdAssertWithMessage(Cond, Msg)					AdAssert(Cond, Msg)