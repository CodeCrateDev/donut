# scan: find devices on the local network with ARP requests.

project(scan
    VERSION 1.0.1
    DESCRIPTION "Find devices on the local network by scanning with ARP requests"
    LANGUAGES CXX
)

add_executable(donut-scan main.cpp)

target_link_libraries(donut-scan PRIVATE donut_common)

target_compile_definitions(donut-scan PRIVATE
    SCAN_VERSION="${PROJECT_VERSION}${DONUT_VERSION_SUFFIX}"
)
