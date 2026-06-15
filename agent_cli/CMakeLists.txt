file(GLOB_RECURSE SEARCH_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_executable(agent_cli main.cpp ${SEARCH_SOURCES}
    ${CMAKE_SOURCE_DIR}/third_party/qrcodegen/qrcodegen.cpp
)

target_include_directories(agent_cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/agent_lib/src
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(agent_cli PRIVATE agent_framework)

if(WIN32)
    target_link_libraries(agent_cli PRIVATE winhttp)
endif()

set_target_properties(agent_cli PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
