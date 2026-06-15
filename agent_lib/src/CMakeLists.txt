file(GLOB_RECURSE AGENT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/agent/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/skill/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tool/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mcp/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/llm/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/personality/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/confirm/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/util/*.cpp
)

add_library(agent_framework STATIC ${AGENT_SOURCES})

target_include_directories(agent_framework
    PUBLIC
        ${CMAKE_SOURCE_DIR}/agent_lib/include
        ${CMAKE_SOURCE_DIR}/third_party
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(agent_framework PUBLIC cxx_std_20)

if(WIN32)
    target_link_libraries(agent_framework PRIVATE winhttp)
endif()

set_target_properties(agent_framework PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
