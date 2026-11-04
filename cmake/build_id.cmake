# Run at every build (the quark_build_id target): writes generated/build_id.cpp, the full build name the menus show,
# "quark-<version>-<commit>[-dirty]" (owner, 2026-11-04), as tools/build_windows.ps1 names its folders. Rewritten only
# when it changes, so an unchanged checkout recompiles nothing. No git (a source download): "quark-<version>".
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY ${SRC} OUTPUT_VARIABLE _c OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE _r ERROR_QUIET)
set(_id "quark-${VER}")
if(_r EQUAL 0 AND _c)
  execute_process(COMMAND git status --porcelain WORKING_DIRECTORY ${SRC} OUTPUT_VARIABLE _d ERROR_QUIET)
  set(_id "${_id}-${_c}")
  if(_d)
    set(_id "${_id}-dirty")
  endif()
endif()
set(_text "// generated at build time by cmake/build_id.cmake\n#include \"common/version.h\"\nconst char kQuarkBuildId[] = \"${_id}\";\n")
set(_old "")
if(EXISTS ${OUT})
  file(READ ${OUT} _old)
endif()
if(NOT _old STREQUAL _text)
  file(WRITE ${OUT} "${_text}")
endif()
