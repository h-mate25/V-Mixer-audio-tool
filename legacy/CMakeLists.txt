cmake_minimum_required(VERSION 3.22)

project(WindowsReacASIO LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ASIO_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ASIOSDK" CACHE PATH "Path to the Steinberg ASIO SDK")

add_library(WindowsReacASIO SHARED
    src/asio/control_panel.cpp
    src/asio/asio_driver.cpp
    src/asio/asio_exports.cpp
    src/audio/audio_engine.cpp
    src/audio/channel_map.cpp
    src/audio/output_monitor.cpp
    src/audio/ring_buffer.cpp
    src/config/config.cpp
    src/diagnostics/logger.cpp
    src/reac/pcap_capture.cpp
    src/reac/pcap_sender.cpp
    src/reac/reac_packet_encoder.cpp
    src/reac/reac_packet_parser.cpp
    src/reac/reac_receiver.cpp
    src/reac/reac_return_map.cpp
    src/reac/reac_transmitter.cpp
    src/asio/WindowsReacASIO.def
)

target_include_directories(WindowsReacASIO PRIVATE
    "${ASIO_SDK_DIR}/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(WindowsReacASIO PRIVATE
    WIN32
    _WINDOWS
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
)

target_link_libraries(WindowsReacASIO PRIVATE
    Ole32
    Winmm
    Shlwapi
    Iphlpapi
)

add_executable(reac_probe
    tools/reac_probe/reac_probe.cpp
    src/reac/pcap_capture.cpp
    src/reac/reac_packet_parser.cpp
)

target_include_directories(reac_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
if (MSVC)
    target_compile_options(reac_probe PRIVATE /EHsc)
endif()
target_link_libraries(reac_probe PRIVATE
    Ole32
    Iphlpapi
)

add_executable(reac_tx_probe
    tools/reac_tx_probe/reac_tx_probe.cpp
    src/reac/pcap_capture.cpp
    src/reac/pcap_sender.cpp
    src/reac/reac_packet_encoder.cpp
    src/reac/reac_return_map.cpp
)

target_include_directories(reac_tx_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
if (MSVC)
    target_compile_options(reac_tx_probe PRIVATE /EHsc)
endif()
target_link_libraries(reac_tx_probe PRIVATE
    Ole32
    Iphlpapi
)

add_executable(reac_packet_test
    tools/reac_packet_test/reac_packet_test.cpp
    src/reac/reac_packet_encoder.cpp
    src/reac/reac_packet_parser.cpp
    src/reac/reac_return_map.cpp
)

target_include_directories(reac_packet_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
