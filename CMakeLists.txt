cmake_minimum_required(VERSION 3.16)
project(quark CXX C)
if(WIN32)
  enable_language(RC)
  file(WRITE ${CMAKE_BINARY_DIR}/quark_icon.rc "GLFW_ICON ICON \"${CMAKE_SOURCE_DIR}/assets/quark.ico\"\n")
  set(QUARK_ICON_RC ${CMAKE_BINARY_DIR}/quark_icon.rc)
endif()

file(STRINGS ${CMAKE_SOURCE_DIR}/VERSION QUARK_VERSION LIMIT_COUNT 1)
add_compile_definitions(QUARK_VERSION="${QUARK_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(QUARK_BUILD_CLIENT "Build the game client (needs OpenGL)" ON)
option(QUARK_BUILD_TESTS "Build headless tests" ON)

if(MSVC)
  # precise FP everywhere: client prediction (pmove / map / collision) must match a server built by another
  # toolchain bit for bit, so no fast-math and no FMA contraction on any compiler
  add_compile_options(/W3 /O2 /fp:precise)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_WIN32_WINNT=0x0601) # Windows 7
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter -fno-fast-math -ffp-contract=off)
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2)
  endif()
  if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0601)
  endif()
endif()

find_package(Threads REQUIRED)

# Update-package signatures (vendored Monocypher, docs/SIGNING.md): launcher, tests and the quark_sign tool.
add_library(quark_crypto STATIC
  src/common/sign.cpp
  third_party/monocypher/monocypher.c
  third_party/monocypher/monocypher-ed25519.c
)
target_include_directories(quark_crypto PUBLIC src)
if(WIN32)
  target_link_libraries(quark_crypto PUBLIC bcrypt)
endif()
add_executable(quark_sign tools/sign/main.cpp)
target_link_libraries(quark_sign quark_crypto)
if(MINGW)
  target_link_options(quark_sign PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Shared engine/game code (no graphics)
add_library(quark_core STATIC
  src/common/net.cpp
  src/common/upnp.cpp
  src/common/http.cpp
  src/common/proc.cpp
  src/game/map.cpp
  src/game/pmove.cpp
  src/game/protocol.cpp
  src/game/server.cpp
  src/game/gamemodes.cpp
  src/game/netclient.cpp
  src/game/bots.cpp
  src/game/master.cpp
  src/game/ranked.cpp
  src/game/customrules.cpp
)
target_include_directories(quark_core PUBLIC src ${CMAKE_BINARY_DIR}/generated)
# ---- maps (a build artifact) ---------------------------------------------------------------------------
# tools/make_maps.py generates the shipped maps at build time (python3, nothing else) into maps/, which .gitignore
# leaves out: the generator is the only source. maps/ is where the game looks (next to the source tree when run from
# it, copied next to the executables below) and what release packaging copies. Hand-made maps (a maps/*.qmap the
# generator doesn't write) are shipped and embedded too.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(QUARK_MAPS_DIR ${CMAKE_SOURCE_DIR}/maps)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tools/make_maps.py)
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/make_maps.py --list
  OUTPUT_VARIABLE QUARK_GEN_MAP_NAMES RESULT_VARIABLE rc OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT rc EQUAL 0 OR QUARK_GEN_MAP_NAMES STREQUAL "")
  message(FATAL_ERROR "tools/make_maps.py --list failed (${rc})")
endif()
string(REPLACE "\r" "" QUARK_GEN_MAP_NAMES "${QUARK_GEN_MAP_NAMES}")
string(REPLACE "\n" ";" QUARK_GEN_MAP_NAMES "${QUARK_GEN_MAP_NAMES}")
set(QUARK_GEN_MAPS "")
foreach(n ${QUARK_GEN_MAP_NAMES})
  list(APPEND QUARK_GEN_MAPS ${QUARK_MAPS_DIR}/${n}.qmap)
endforeach()
add_custom_command(OUTPUT ${QUARK_GEN_MAPS}
  COMMAND ${CMAKE_COMMAND} -E env PYTHONHASHSEED=0 ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/make_maps.py --out ${QUARK_MAPS_DIR}
  DEPENDS ${CMAKE_SOURCE_DIR}/tools/make_maps.py
  COMMENT "Generating maps (tools/make_maps.py)" VERBATIM)
