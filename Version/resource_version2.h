// Copyright © 2023-2024 aka perchik71. All rights reserved.
// Contacts: <email:timencevaleksej@gmail.com>
// License: https://www.gnu.org/licenses/gpl-3.0.html

#pragma once

#define STRINGIZE2(s) #s
#define STRINGIZE(s) STRINGIZE2(s)

#define VERSION_MAJOR			1
#define VERSION_MINOR			6
#define VERSION_PATCH			0
#define VERSION_REVISION		0

// CommonLib/F4SE packs major/minor into 8 bits, patch into 12, and revision into 4.
#if VERSION_MAJOR < 0 || VERSION_MAJOR > 255
#error VERSION_MAJOR must be in the range 0..255
#endif

#if VERSION_MINOR < 0 || VERSION_MINOR > 255
#error VERSION_MINOR must be in the range 0..255
#endif

#if VERSION_PATCH < 0 || VERSION_PATCH > 4095
#error VERSION_PATCH must be in the range 0..4095
#endif

#if VERSION_REVISION < 0 || VERSION_REVISION > 15
#error VERSION_REVISION must be in the range 0..15
#endif

#define VER_FILE_VERSION		VERSION_MAJOR,	VERSION_MINOR,	VERSION_PATCH,	VERSION_REVISION
#define VER_PRODUCT_VERSION		VER_FILE_VERSION

#define VER_FILE_VERSION_STR	\
	STRINGIZE(VERSION_MAJOR)	\
"." STRINGIZE(VERSION_MINOR)	\
"." STRINGIZE(VERSION_PATCH)	\
"." STRINGIZE(VERSION_REVISION)

#define VER_PRODUCT_VERSION_STR	VER_FILE_VERSION_STR
