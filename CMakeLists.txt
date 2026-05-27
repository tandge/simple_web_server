cmake_minimum_required(VERSION 3.10)
project(LightWebServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter")
endif()

# 源文件
file(GLOB_RECURSE SOURCES src/*.cpp)
list(FILTER SOURCES EXCLUDE REGEX ".*\\.git.*")

add_executable(webserver main.cpp ${SOURCES})

target_include_directories(webserver PRIVATE ${CMAKE_SOURCE_DIR})

# 平台依赖库
if(WIN32)
    target_link_libraries(webserver ws2_32)
    # Windows需要定义FD_SETSIZE
    target_compile_definitions(webserver PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
else()
    target_link_libraries(webserver pthread)
endif()

# Release优化
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "CXX compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Target platform: ${CMAKE_SYSTEM_NAME}")
