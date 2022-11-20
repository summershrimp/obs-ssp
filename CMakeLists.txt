cmake_minimum_required(VERSION 3.16...3.21)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/Modules")

# Change obs-plugintemplate to your plugin's name in a machine-readable format (e.g.:
# obs-myawesomeplugin) and set
project(obs-ssp VERSION 0.9.5)
add_library(${CMAKE_PROJECT_NAME} MODULE)

# Replace `Your Name Here` with the name (yours or your organization's) you want to see as the
# author of the plugin (in the plugin's metadata itself and in the installers)
set(PLUGIN_AUTHOR "Yibai Zhang")

# Replace `com.example.obs-plugin-template` with a unique Bundle ID for macOS releases (used both in
# the installer and when submitting the installer for notarization)
set(MACOS_BUNDLEID "com.summershrimp.${CMAKE_PROJECT_NAME}")

# Replace `me@contoso.com` with the maintainer email address you want to put in Linux packages
set(LINUX_MAINTAINER_EMAIL "xm1994@gmail.com")

# Add your custom source files here - header files are optional and only required for visibility
# e.g. in Xcode or Visual Studio

set(obs-ssp_SOURCES
    src/obs-ssp.cpp
    src/obs-ssp-source.cpp
    src/ffmpeg-decode.c
    src/controller/cameraconfig.cpp
    src/controller/cameracontroller.cpp
    src/ssp-mdns.cpp
    src/ssp-controller.cpp
    src/VFrameQueue.cpp
    src/ssp-client.cpp)

set(obs-ssp_HEADERS src/obs-ssp.h src/ssp-mdns.h src/ssp-controller.h src/VFrameQueue.h
                    src/ssp-client.h)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${obs-ssp_SOURCES})

# Import libobs as main plugin dependency
find_package(libobs REQUIRED)
include(cmake/ObsPluginHelpers.cmake)

# Uncomment these lines if you want to use the OBS Frontend API in your plugin
#[[
find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
#]]

# Uncomment those lines if you want to use Qt in your plugin

find_qt(COMPONENTS Widgets Core Network)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt::Core Qt::Widgets Qt6::Network)
set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC ON
             AUTOUIC ON
             AUTORCC ON)

configure_file(src/plugin-macros.h.in ${CMAKE_SOURCE_DIR}/src/plugin-macros.generated.h)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-macros.generated.h)

# /!\ TAKE NOTE: No need to edit things past this point /!\

# --- Platform-independent build settings ---
find_package(FFmpeg REQUIRED COMPONENTS avcodec avutil)
add_subdirectory(thirdpty)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src
                                                         ${CMAKE_SOURCE_DIR}/lib/ssp/include)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs FFmpeg::avcodec FFmpeg::avutil mdns)

# --- End of section ---

# --- Windows-specific build settings and tasks ---
if(OS_WINDOWS)
  configure_file(cmake/bundle/windows/installer-Windows.iss.in
                 ${CMAKE_BINARY_DIR}/installer-Windows.generated.iss)

  configure_file(cmake/bundle/windows/resource.rc.in ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.rc)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.rc)

  if(MSVC)
    find_package(w32-pthreads REQUIRED)
    get_target_property(TEST OBS::w32-pthreads INTERFACE_INCLUDE_DIRECTORIES)
    message(STATUS "w32-pthreads include dir: ${TEST}")
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::w32-pthreads)
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE /W4)
  endif()
  # --- End of section ---

  # -- macOS specific build settings and tasks --
elseif(OS_MACOS)
  configure_file(cmake/bundle/macos/installer-macos.pkgproj.in
                 ${CMAKE_BINARY_DIR}/installer-macos.generated.pkgproj)

  set(MACOSX_PLUGIN_GUI_IDENTIFIER "${MACOS_BUNDLEID}")
  set(MACOSX_PLUGIN_BUNDLE_VERSION "${CMAKE_PROJECT_VERSION}")
  set(MACOSX_PLUGIN_SHORT_VERSION_STRING "1")

  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wall)
  # --- End of section ---

  # --- Linux-specific build settings and tasks ---
else()
  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wall)
endif()
# --- End of section ---

setup_plugin_target(${CMAKE_PROJECT_NAME})

if(OS_WINDOWS)
  set(LIBSSP_DOWNLOAD_ADDR
      "https://github.com/imaginevision/libssp/raw/fa0affd3858049a7773995c38bb454f989e7c8f4/lib/win_x64_vs2017/libssp.dll"
  )
  set(LIBSSP_FILENAME "libssp.dll")
  set(LIBSSP_HASH "SHA256=905cdccf06d8eededa18c26b21395404e64a086cc3e87679d4bd8cd38c7497d3")
  set(LIBSSP_INSTALL_PREFIX "${OBS_PLUGIN_DESTINATION}")
elseif(OS_MACOS)
  set(LIBSSP_DOWNLOAD_ADDR
      "https://github.com/summershrimp/obs-deps/releases/download/2021-04-03/libssp.dylib")
  set(LIBSSP_FILENAME "libssp.dylib")
  set(LIBSSP_HASH "SHA256=5e04060a8f248785202b0b097cc52a0c23c2f8cff8796c9d3c0672da28d9cf0c")
  set(LIBSSP_INSTALL_PREFIX "./${CMAKE_PROJECT_NAME}.plugin/Contents/Frameworks")
endif()

if(LIBSSP_DOWNLOAD_ADDR)
  install(
    CODE "file(DOWNLOAD \"${LIBSSP_DOWNLOAD_ADDR}\" \"${CMAKE_BINARY_DIR}/${LIBSSP_FILENAME}\" SHOW_PROGRESS EXPECTED_HASH \"${LIBSSP_HASH}\")
          file(INSTALL \"${CMAKE_BINARY_DIR}/${LIBSSP_FILENAME}\" DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${LIBSSP_INSTALL_PREFIX}\")"
  )
endif()
