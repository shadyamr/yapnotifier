cmake_minimum_required(VERSION 3.24)
project(YapNotifier LANGUAGES C CXX RC)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "YapNotifier builds as 32-bit Windows like gta_sa.exe: configure with -A Win32 "
                      "(the x64 TS3 plugin is built from here as a sub-build)")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: neither binary may depend on a vcredist the player may not have.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(YAP_COMMON_DEFS WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
set(YAP_COMMON_OPTS /W4 /permissive- /utf-8)

include(FetchContent)
# UI stack: RmlUi (HTML/CSS-like documents) on FreeType, with its SVG plugin on LunaSVG for the
# vector icons. All static; OVERRIDE_FIND_PACKAGE routes RmlUi's find_package() to these fetches.
set(BUILD_SHARED_LIBS OFF)
set(FT_DISABLE_ZLIB ON)
set(FT_DISABLE_BZIP2 ON)
set(FT_DISABLE_PNG ON)
set(FT_DISABLE_HARFBUZZ ON)
set(FT_DISABLE_BROTLI ON)
set(SKIP_INSTALL_ALL ON)
set(LUNASVG_BUILD_EXAMPLES OFF)
set(LUNASVG_DISABLE_LOAD_SYSTEM_FONTS ON)  # never scan the fonts folder from inside the game
set(RMLUI_SAMPLES OFF)
set(RMLUI_SVG_PLUGIN ON)
set(RMLUI_FONT_ENGINE "freetype")
FetchContent_Declare(Freetype
  GIT_REPOSITORY https://github.com/freetype/freetype.git
  GIT_TAG        VER-2-14-3
  GIT_SHALLOW    TRUE
  OVERRIDE_FIND_PACKAGE)
FetchContent_Declare(lunasvg
  GIT_REPOSITORY https://github.com/sammycage/lunasvg.git
  GIT_TAG        v3.5.0
  GIT_SHALLOW    TRUE
  OVERRIDE_FIND_PACKAGE)
FetchContent_Declare(RmlUi
  GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
  GIT_TAG        6.3
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(Freetype lunasvg)
add_library(Freetype::Freetype ALIAS freetype)  # the name RmlUi links against; freetype's tree only exports it on install
FetchContent_MakeAvailable(RmlUi)

# RmlUi's Win32 platform backend (system interface, WindowProcedure, IME) ships as a loose source;
# build it as our own lib (third-party code, not held to /W4). We render with our own D3D9 renderer.
add_library(rmlui_backend STATIC ${rmlui_SOURCE_DIR}/Backends/RmlUi_Platform_Win32.cpp)
target_include_directories(rmlui_backend SYSTEM PUBLIC ${rmlui_SOURCE_DIR}/Backends)
target_link_libraries(rmlui_backend PUBLIC RmlUi::RmlUi)
target_compile_definitions(rmlui_backend PUBLIC ${YAP_COMMON_DEFS})
set_target_properties(rmlui_core lunasvg plutovg freetype PROPERTIES SYSTEM ON)  # keep /W4 clean on our TUs

# --- GTA SA side: YapNotifierSA.asi -------------------------------------------
set(YAP_ASI_SOURCES
  src/main.cpp
  src/log.cpp
  src/colorpicker.cpp
  src/config.cpp
  src/overlay.cpp
  src/hud.cpp
  src/icons.cpp
  src/menu.cpp
  src/notify.cpp
  src/roster.cpp
  src/teamspeak.cpp
  src/ui_files.cpp
  src/update.cpp)
add_library(YapNotifier SHARED ${YAP_ASI_SOURCES} YapNotifier.rc)
target_include_directories(YapNotifier PRIVATE shared)
target_link_libraries(YapNotifier PRIVATE rmlui_backend ws2_32 winhttp bcrypt)
target_compile_definitions(YapNotifier PRIVATE ${YAP_COMMON_DEFS})
target_compile_options(YapNotifier PRIVATE ${YAP_COMMON_OPTS})
set_target_properties(YapNotifier PROPERTIES PREFIX "" OUTPUT_NAME "YapNotifierSA" SUFFIX ".asi")
# Always emit a PDB so crash offsets (YapNotifierSA.asi+0x...) resolve to source lines.
target_compile_options(YapNotifier PRIVATE /Zi)
target_link_options(YapNotifier PRIVATE /DEBUG /OPT:REF /OPT:ICF)

set(YAPNOTIFIER_DEPLOY_DIR "" CACHE PATH "GTA San Andreas folder (or its scripts folder); when set, the .asi is copied there after every build")
if(YAPNOTIFIER_DEPLOY_DIR)
  add_custom_command(TARGET YapNotifier POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:YapNotifier> "${YAPNOTIFIER_DEPLOY_DIR}/"
    COMMENT "Deploying YapNotifierSA.asi to ${YAPNOTIFIER_DEPLOY_DIR}")
endif()

# --- TeamSpeak side: x64 sub-build -> build/YapNotifier.ts3_plugin ------------
include(ExternalProject)
ExternalProject_Add(ts3plugin_x64
  SOURCE_DIR ${CMAKE_SOURCE_DIR}/ts3plugin
  BINARY_DIR ${CMAKE_BINARY_DIR}/ts3plugin-x64
  CMAKE_GENERATOR ${CMAKE_GENERATOR}
  CMAKE_GENERATOR_PLATFORM x64
  CMAKE_ARGS -DYAP_PACKAGE_DIR=${CMAKE_BINARY_DIR}
  BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG>
  INSTALL_COMMAND ""
  BUILD_ALWAYS TRUE)

# --- Tests --------------------------------------------------------------------
enable_testing()
add_executable(test_parser tests/test_parser.cpp src/teamspeak.cpp src/update.cpp src/log.cpp src/config.cpp src/notify.cpp src/roster.cpp)
target_include_directories(test_parser PRIVATE src shared)
target_link_libraries(test_parser PRIVATE ws2_32 winhttp bcrypt)
target_compile_definitions(test_parser PRIVATE ${YAP_COMMON_DEFS})
target_compile_options(test_parser PRIVATE ${YAP_COMMON_OPTS})
add_test(NAME parser COMMAND test_parser)

add_executable(test_ui tests/test_ui.cpp src/hud.cpp src/menu.cpp src/colorpicker.cpp src/icons.cpp src/ui_files.cpp src/log.cpp src/config.cpp src/notify.cpp src/roster.cpp src/teamspeak.cpp src/update.cpp)
target_include_directories(test_ui PRIVATE src shared)
target_link_libraries(test_ui PRIVATE RmlUi::RmlUi ws2_32 winhttp bcrypt)
target_compile_definitions(test_ui PRIVATE ${YAP_COMMON_DEFS})
target_compile_options(test_ui PRIVATE ${YAP_COMMON_OPTS})
add_test(NAME ui COMMAND test_ui WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/assets/ui)
