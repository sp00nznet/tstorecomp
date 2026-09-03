cmake_minimum_required(VERSION 3.20)
project(tstorecomp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The loader, the Android shim layer and the host window are title-agnostic and
# live in androidrecomp. Only what is specific to this game belongs here --
# right now that is contract/bgcore.txt, and next the JNI bridge.
add_subdirectory(androidrecomp)

add_executable(tsto_host androidrecomp/tools/arc_host.cpp)
target_link_libraries(tsto_host PRIVATE androidrecomp)
