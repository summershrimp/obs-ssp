project(ssp-connector)

option(USE_NATIVE_SSP "Use native SSP implementation instead of closed-source libssp" OFF)
option(SSP_BUILD_TESTS "Build SSP protocol unit tests" OFF)

if(USE_NATIVE_SSP OR (OS_LINUX AND CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64"))
  # ── Native SSP connector (no libssp dependency) ──
  message(STATUS "Building native SSP connector (no libssp dependency)")

  find_package(OpenSSL REQUIRED)

  # SSP client library (compiled as object library for linking)
  add_library(ssp_client OBJECT ssp.c)
  target_include_directories(ssp_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                                               ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(ssp_client PUBLIC OpenSSL::SSL OpenSSL::Crypto)

  # ssp-connector executable (CLI + IPC bridge)
  add_executable(ssp-connector ssp_connector_main.c)
  target_include_directories(ssp-connector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(ssp-connector PRIVATE ssp_client)

  # Optional: unit tests
  if(SSP_BUILD_TESTS)
    add_executable(test-ssp test_ssp.c)
    target_include_directories(test-ssp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                                ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(test-ssp PRIVATE ssp_client)
  endif()

else()
  # ── Original libssp-based connector ──
  include(FetchContent)
  FetchContent_Declare(
    libssp
    GIT_REPOSITORY "https://github.com/imaginevision/libssp.git"
    GIT_TAG f9882aeebdda781d14262fcc04b9038a567ac985)

  if(NOT libssp_POPULATED)
    FetchContent_Populate(libssp)
    add_library(libssp INTERFACE)
    target_include_directories(libssp INTERFACE ${libssp_SOURCE_DIR}/include)
    if(OS_WINDOWS)
      target_link_libraries(
        libssp INTERFACE "${libssp_SOURCE_DIR}/lib/win_x64_vs2017/libssp$<$<CONFIG:Debug>:d>.lib")
      set(LIBSSP_LIBRARY
          "${libssp_SOURCE_DIR}/lib/win_x64_vs2017/libssp$<$<CONFIG:Debug>:d>.dll"
          PARENT_SCOPE)
    elseif(OS_MACOS)
      set(_COMMAND
          "lipo -create \"${libssp_SOURCE_DIR}/lib/mac_arm64/libssp.dylib\" \
          \"${libssp_SOURCE_DIR}/lib/mac/libssp.dylib\" \
          -output \"${CMAKE_CURRENT_BINARY_DIR}/libssp.dylib\"")
      message(STATUS ${_COMMAND})
      execute_process(COMMAND /bin/sh -c "${_COMMAND}")
      set(LIBSSP_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/libssp.dylib")
      set(LIBSSP_LIBRARY "${LIBSSP_LIBRARY}" PARENT_SCOPE)
      target_link_libraries(libssp INTERFACE "${LIBSSP_LIBRARY}")
      set(_COMMAND
          "/usr/bin/codesign --force \\
          --sign \"${CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY}\" \
          --options runtime \
          --entitlements \"${CMAKE_CURRENT_LIST_DIR}/../cmake/bundle/macOS/entitlements.plist\" \
          \"${LIBSSP_LIBRARY}\"")
      execute_process(COMMAND /bin/sh -c "${_COMMAND}")
    elseif(OS_LINUX)
      target_link_libraries(libssp INTERFACE "${libssp_SOURCE_DIR}/lib/linux_x64/libssp.so")
      set(LIBSSP_LIBRARY "${libssp_SOURCE_DIR}/lib/linux_x64/libssp.so" PARENT_SCOPE)
    endif()
  endif()

  set(SSP_CONNECTOR_SOURCE main.cpp)

  add_executable(ssp-connector ${SSP_CONNECTOR_SOURCE})
  target_link_libraries(ssp-connector PRIVATE libssp)
  target_include_directories(ssp-connector PRIVATE libuv/include)

  if(OS_WINDOWS)
    target_compile_definitions(ssp-connector PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
endif()
