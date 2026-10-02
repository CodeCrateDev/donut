# donut_common: code shared by all tools in the toolkit.

add_library(donut_common STATIC
    src/cli.cpp
    src/exec.cpp
    src/net.cpp
)

target_compile_features(donut_common PUBLIC cxx_std_20)

# The toolkit version comes from project(donut) in the root CMakeLists.txt.
configure_file(include/donut/version.h.in
    "${CMAKE_BINARY_DIR}/generated/donut/version.h"
    @ONLY
)

target_include_directories(donut_common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
)
