cmake_minimum_required(VERSION 3.16)

project(donut
    VERSION 1.0.0
    DESCRIPTION "donut - a toolkit of small Linux utilities"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Shared toolkit code (version handling, CLI helpers, ...).
add_subdirectory(libs/donut)

# Every directory under tools/ that has its own CMakeLists.txt is one program
# of the toolkit; add a new directory there and it is built with everything else.
file(GLOB DONUT_TOOL_DIRS LIST_DIRECTORIES true tools/*)
foreach(tool_dir IN LISTS DONUT_TOOL_DIRS)
    if(EXISTS "${tool_dir}/CMakeLists.txt")
        add_subdirectory("${tool_dir}")
    endif()
endforeach()

message(STATUS "donut toolkit ${PROJECT_VERSION}")
