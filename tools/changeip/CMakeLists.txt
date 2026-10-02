# changeip: change the IPv4 address of a network interface.

project(changeip
    VERSION 1.1.0
    DESCRIPTION "Change the IPv4 address, gateway and DNS of a network interface"
    LANGUAGES CXX
)

add_executable(donut-changeip main.cpp)

target_link_libraries(donut-changeip PRIVATE donut_common)

target_compile_definitions(donut-changeip PRIVATE
    CHANGEIP_VERSION="${PROJECT_VERSION}${DONUT_VERSION_SUFFIX}"
)
