cmake_minimum_required(VERSION 3.16)
project(dk94 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Two ways to build:
#  * developer build (default): the ROM is recompiled to C at build time and
#    linked in.  The executable contains game code - never distribute it.
#  * player build (-DDK94_PLAYER=ON): contains no game code at all.  It
#    recompiles the user's ROM at startup and compiles it in memory with
#    TinyCC.  This is the build that can be distributed.
option(DK94_PLAYER "Build the distributable player (recompiles the ROM at runtime)" OFF)
set(DK94_ROM "${CMAKE_SOURCE_DIR}/Donkey Kong (World) (Rev 1) (SGB Enhanced).gb"
    CACHE FILEPATH "ROM to recompile (developer build only)")
set(DK94_BANKS 32 CACHE STRING "Number of 16 KiB ROM banks")
set(TINYCC_DIR "" CACHE PATH "TinyCC 0.9.27 source tree (player build; downloaded if empty)")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(SDL2 REQUIRED CONFIG)

set(RECOMP_DIR ${CMAKE_SOURCE_DIR}/tools/recomp)
set(GEN_DIR ${CMAKE_BINARY_DIR}/gen)
set(COVERAGE ${RECOMP_DIR}/coverage.txt)

# Generic tables (nothing from the ROM): opcode table, interpreter, game
# notes, TinyCC prelude.
set(TABLES ${GEN_DIR}/interp_ops.inc ${GEN_DIR}/sm83_table.c ${GEN_DIR}/game_data.c ${GEN_DIR}/tcc_prelude.c)
add_custom_command(
  OUTPUT ${TABLES}
  COMMAND Python3::Interpreter ${RECOMP_DIR}/gen_tables.py
          --out ${GEN_DIR} --runtime ${CMAKE_SOURCE_DIR}/runtime
          --config ${RECOMP_DIR}/overrides.toml --coverage ${COVERAGE}
  DEPENDS ${RECOMP_DIR}/gen_tables.py ${RECOMP_DIR}/sm83.py ${RECOMP_DIR}/overrides.toml
          ${COVERAGE} runtime/gb.h runtime/cpu_ops.h
  COMMENT "Generating opcode tables"
  VERBATIM)

set(RECOMP_SOURCES runtime/recomp/recomp.c ${GEN_DIR}/sm83_table.c ${GEN_DIR}/game_data.c)

add_executable(dk94
  runtime/main.c runtime/cpu.c runtime/mem.c runtime/ppu.c runtime/apu.c
  runtime/pad.c runtime/files.c runtime/launcher.c runtime/sgb.c runtime/sgb_sound.c
  runtime/spc.c runtime/dsp.c runtime/platform_sdl.c
  ${RECOMP_SOURCES} ${GEN_DIR}/interp_ops.inc)
target_include_directories(dk94 PRIVATE runtime runtime/recomp ${GEN_DIR})

if(DK94_PLAYER)
  # ---- TinyCC as a shared library (LGPL-2.1: kept dynamically linked) ----
  if(NOT TINYCC_DIR)
    include(FetchContent)
    FetchContent_Declare(tinycc
      URL https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27.tar.bz2
      URL_HASH SHA256=de23af78fca90ce32dff2dd45b3432b2334740bb9bb7b05bf60fdbfc396ceb9c)
    FetchContent_Populate(tinycc)
    set(TINYCC_DIR ${tinycc_SOURCE_DIR})
  endif()
  file(WRITE ${CMAKE_BINARY_DIR}/tcc-config/config.h
       "#define TCC_VERSION \"0.9.27\"\n#define CONFIG_TCCDIR \".\"\n")
  add_library(tcc SHARED ${TINYCC_DIR}/libtcc.c)
  target_compile_definitions(tcc PRIVATE ONE_SOURCE=1 TCC_TARGET_X86_64 $<$<BOOL:${WIN32}>:TCC_TARGET_PE>)
  target_include_directories(tcc PRIVATE ${CMAKE_BINARY_DIR}/tcc-config ${TINYCC_DIR})
  target_compile_options(tcc PRIVATE -w)
  set_target_properties(tcc PROPERTIES PREFIX "lib" C_VISIBILITY_PRESET default)
  if(NOT WIN32)
    target_link_libraries(tcc PRIVATE m dl)
  endif()

  target_sources(dk94 PRIVATE runtime/native.c ${GEN_DIR}/tcc_prelude.c)
  target_include_directories(dk94 PRIVATE ${TINYCC_DIR})
  target_compile_definitions(dk94 PRIVATE DK94_PLAYER)
  target_link_libraries(dk94 PRIVATE tcc)
  set_target_properties(dk94 PROPERTIES BUILD_RPATH "\$ORIGIN" INSTALL_RPATH "\$ORIGIN")
else()
  # ---- developer build: recompile the ROM at build time ----
  if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Cross-compiling needs -DDK94_PLAYER=ON (the recompiler must run on the build machine)")
  endif()
  add_executable(dk94_recomp tools/recomp/dk94_recomp.c ${RECOMP_SOURCES})
  target_include_directories(dk94_recomp PRIVATE runtime/recomp)

  set(GEN_SOURCES ${GEN_DIR}/gen_table.c)
  math(EXPR LAST_BANK "${DK94_BANKS} - 1")
  foreach(b RANGE ${LAST_BANK})
    if(b LESS 10)
      set(b "0${b}")
    endif()
    list(APPEND GEN_SOURCES ${GEN_DIR}/gen_bank_${b}.c)
  endforeach()
  add_custom_command(
    OUTPUT ${GEN_SOURCES}
    COMMAND dk94_recomp ${DK94_ROM} ${GEN_DIR} ${DK94_BANKS}
    DEPENDS dk94_recomp ${DK94_ROM}
    COMMENT "Recompiling ROM to C"
    VERBATIM)
  # Generated functions are huge switch/goto state machines; -O1 keeps
  # compile times sane with little runtime cost.
  set_source_files_properties(${GEN_SOURCES} PROPERTIES COMPILE_OPTIONS "-O1")
  target_sources(dk94 PRIVATE ${GEN_SOURCES})
  target_compile_definitions(dk94 PRIVATE DK94_STATIC_CODE)
endif()

if(TARGET SDL2::SDL2main)
  target_link_libraries(dk94 PRIVATE SDL2::SDL2main)
endif()
if(DK94_STATIC_SDL AND TARGET SDL2::SDL2-static)
  target_link_libraries(dk94 PRIVATE SDL2::SDL2-static)
else()
  target_link_libraries(dk94 PRIVATE SDL2::SDL2)
endif()

if(NOT WIN32)
  target_link_libraries(dk94 PRIVATE m)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(dk94 PRIVATE -Wall -Wno-unused-label)
endif()
if(WIN32 AND TARGET SDL2::SDL2)
  # ship SDL2.dll next to the executable
  add_custom_command(TARGET dk94 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:dk94>)
endif()
if(WIN32)
  target_link_libraries(dk94 PRIVATE comdlg32)
endif()
if(MINGW)
  target_link_options(dk94 PRIVATE -static-libgcc)
  if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_link_options(dk94 PRIVATE -mwindows)
  endif()
endif()
