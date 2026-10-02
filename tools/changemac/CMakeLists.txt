# changemac: change the MAC address of a network interface.

project(changemac
    VERSION 1.0.0
    DESCRIPTION "Change the MAC address of a network interface"
    LANGUAGES CXX
)

add_executable(donut-changemac main.cpp)

target_link_libraries(donut-changemac PRIVATE donut_common)

target_compile_definitions(donut-changemac PRIVATE
    CHANGEMAC_VERSION="${PROJECT_VERSION}${DONUT_VERSION_SUFFIX}"
)
