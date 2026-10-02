# netinfo: show interfaces, addresses, the default route and DNS at a glance.

project(netinfo
    VERSION 1.0.0
    DESCRIPTION "Show network interfaces, addresses, routes and DNS at a glance"
    LANGUAGES CXX
)

add_executable(donut-netinfo main.cpp)

target_link_libraries(donut-netinfo PRIVATE donut_common)

target_compile_definitions(donut-netinfo PRIVATE
    NETINFO_VERSION="${PROJECT_VERSION}${DONUT_VERSION_SUFFIX}"
)
