# Build step (CMake script mode): puts the shipped maps next to the executables.
#   cmake -DMAPS=<a.qmap|b.qmap|...> -DDEST=<build>/maps -P tools/copy_maps.cmake
# Copies each of MAPS ('|'-separated: the generated maps and the hand-made ones .gitignore un-ignores) into DEST when
# it differs, and removes every other *.qmap in DEST, so a map the generator no longer makes (or a stray file left in
# maps/) doesn't linger in the build and get listed, played or tested.
cmake_minimum_required(VERSION 3.16)   # (script mode: IN_LIST and the rest need the policies set)
if(NOT DEFINED MAPS OR NOT DEFINED DEST)
  message(FATAL_ERROR "copy_maps.cmake: MAPS and DEST are required")
endif()
string(REPLACE "|" ";" MAPS "${MAPS}")
file(MAKE_DIRECTORY "${DEST}")
set(keep "")
foreach(f IN LISTS MAPS)
  if(f STREQUAL "")
    continue()
  endif()
  get_filename_component(n "${f}" NAME)
  list(APPEND keep "${n}")
  configure_file("${f}" "${DEST}/${n}" COPYONLY)   # (only rewritten when it changed)
endforeach()
file(GLOB present "${DEST}/*.qmap")
foreach(f IN LISTS present)
  get_filename_component(n "${f}" NAME)
  if(NOT n IN_LIST keep)
    get_filename_component(stem "${f}" NAME_WE)
    message(STATUS "copy_maps: removing ${n} (not a shipped map)")
    file(REMOVE "${f}" "${DEST}/${stem}.qlm")   # (and an old light cache made next to it)
  endif()
endforeach()
