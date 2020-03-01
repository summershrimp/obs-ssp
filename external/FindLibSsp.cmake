# This module is used to find libssp
#
# Once done these will be defined:
#
#  LIBSSP_FOUND
#  LIBSSP_INCLUDE_DIRS
#  LIBSSP_LIBRARIES

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_lib_suffix 64)
else()
    message(FATAL_ERROR "libssp does not provide 32bit version.")
endif()

find_path(LIBSSP_INCLUDE_DIR
        NAMES imf/ssp/sspclient.h
        HINTS
        ENV LIBSSP_DIR
        ${LIBSSP_DIR}/include
        PATHS
        /usr/include /usr/local/include /opt/local/include /sw/include
        )

set(LIBSSP_LIB_NAME "libsspd")

if(MSVC)
    SET(LIBSSP_LIB_SUFFIX "win_x64_vs2017")
    if(CMAKE_BUILD_TYPE EQUAL "Debug")
        set(LIBSSP_LIB_NAME "libsspd")
    endif()
else()
    SET(LIBSSP_LIB_SUFFIX "linux_x64")
endif()

find_library(LIBSSP_LIB
        NAMES ${LIBSSP_LIB_NAME}
        HINTS
        ENV LIBSSP_DIR
        ${LIBSSP_DIR}/lib
        PATH_SUFFIXES
        ${LIBSSP_LIB_SUFFIX}
        )

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibSsp DEFAULT_MSG LIBSSP_LIB LIBSSP_INCLUDE_DIR)
mark_as_advanced(LIBSSP_INCLUDE_DIR LIBSSP_LIB)


if(LIBSSP_FOUND)
    set(LIBSSP_LIBRARIES ${LIBSSP_LIB} )
else()
    message(FATAL_ERROR "Could not find the libssp library" )
endif()
