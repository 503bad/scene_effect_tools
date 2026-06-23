cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version} LANGUAGES C)

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

# defaults.cmake downloaded OBS sources to ${CMAKE_CURRENT_SOURCE_DIR}/.deps
# and installed libobsConfig.cmake under .deps/cmake/. When the vcpkg
# toolchain is in use it can hide that prefix from find_package, so point
# directly at the config dir as a fallback.
if(NOT libobs_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/cmake/libobsConfig.cmake")
  set(libobs_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps/cmake" CACHE PATH "Path to libobsConfig.cmake" FORCE)
endif()
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/.deps")

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE src)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.c
    src/sfx-common.c
    src/sfx-render.c
    src/filters/sepia.c
    src/filters/cartoon.c
    src/filters/pixel-art.c
    src/filters/insta-grade.c
    src/filters/old-film.c
    src/filters/vhs.c
    src/filters/bloom.c
    src/filters/focus.c
    src/filters/crunch.c
    src/filters/glitch.c
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# --- Distribution layout (package/) ----------------------------------------
# Emit exactly the tree we ship, with the "dist" install component:
#   cmake --install <build> --config RelWithDebInfo --component dist --prefix package
#
#   package/
#   ├─ bin/
#   │   └─ screen-effect-tools.dll
#   ├─ screen-effect-tools/
#   │   ├─ effects/*.effect
#   │   └─ locale/*.ini
#   ├─ README.md
#   ├─ LICENSE
#   └─ SOURCE-NOTICE.txt
install(
  TARGETS ${CMAKE_PROJECT_NAME}
  LIBRARY DESTINATION bin COMPONENT dist
  RUNTIME DESTINATION bin COMPONENT dist
)
if(MSVC)
  install(
    FILES "$<TARGET_PDB_FILE:${CMAKE_PROJECT_NAME}>"
    DESTINATION bin
    COMPONENT dist
    OPTIONAL
  )
endif()
install(
  DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/effects"
  DESTINATION ${CMAKE_PROJECT_NAME}
  COMPONENT dist
)
install(
  DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/locale"
  DESTINATION ${CMAKE_PROJECT_NAME}
  COMPONENT dist
)
install(
  FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/dist/SOURCE-NOTICE.txt"
  DESTINATION .
  COMPONENT dist
)

# --- macOS distribution layout (package_mac/) ------------------------------
# On macOS the plugin ships as a self-contained .plugin bundle (binary +
# effects + locale). Stage everything needed for distribution under
# package_mac/ automatically at the end of every build.
if(APPLE)
  set(_pkg_mac_dir "${CMAKE_CURRENT_SOURCE_DIR}/package_mac")
  add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
      "${_pkg_mac_dir}/$<TARGET_BUNDLE_DIR_NAME:${CMAKE_PROJECT_NAME}>"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_pkg_mac_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
      "$<TARGET_BUNDLE_DIR:${CMAKE_PROJECT_NAME}>"
      "${_pkg_mac_dir}/$<TARGET_BUNDLE_DIR_NAME:${CMAKE_PROJECT_NAME}>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
      "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
      "${CMAKE_CURRENT_SOURCE_DIR}/dist/SOURCE-NOTICE.txt"
      "${_pkg_mac_dir}"
    COMMENT "Stage macOS distribution into package_mac/"
    VERBATIM
  )
endif()
