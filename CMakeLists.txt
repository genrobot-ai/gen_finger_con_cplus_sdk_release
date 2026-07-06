cmake_minimum_required(VERSION 3.10)
project(finger_cpp_sdk VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)
include(CTest)
enable_testing()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
)

set(SDK_SOURCES
    src/das_protocol.cpp
    src/pack.cpp
    src/databus.cpp
    src/camera.cpp
    src/finger_system.cpp
    src/tactile_processing.cpp
)

add_library(finger_cpp_sdk_static STATIC ${SDK_SOURCES})
target_link_libraries(finger_cpp_sdk_static
    ${OpenCV_LIBS}
    Threads::Threads
)

add_library(finger_cpp_sdk SHARED ${SDK_SOURCES})
target_link_libraries(finger_cpp_sdk
    ${OpenCV_LIBS}
    Threads::Threads
)

add_executable(camera_cmd src/camera_cmd.cpp)
target_link_libraries(camera_cmd
    finger_cpp_sdk_static
    ${OpenCV_LIBS}
    Threads::Threads
)

add_executable(finger_sdk_tests tests/finger_sdk_tests.cpp)
target_link_libraries(finger_sdk_tests
    finger_cpp_sdk_static
    ${OpenCV_LIBS}
    Threads::Threads
)

add_test(NAME finger_sdk_tests COMMAND finger_sdk_tests)

install(TARGETS camera_cmd finger_cpp_sdk finger_cpp_sdk_static
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/ DESTINATION include)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/calib_result)
