// Copyright © 2023-2024 aka perchik71. All rights reserved.
// Contacts: <email:timencevaleksej@gmail.com>
// License: https://www.gnu.org/licenses/gpl-3.0.html

#pragma once

#define STRINGIZE2(s) #s
#define STRINGIZE(s) STRINGIZE2(s)

#define VERSION_MAJOR			1
#define VERSION_MINOR			6

#ifndef VERSION_BUILD
#define VERSION_BUILD			0
#endif

#define VERSION_REVISION		0

// F4SE stores the patch field in 12 bits.
#if VERSION_BUILD < 0 || VERSION_BUILD > 4095
#error VERSION_BUILD must be in the range 0..4095
#endif

#define VER_FILE_VERSION		VERSION_MAJOR,	VERSION_MINOR,	VERSION_BUILD,	VERSION_REVISION
#define VER_PRODUCT_VERSION		VER_FILE_VERSION

#define VER_FILE_VERSION_STR	\
	STRINGIZE(VERSION_MAJOR)	\
"." STRINGIZE(VERSION_MINOR)	\
"." STRINGIZE(VERSION_BUILD)	\
"." STRINGIZE(VERSION_REVISION)

#define VER_PRODUCT_VERSION_STR	VER_FILE_VERSION_STR
