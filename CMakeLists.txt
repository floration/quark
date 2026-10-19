cmake_minimum_required(VERSION 3.16)
project(quark CXX C)
if(WIN32)
  enable_language(RC)
  file(WRITE ${CMAKE_BINARY_DIR}/quark_icon.rc "IDI_ICON1 ICON \"${CMAKE_SOURCE_DIR}/assets/quark.ico\"\n")
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
  add_compile_options(/W3 /O2 /fp:fast)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_WIN32_WINNT=0x0601) # Windows 7
else()
  add_compile_options(-Wall -Wno-unused-parameter)
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2)
  endif()
  if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0601)
  endif()
endif()

find_package(Threads REQUIRED)

# Shared engine/game code (no graphics)
add_library(quark_core STATIC
  src/common/net.cpp
  src/common/upnp.cpp
  src/game/map.cpp
  src/game/pmove.cpp
  src/game/protocol.cpp
  src/game/server.cpp
  src/game/gamemodes.cpp
  src/game/netclient.cpp
  src/game/bots.cpp
)
target_include_directories(quark_core PUBLIC src)
target_link_libraries(quark_core PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(quark_core PUBLIC ws2_32 winmm)
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

  add_executable(quark WIN32 MACOSX_BUNDLE
    src/client/main.cpp
    src/client/game.cpp
    src/client/gl.cpp
    src/client/renderer.cpp
    src/client/lightmap.cpp
    src/client/ui.cpp
    src/client/config.cpp
    ${QUARK_ICON_RC}
  )
  target_link_libraries(quark quark_core glfw)
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
      target_link_libraries(${t} "-framework OpenGL")
    elseif(WIN32)
      target_link_libraries(${t} opengl32)
    endif()
  endforeach()
  if(WIN32)
    target_link_libraries(quark_launcher winhttp ole32 uuid shell32 advapi32)
  endif()
endif()

if(QUARK_BUILD_TESTS)
  add_executable(quark_tests tests/tests.cpp)
  target_link_libraries(quark_tests quark_core)
endif()

# Copy maps next to the executables so builds run in place.
add_custom_target(copy_maps ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/maps ${CMAKE_BINARY_DIR}/maps)
