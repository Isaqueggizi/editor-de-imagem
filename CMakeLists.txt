cmake_minimum_required(VERSION 3.20)
project(NexaFi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(NexaFi WIN32
    src/main.cpp
    resources/NexaFi.rc
)

target_compile_definitions(NexaFi PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(NexaFi PRIVATE resources)
target_link_libraries(NexaFi PRIVATE
    wlanapi iphlpapi ws2_32 winhttp dnsapi dwmapi
)

if(MINGW)
    target_link_options(NexaFi PRIVATE -static -static-libgcc -static-libstdc++)
endif()

install(TARGETS NexaFi RUNTIME DESTINATION .)
