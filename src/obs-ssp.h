/*
obs-ssp
 Copyright (C) 2019-2020 Yibai Zhang

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; If not, see <https://www.gnu.org/licenses/>
*/

#ifndef OBSSSP_H
#define OBSSSP_H

#include <string>
#include "imf/ISspClient.h"
#include <stdint.h>
#include "plugin-support.h"

#if INTPTR_MAX == INT64_MAX
#define OBS_SSP_BITSTR "64bit"
#elif INTPTR_MAX == INT32_MAX
#define OBS_SSP_BITSTR "32bit"
#else
#error Unknown pointer size or missing size macros!
#endif

#define ssp_blog(level, msg, ...) \
	blog(level, "[%s] " msg, PLUGIN_NAME, ##__VA_ARGS__)

extern create_ssp_class_ptr create_ssp_class;
extern create_loop_class_ptr create_loop_class;

#ifdef _WIN64
#define LIBSSP_LIBRARY_NAME "../../obs-plugins/" OBS_SSP_BITSTR "/libssp.dll"
#elif defined(__APPLE__)
#define LIBSSP_LIBRARY_NAME "../Frameworks/libssp.dylib"
#else
#define LIBSSP_LIBRARY_NAME "libssp.so"
#endif

#define ZCAM_QUERY_DOMAIN "_eagle._tcp.local"

#endif // OBSSSP_H