file(GLOB QUARK_MAP_FILES CONFIGURE_DEPENDS ${QUARK_MAPS_DIR}/*.qmap)
set(QUARK_HAND_MAPS "")
foreach(f ${QUARK_MAP_FILES})
  get_filename_component(n ${f} NAME_WE)
  if(NOT n IN_LIST QUARK_GEN_MAP_NAMES)
    list(APPEND QUARK_HAND_MAPS ${f})
  endif()
endforeach()
# Official maps (ranked only runs on these): name + sha256 of each shipped map, and its text, embedded in the game.
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/generated/official_maps.stamp
  BYPRODUCTS ${CMAKE_BINARY_DIR}/generated/official_maps.h ${CMAKE_BINARY_DIR}/generated/official_map_data.h
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/embed_maps.py ${CMAKE_BINARY_DIR}/generated ${QUARK_GEN_MAPS} ${QUARK_HAND_MAPS}
  DEPENDS ${CMAKE_SOURCE_DIR}/tools/embed_maps.py ${QUARK_GEN_MAPS} ${QUARK_HAND_MAPS}
  COMMENT "Embedding the shipped maps" VERBATIM)
add_custom_target(quark_maps DEPENDS ${CMAKE_BINARY_DIR}/generated/official_maps.stamp)
add_dependencies(quark_core quark_maps)
# Profiling: -DQUARK_TRACY=ON adds Tracy zones (src/common/profile.h). QUARK_TRACY_DIR can point at a
# local Tracy checkout; otherwise v0.11.1 is fetched. Off by default (release builds never include it).
option(QUARK_TRACY "Build with the Tracy profiler client" OFF)
option(QUARK_ZONESTATS "Per-zone CPU totals printed by quark --bench (profiling builds only)" OFF)
if(QUARK_ZONESTATS)
  add_compile_definitions(QUARK_ZONESTATS)
endif()
if(QUARK_TRACY)
  if(QUARK_TRACY_DIR)
    set(tracy_SOURCE_DIR ${QUARK_TRACY_DIR})
  else()
    include(FetchContent)
    FetchContent_Declare(tracy GIT_REPOSITORY https://github.com/wolfpld/tracy.git GIT_TAG v0.11.1 GIT_SHALLOW TRUE)
    FetchContent_Populate(tracy)
  endif()
  target_sources(quark_core PRIVATE ${tracy_SOURCE_DIR}/public/TracyClient.cpp)
  target_include_directories(quark_core PUBLIC ${tracy_SOURCE_DIR}/public)
  target_compile_definitions(quark_core PUBLIC QUARK_TRACY TRACY_ENABLE)
  target_link_libraries(quark_core PUBLIC ${CMAKE_DL_LIBS})
endif()
target_link_libraries(quark_core PUBLIC Threads::Threads quark_crypto)
if(WIN32)
  target_link_libraries(quark_core PUBLIC ws2_32 winmm winhttp)
endif()
if(MINGW)
  # Self-contained exes: no libstdc++/libgcc/winpthread DLLs to ship.
  target_link_options(quark_core PUBLIC -static -static-libgcc -static-libstdc++)
endif()

add_executable(quark_server src/server/main.cpp)
target_link_libraries(quark_server quark_core)

if(QUARK_BUILD_CLIENT)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/glfw EXCLUDE_FROM_ALL)
  if(NOT MSVC)
    target_compile_options(glfw PRIVATE -Wno-sign-compare -Wno-missing-field-initializers)   # third party: our -Wextra is for our code
  endif()

  add_executable(quark WIN32 MACOSX_BUNDLE
    src/client/main.cpp
    src/client/game.cpp
    src/client/community.cpp
    src/client/editor.cpp
    src/client/openurl.cpp
    src/client/gl.cpp
    src/client/renderer.cpp
    src/client/lightmap.cpp
    src/client/ui.cpp
    src/client/config.cpp
    src/client/hudwidgets.cpp
    src/client/profiles.cpp
    src/client/binds.cpp
    src/client/audio.cpp
    ${QUARK_ICON_RC}
  )
  target_link_libraries(quark quark_core glfw ${CMAKE_DL_LIBS})
  if(WIN32)
    target_link_libraries(quark shell32)
  endif()
  add_executable(quark_launcher WIN32
    src/launcher/main.cpp
    src/launcher/install.cpp
    ${QUARK_ICON_RC}
    src/client/gl.cpp
    src/client/renderer.cpp
    src/client/lightmap.cpp
    src/client/ui.cpp
  )
  target_link_libraries(quark_launcher quark_core glfw)
  foreach(t quark quark_launcher)
    if(APPLE)
      target_link_libraries(${t} "-framework OpenGL" "-framework AudioToolbox" "-framework CoreFoundation")
    elseif(WIN32)
      target_link_libraries(${t} opengl32)
    endif()
  endforeach()
  if(WIN32)
    target_link_libraries(quark_launcher winhttp ole32 uuid shell32 advapi32)
  endif()
endif()

if(QUARK_BUILD_TESTS)
  add_executable(quark_tests tests/tests.cpp tests/mapreport.cpp)
  target_link_libraries(quark_tests quark_core)
  target_compile_definitions(quark_tests PRIVATE QUARK_SOURCE_DIR="${CMAKE_SOURCE_DIR}")   # cross-language constant checks read tools/
  target_compile_definitions(quark_tests PRIVATE QUARK_TEST_DATA="${CMAKE_SOURCE_DIR}/tests/data")   # regression inputs
endif()

# Copy maps (generated + hand-made) next to the executables so builds run in place.
add_custom_target(copy_maps ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${QUARK_MAPS_DIR} ${CMAKE_BINARY_DIR}/maps)
add_dependencies(copy_maps quark_maps)
