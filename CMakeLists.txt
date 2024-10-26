cmake_minimum_required(VERSION 3.16...3.26)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)

set(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
set(ENABLE_QT "Use Qt functionality" ON)

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
  find_qt(COMPONENTS Widgets Core Network)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt::Core Qt::Widgets Qt::Network)
  target_compile_options(
    ${CMAKE_PROJECT_NAME} PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header
                                  -Wno-comma>)
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON
               AUTOUIC ON
               AUTORCC ON)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/Modules")

set(obs-ssp_SOURCES
    src/obs-ssp.cpp
    src/obs-ssp-source.cpp
    src/ffmpeg-decode.c
    src/controller/cameraconfig.cpp
    src/controller/cameracontroller.cpp
    src/ssp-mdns.cpp
    src/ssp-controller.cpp
    src/VFrameQueue.cpp
    src/ssp-client-iso.cpp)

set(obs-ssp_HEADERS src/obs-ssp.h src/ssp-mdns.h src/ssp-controller.h src/VFrameQueue.h
                    src/ssp-client.h)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${obs-ssp_SOURCES})

# /!\ TAKE NOTE: No need to edit things past this point /!\

# --- Platform-independent build settings ---
find_package(FFmpeg REQUIRED COMPONENTS AVCODEC AVUTIL)
add_subdirectory(thirdpty)

target_include_directories(
  ${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/lib/ssp/include
                                ${CMAKE_SOURCE_DIR}/ssp_connector)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_HEVC)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE FFmpeg::avcodec FFmpeg::avutil mdns)

add_subdirectory(ssp_connector)

if(OS_MACOS)
  install(TARGETS ssp-connector DESTINATION "./${CMAKE_PROJECT_NAME}.plugin/Contents/MacOS")
  install(FILES ${LIBSSP_LIBRARY} DESTINATION "./${CMAKE_PROJECT_NAME}.plugin/Contents/Frameworks")
elseif(OS_WINDOWS)
  install(TARGETS ssp-connector DESTINATION "obs-plugins/64bit")
  install(FILES ${LIBSSP_LIBRARY} DESTINATION "obs-plugins/64bit")
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
