project(ssp-connector)

include(FetchContent)
FetchContent_Declare(
  libssp
  GIT_REPOSITORY "https://github.com/imaginevision/libssp.git"
  GIT_TAG ee754be1d2ed54633ff467a7cd6fec14c9b0b537)

if(NOT libssp_POPULATED)
  FetchContent_Populate(libssp)
  add_library(libssp INTERFACE)
  target_include_directories(libssp INTERFACE ${libssp_SOURCE_DIR}/include)
  if(OS_WINDOWS)
    target_link_libraries(
      libssp INTERFACE "${libssp_SOURCE_DIR}/lib/win_x64_vs2017/libssp$<$<CONFIG:Debug>:d>.lib")
    set(LIBSSP_LIBRARY
        "${libssp_SOURCE_DIR}/lib/win_x64_vs2017/libssp$<$<CONFIG:Debug>:d>.lib"
        PARENT_SCOPE)
  elseif(OS_MACOS)
    target_link_libraries(libssp INTERFACE ${libssp_SOURCE_DIR}/lib/mac/libssp.dylib)
    set(LIBSSP_LIBRARY
        "${libssp_SOURCE_DIR}/lib/mac/libssp.dylib"
        PARENT_SCOPE)
  endif()
endif()

set(SSP_CONNECTOR_SOURCE main.cpp)

add_executable(ssp-connector ${SSP_CONNECTOR_SOURCE})
set_target_properties(ssp-connector PROPERTIES OSX_ARCHITECTURES x86_64)
target_link_libraries(ssp-connector PRIVATE libssp)
target_include_directories(ssp-connector PRIVATE libuv/include)

if(OS_WINDOWS)
  target_compile_definitions(ssp-connector PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
