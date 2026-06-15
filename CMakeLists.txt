cmake_minimum_required(VERSION 3.20)

project(agent_framework
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_compile_definitions(AGENT_WINDOWS)
elseif(UNIX)
    add_compile_definitions(AGENT_LINUX)
endif()

add_subdirectory(agent_lib/src)

add_subdirectory(agent_cli)

if(WIN32)
    enable_testing()
    add_subdirectory(tests)
endif()
