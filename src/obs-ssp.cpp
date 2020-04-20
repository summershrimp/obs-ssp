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

#ifdef _WIN32
#include <Windows.h>
#endif

#include <sys/stat.h>
#include <obs-module.h>
#include <util/platform.h>

#include "obs-ssp.h"

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Yibai Zhang")
OBS_MODULE_USE_DEFAULT_LOCALE("obs-ssp", "en-US")

extern struct obs_source_info create_ssp_source_info();
struct obs_source_info ssp_source_info;

create_ssp_class_ptr create_ssp_class;
create_loop_class_ptr create_loop_class;



bool obs_module_load(void)
{
	blog(LOG_INFO, "hello ! (obs-ssp version %s) size: %d", OBS_SSP_VERSION, sizeof(ssp_source_info));
    void *ssp_handle = os_dlopen(LIBSSP_LIBRARY_NAME);
    if(!ssp_handle){
        blog(LOG_WARNING, "Load %s failed.", LIBSSP_LIBRARY_NAME);
        return false;
    }
    create_ssp_class = (create_ssp_class_ptr)os_dlsym(ssp_handle, "create_ssp_class");
    if(!create_ssp_class){
        blog(LOG_WARNING, "Cannot find create_ssp_class() in %s.", LIBSSP_LIBRARY_NAME);
        return false;
    }

    create_loop_class = (create_loop_class_ptr)os_dlsym(ssp_handle, "create_loop_class");
    if(!create_loop_class){
        blog(LOG_WARNING, "Cannot find create_loop_class() in %s.", LIBSSP_LIBRARY_NAME);
        return false;
    }

    blog(LOG_INFO, "libssp load successful!");

	ssp_source_info = create_ssp_source_info();
	obs_register_source(&ssp_source_info);
	return true;
}

void obs_module_unload()
{
	blog(LOG_INFO, "goodbye !");
}

const char* obs_module_name()
{
	return "obs-ssp";
}

const char* obs_module_description()
{
	return "Z CAM SSP input integration for OBS Studio";
}
