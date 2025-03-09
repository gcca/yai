cmake_minimum_required(VERSION 3.30)

project(
  yai_chat_module
  VERSION 0.1.0
  LANGUAGES CXX)

include(cmake/CPM.cmake)

cpmaddpackage("gh:gcca/xai-cpp#master")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_COMPILER "clang")
set(CMAKE_CXX_COMPILER "clang++")

find_package(Python3 REQUIRED COMPONENTS Interpreter Development)

include_directories(${Python3_INCLUDE_DIRS})

function(add_yai_ext name)
  set(target "yai_${name}")
  add_library(${target} MODULE yai_ext/yai-${name}.cc yai_ext/yAi.cpp)
  set_target_properties(
    ${target}
    PROPERTIES PREFIX ""
               SUFFIX ".cpython-312-darwin.so"
               LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})
  target_link_libraries(${target} PRIVATE ${Python3_LIBRARIES} xAI::xAI)
  target_compile_options(${target} PRIVATE ${YAI_COMPILE_OPTIONS})
  if(APPLE)
    set_target_properties(${target} PROPERTIES LINK_FLAGS
                                               "-undefined dynamic_lookup")
  endif()
endfunction()

set(COMMON_COMPILE_OPTIONS
    -Wall
    -Wextra
    -Werror
    -Wpedantic
    -Wshadow
    -Weverything
    -Wconversion
    -Wsign-conversion
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wfloat-equal
    -Wformat=2
    -Wnull-dereference
    -Wundef
    -Wuninitialized
    -Wcast-align
    -Wformat-security
    -Wstrict-overflow
    -Wswitch-enum
    -Wunused-variable
    -Wunused-parameter
    -Wpointer-arith
    -Wcast-align
    -Wno-variadic-macros
    -fexceptions
    -fsafe-buffer-usage-suggestions
    -Wno-c++98-compat
    -Wno-c++98-compat-pedantic
    -Wno-padded
    -Wno-covered-switch-default
    -Wno-unsafe-buffer-usage)

set(YAI_COMPILE_OPTIONS ${COMMON_COMPILE_OPTIONS}
  -Wno-disabled-macro-expansion
  -Wno-cast-function-type-strict
  -Wno-missing-prototypes
  -Wno-old-style-cast
  -Wno-reserved-identifier
  -Wno-reserved-macro-identifier)

add_yai_ext(chat)
