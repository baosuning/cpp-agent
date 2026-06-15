add_executable(test_tools main.cpp)

target_link_libraries(test_tools PRIVATE agent_framework)

set_target_properties(test_tools PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)