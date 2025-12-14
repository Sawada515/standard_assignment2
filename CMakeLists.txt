cmake_minimum_required(VERSION 3.10)

project(standard_assignment2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(OpenCV REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/src/include)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

set(SOURCES
    src/include/logger/logger.hpp
    src/lib/camera/v4l2_capture.cpp
    src/lib/camera/camera_capture_thread.cpp
    src/lib/image_processor/image_processor.cpp
    src/lib/network/udp_sender.cpp
    src/lib/network/udp_sender_thread.cpp
    src/main.cpp
)

add_executable(webcam_app ${SOURCES})

#target_compile_options(webcam_app PRIVATE -Wall -Wextra -Wpedantic)
target_compile_options(webcam_app PRIVATE -Wall -Wextra -Wpedantic -ggdb3 -O0)

target_link_libraries(
    webcam_app PRIVATE
    ${OpenCV_LIBS}
    Threads::Threads
)
