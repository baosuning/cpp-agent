add_executable(comprehensive_test main.cpp)

target_link_libraries(comprehensive_test PRIVATE agent_framework)

target_include_directories(comprehensive_test PRIVATE
    ${CMAKE_SOURCE_DIR}/agent_lib/src
)

set_target_properties(comprehensive_test PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)