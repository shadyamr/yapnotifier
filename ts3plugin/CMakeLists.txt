# The TeamSpeak 3 client is 64-bit, the overlay is 32-bit (gta_sa.exe), and one CMake tree can only
# target one architecture: so this is its own project, normally built by the top level as an x64
# ExternalProject. Standalone: cmake -S ts3plugin -B build-ts3 -A x64
cmake_minimum_required(VERSION 3.24)
project(YapNotifierTS3 LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "The TS3 plugin only builds as x64 Windows (the TeamSpeak 3 client is 64-bit)")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(YAP_PACKAGE_DIR "${CMAKE_BINARY_DIR}" CACHE PATH "Where YapNotifier.ts3_plugin is written")

include(FetchContent)
FetchContent_Declare(ts3sdk
  GIT_REPOSITORY https://github.com/TeamSpeak-Systems/ts3client-pluginsdk.git
  GIT_TAG        4aa90a53aa150cbf81e13bc97e68c0431b26499f)  # plugin API 26, no release tags upstream
FetchContent_MakeAvailable(ts3sdk)

add_library(yapnotifier_ts3 SHARED plugin.cpp sqlite_read.cpp ts_contacts.cpp)
target_include_directories(yapnotifier_ts3 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../shared ${CMAKE_CURRENT_SOURCE_DIR} ${ts3sdk_SOURCE_DIR}/include)
target_link_libraries(yapnotifier_ts3 PRIVATE ws2_32)
target_compile_definitions(yapnotifier_ts3 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(yapnotifier_ts3 PRIVATE /W4 /permissive- /utf-8)
set_target_properties(yapnotifier_ts3 PROPERTIES PREFIX "" OUTPUT_NAME "yapnotifier_ts3_win64")

# A .ts3_plugin is a zip of package.ini + plugins/<dll>; TeamSpeak installs it on double-click.
set(TS3_PKG_DIR ${CMAKE_BINARY_DIR}/ts3_package)
file(MAKE_DIRECTORY ${TS3_PKG_DIR}/plugins)  # WORKING_DIRECTORY must exist before the first command runs
add_custom_target(ts3_package ALL
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/package.ini ${TS3_PKG_DIR}/package.ini
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:yapnotifier_ts3> ${TS3_PKG_DIR}/plugins/
  COMMAND ${CMAKE_COMMAND} -E tar cf ${YAP_PACKAGE_DIR}/YapNotifier.ts3_plugin --format=zip -- package.ini plugins
  WORKING_DIRECTORY ${TS3_PKG_DIR}
  DEPENDS yapnotifier_ts3
  COMMENT "Packaging YapNotifier.ts3_plugin")
