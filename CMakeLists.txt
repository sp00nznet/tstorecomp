cmake_minimum_required(VERSION 3.20)
project(tstorecomp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/W4)
  # windows.h defines min/max macros that collide with <algorithm>.
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  add_compile_options(-Wall -Wextra)
endif()

add_executable(tsto_host
  host/elf_image.cpp
  host/main.cpp
)
target_include_directories(tsto_host PRIVATE host)
