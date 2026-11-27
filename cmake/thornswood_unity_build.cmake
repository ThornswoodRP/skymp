# THORNSWOOD (Thornswood #7247; Patrick, 27 Nov 2026: a 20
# to 45 minute engine build is too long).
#
# WHY. Measured on the pull request build of skymp #220 (run 45947372063, 27
# Nov 2026): 26 minutes, of which `cmake --build` was 21. ccache answered 390
# of 619 compiles and compiled 229, and those 229 were the whole wait:
# server_guest_lib 7.5 minutes, the addon (skymp5-server) 3, unit 8.6, one
# after the other, with every one of the runner's four processors busy. The
# same 220 to 240 compiles missed on the push builds of #208 and #210 and the
# pull request of the killmove change: nearly every server file reaches the
# same headers (MpActor.h, MpChangeForms.h, Networking.h, the libespm
# records, 198 to 227 files each), so a change to any of them compiles about
# 220 files again. Each of those files is about 15 seconds of one processor,
# and most of that is the same headers read again: nlohmann/json.hpp,
# simdjson.h, spdlog, fmt, SLikeNet, Catch2 and the server's own headers,
# which 210 to 230 of the 267 files of these three targets read in full
# (g++ alone takes about 5 seconds a file for the libraries).
#
# WHAT. These three targets compile as a unity build: CMake writes one file
# for every eight sources, which includes them one after the other, so the
# headers are read once for the eight instead of eight times. ccache keeps
# each of those files' objects as it kept each source's, so a change still
# compiles only the batches that reach what it changed.
#
# What it asks of the code: the sources of a batch are one file to the
# compiler, so two sources of one target cannot each keep their own copy of a
# name at file scope or in an anonymous namespace. Shared ones are said once
# (unit/TestUtils.hpp, unit/TestFormIds.h, unit/platform_lib_tests/TestIds.h,
# LeveledListUtils::kPlayerCharacterLevel), and a name that means different
# things in two files is called what it is in each. A clash is a compile
# error naming both, on every build, never a silent change.
#
# /bigobj: a batch's object holds more sections than MSVC's default allows.

function(thornswood_unity_build)
  cmake_parse_arguments(A "" "" "TARGETS" ${ARGN})
  if("${A_TARGETS}" STREQUAL "")
    message(FATAL_ERROR "thornswood_unity_build: missing TARGETS")
  endif()
  foreach(target ${A_TARGETS})
    set_target_properties(${target} PROPERTIES
      UNITY_BUILD ON
      UNITY_BUILD_BATCH_SIZE 8
    )
    if(MSVC)
      target_compile_options(${target} PRIVATE /bigobj)
    endif()
  endforeach()
endfunction()
