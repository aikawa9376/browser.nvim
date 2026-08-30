if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.21)
  project(browserd LANGUAGES C CXX)
  include(CTest)
endif()

find_package(Threads REQUIRED)

add_library(browserd_common
  src/framebuffer.cc
  src/ipc.cc
  src/json.cc
  src/kitty_renderer.cc
  src/shm.cc
)
target_compile_features(browserd_common PUBLIC cxx_std_20)
target_include_directories(browserd_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(browserd_common PUBLIC Threads::Threads)
target_compile_options(browserd_common PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>
)

add_library(browserd_core
  src/app.cc
  src/browser_manager.cc
)
target_compile_features(browserd_core PUBLIC cxx_std_20)
target_include_directories(browserd_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(browserd_core PUBLIC browserd_common)
target_compile_options(browserd_core PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>
)

set(_browser_default_cef_root
  "${CMAKE_SOURCE_DIR}/deps/cef_binary_151.3.11+gd08600e+chromium-151.0.7922.47_linux64_minimal")
if(NOT CEF_ROOT AND IS_DIRECTORY "${_browser_default_cef_root}")
  set(CEF_ROOT "${_browser_default_cef_root}" CACHE PATH
    "Path to the pinned CEF binary distribution")
endif()
option(BROWSER_ENABLE_CEF "Build browserd with CEF off-screen rendering" ON)

if(BROWSER_ENABLE_CEF AND NOT CEF_ROOT)
  message(FATAL_ERROR
    "BROWSER_ENABLE_CEF=ON but CEF_ROOT is unavailable. "
    "Run scripts/fetch-cef.sh or pass -DCEF_ROOT=/path/to/cef. "
    "Use -DBROWSER_ENABLE_CEF=OFF only for the Phase 1 gradient daemon.")
endif()

if(BROWSER_ENABLE_CEF)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/web/bridge.js"
    "${CMAKE_SOURCE_DIR}/web/hints.js"
    "${CMAKE_SOURCE_DIR}/web/visual.js")
  file(READ "${CMAKE_SOURCE_DIR}/web/bridge.js" BROWSER_BRIDGE_JS)
  file(READ "${CMAKE_SOURCE_DIR}/web/hints.js" BROWSER_HINTS_JS)
  file(READ "${CMAKE_SOURCE_DIR}/web/visual.js" BROWSER_VISUAL_JS)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
  configure_file(src/embedded_scripts.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_scripts.h" @ONLY)

  list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
  find_package(CEF REQUIRED)
  add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}"
    "${CMAKE_BINARY_DIR}/libcef_dll_wrapper")

  add_library(browserd_cef_core
    src/cef_app.cc
    src/cef_browser_client.cc
    src/cef_browser_manager.cc
    src/cef_daemon.cc
  )
  target_compile_features(browserd_cef_core PUBLIC cxx_std_20)
  target_include_directories(browserd_cef_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CEF_ROOT}
  )
  target_compile_definitions(browserd_cef_core PUBLIC
    BROWSER_WITH_CEF=1
    BROWSER_CEF_ROOT="${CEF_ROOT}"
  )
  target_link_libraries(browserd_cef_core PUBLIC
    browserd_common
    libcef_dll_wrapper
    ${CEF_LIB_RELEASE}
    ${CEF_STANDARD_LIBS}
  )
  SET_LIBRARY_TARGET_PROPERTIES(browserd_cef_core)

  add_executable(browserd src/main.cc)
  target_link_libraries(browserd PRIVATE browserd_cef_core browserd_core)
  set_target_properties(browserd PROPERTIES
    INSTALL_RPATH "$ORIGIN"
    BUILD_WITH_INSTALL_RPATH TRUE
  )
  SET_EXECUTABLE_TARGET_PROPERTIES(browserd)
  COPY_FILES("browserd" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}")
  COPY_FILES("browserd" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}")
  foreach(_browser_cef_binary IN LISTS CEF_BINARY_FILES)
    install(FILES "${CEF_BINARY_DIR}/${_browser_cef_binary}"
      DESTINATION bin)
  endforeach()
  foreach(_browser_cef_resource IN LISTS CEF_RESOURCE_FILES)
    if(IS_DIRECTORY "${CEF_RESOURCE_DIR}/${_browser_cef_resource}")
      install(DIRECTORY "${CEF_RESOURCE_DIR}/${_browser_cef_resource}"
        DESTINATION bin)
    else()
      install(FILES "${CEF_RESOURCE_DIR}/${_browser_cef_resource}"
        DESTINATION bin)
    endif()
  endforeach()
  message(STATUS "browserd: CEF enabled (${CEF_ROOT})")
else()
  add_executable(browserd src/main.cc)
  target_link_libraries(browserd PRIVATE browserd_core)
  message(WARNING
    "browserd: CEF disabled; building the Phase 1 gradient daemon. "
    "Run scripts/fetch-cef.sh or pass -DCEF_ROOT=/path/to/cef.")
endif()

install(TARGETS browserd RUNTIME DESTINATION bin)

if(BUILD_TESTING)
  add_subdirectory(tests)
  if(BROWSER_ENABLE_CEF AND DEFINED ENV{DISPLAY} AND NOT "$ENV{DISPLAY}" STREQUAL "")
    add_test(
      NAME browserd.cef_smoke
      COMMAND bash
        ${CMAKE_SOURCE_DIR}/tests/integration/cef-smoke.sh
        $<TARGET_FILE:browserd>
        ${CMAKE_SOURCE_DIR}/tests/pages/basic.html
        ${CMAKE_SOURCE_DIR}/tests/pages/scroll.html
        ${CMAKE_SOURCE_DIR}/tests/pages/links.html
        ${CMAKE_SOURCE_DIR}/tests/pages/forms.html
        ${CMAKE_SOURCE_DIR}/tests/pages/selection.html
        ${CMAKE_SOURCE_DIR}/tests/pages/graphemes.html
    )
    set_tests_properties(browserd.cef_smoke PROPERTIES TIMEOUT 75)
  endif()
endif()
