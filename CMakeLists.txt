cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

if(NOT DEFINED CMAKE_COMPILE_WARNING_AS_ERROR)
  set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
endif()

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" ON)

if(NOT ENABLE_QT)
  message(FATAL_ERROR "obs-ssp requires Qt for camera control and discovery.")
endif()

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 REQUIRED COMPONENTS Widgets Core Network)

  # Qt's macOS package still links the legacy AGL framework, which was removed from the macOS 26 SDK.
  if(OS_MACOS AND TARGET WrapOpenGL::WrapOpenGL)
    get_target_property(_wrap_opengl_libraries WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    if(_wrap_opengl_libraries)
      list(FILTER _wrap_opengl_libraries EXCLUDE REGEX "(^|/)AGL\\.framework$|^-framework AGL$")
      set_property(TARGET WrapOpenGL::WrapOpenGL PROPERTY INTERFACE_LINK_LIBRARIES "${_wrap_opengl_libraries}")
    endif()
  endif()

  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOMOC_COMPILER_PREDEFINES OFF AUTOUIC ON AUTORCC ON
  )
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/Modules")

set(
  obs-ssp_SOURCES
  src/obs-ssp.cpp
  src/obs-ssp-source.cpp
  src/ffmpeg-decode.c
  src/controller/cameraconfig.cpp
  src/controller/cameracontroller.cpp
  src/ssp-mdns.cpp
  src/ssp-controller.cpp
  src/VFrameQueue.cpp
  src/ssp-client-iso.cpp
  src/util/pipe.c
)

if(OS_WINDOWS)
  list(APPEND obs-ssp_SOURCES src/util/pipe-windows.c)
else()
  list(APPEND obs-ssp_SOURCES src/util/pipe-posix.c)
endif()

set(
  obs-ssp_HEADERS
  src/obs-ssp.h
  src/ssp-mdns.h
  src/ssp-controller.h
  src/VFrameQueue.h
  src/ssp-client-iso.h
  src/util/pipe.h
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${obs-ssp_SOURCES} ${obs-ssp_HEADERS})

# Keep legacy decoder warnings visible without requiring changes to the excluded source.
set_source_files_properties(
  src/ffmpeg-decode.c
  PROPERTIES
    COMPILE_OPTIONS
      "$<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-error=switch>;$<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-error=newline-eof>"
)

# /!\ TAKE NOTE: No need to edit things past this point /!\

# --- Platform-independent build settings ---
find_package(FFmpeg REQUIRED COMPONENTS avcodec avutil)
add_subdirectory(thirdpty)

target_include_directories(
  ${CMAKE_PROJECT_NAME}
  PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/lib/ssp/include ${CMAKE_SOURCE_DIR}/ssp_connector
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_HEVC)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE FFmpeg::avcodec FFmpeg::avutil mdns)

add_subdirectory(ssp_connector)
add_dependencies(${CMAKE_PROJECT_NAME} ssp-connector)

if(OS_MACOS)
  install(TARGETS ssp-connector DESTINATION "./${CMAKE_PROJECT_NAME}.plugin/Contents/MacOS")
  if(LIBSSP_LIBRARY)
    install(FILES ${LIBSSP_LIBRARY} DESTINATION "./${CMAKE_PROJECT_NAME}.plugin/Contents/Frameworks")
  endif()
elseif(OS_WINDOWS)
  install(TARGETS ssp-connector DESTINATION "${CMAKE_PROJECT_NAME}/bin/64bit")
  if(LIBSSP_LIBRARY)
    install(FILES ${LIBSSP_LIBRARY} DESTINATION "${CMAKE_PROJECT_NAME}/bin/64bit")
  endif()
elseif(OS_LINUX)
  install(TARGETS ssp-connector DESTINATION ${CMAKE_INSTALL_BINDIR})
  if(LIBSSP_LIBRARY)
    install(FILES ${LIBSSP_LIBRARY} DESTINATION ${CMAKE_INSTALL_LIBDIR})
  endif()
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
