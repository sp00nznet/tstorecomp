cmake_minimum_required(VERSION 3.20)
project(tstorecomp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/W4)
  # windows.h defines min/max macros that collide with <algorithm>.
  # Shim forwarders deliberately reimplement strcpy/strcat & friends.
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra)
endif()

add_executable(tsto_host
  host/elf_image.cpp
  host/shim.cpp
  host/main.cpp
)
target_include_directories(tsto_host PRIVATE host)

if(UNIX AND NOT APPLE)
  target_link_libraries(tsto_host PRIVATE dl)
endif()

# zlib satisfies 12 imports directly -- Bionic ships stock zlib, so every
# signature matches. On Windows point CMake at vcpkg:
#   -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
  target_link_libraries(tsto_host PRIVATE ZLIB::ZLIB)
  target_compile_definitions(tsto_host PRIVATE TSTO_HAVE_ZLIB)
  message(STATUS "zlib found -- 12 imports bound")
else()
  message(STATUS "zlib not found -- those 12 imports stay on the work list")
endif()
