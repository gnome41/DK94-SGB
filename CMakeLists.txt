cmake_minimum_required(VERSION 3.16)
project(dk94 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(DK94_ROM "${CMAKE_SOURCE_DIR}/Donkey Kong (World) (Rev 1) (SGB Enhanced).gb"
    CACHE FILEPATH "ROM to recompile")
set(DK94_BANKS 32 CACHE STRING "Number of 16 KiB ROM banks")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(SDL2 REQUIRED CONFIG)

set(RECOMP_DIR ${CMAKE_SOURCE_DIR}/tools/recomp)
set(GEN_DIR ${CMAKE_BINARY_DIR}/gen)
set(COVERAGE ${RECOMP_DIR}/coverage.txt)

set(GEN_SOURCES ${GEN_DIR}/gen_table.c)
math(EXPR LAST_BANK "${DK94_BANKS} - 1")
foreach(b RANGE ${LAST_BANK})
  if(b LESS 10)
    set(b "0${b}")
  endif()
  list(APPEND GEN_SOURCES ${GEN_DIR}/gen_bank_${b}.c)
endforeach()

add_custom_command(
  OUTPUT ${GEN_SOURCES} ${GEN_DIR}/interp_ops.inc
  COMMAND Python3::Interpreter ${RECOMP_DIR}/recomp.py
          --rom ${DK94_ROM} --out ${GEN_DIR}
          --config ${RECOMP_DIR}/overrides.toml
          --coverage ${COVERAGE}
          --banks ${DK94_BANKS}
  DEPENDS ${RECOMP_DIR}/recomp.py ${RECOMP_DIR}/analyze.py ${RECOMP_DIR}/emit_c.py
          ${RECOMP_DIR}/sm83.py ${RECOMP_DIR}/overrides.toml ${COVERAGE} ${DK94_ROM}
  COMMENT "Recompiling ROM to C"
  VERBATIM)

add_executable(dk94
  runtime/main.c runtime/cpu.c runtime/mem.c runtime/ppu.c runtime/apu.c
  runtime/pad.c runtime/files.c runtime/launcher.c runtime/sgb.c runtime/sgb_sound.c runtime/spc.c runtime/dsp.c runtime/platform_sdl.c
  ${GEN_SOURCES} ${GEN_DIR}/interp_ops.inc)
target_include_directories(dk94 PRIVATE runtime ${GEN_DIR})

# Generated functions are huge switch/goto state machines; -O1 keeps
# compile times sane with little runtime cost.
set_source_files_properties(${GEN_SOURCES} PROPERTIES COMPILE_OPTIONS "-O1")

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
