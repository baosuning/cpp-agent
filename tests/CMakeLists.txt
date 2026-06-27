add_executable(tool_test tool_test/main.cpp)
target_link_libraries(tool_test PRIVATE agent_framework)
if(WIN32)
    target_link_libraries(tool_test PRIVATE winhttp)
endif()
set_target_properties(tool_test PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

add_executable(framework_test framework_test/main.cpp)
target_link_libraries(framework_test PRIVATE agent_framework)
target_include_directories(framework_test PRIVATE
    ${CMAKE_SOURCE_DIR}/agent_lib/src
)
if(WIN32)
    target_link_libraries(framework_test PRIVATE winhttp)
endif()
set_target_properties(framework_test PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# IlinkClient 图片下载和发送异常处理测试
add_executable(ilink_image_test
    ilink_image_test/main.cpp
    ${CMAKE_SOURCE_DIR}/agent_cli/src/channels/wechat/ilink_client.cpp
)
target_link_libraries(ilink_image_test PRIVATE agent_framework)
target_include_directories(ilink_image_test PRIVATE
    ${CMAKE_SOURCE_DIR}/agent_lib/src
    ${CMAKE_SOURCE_DIR}/agent_cli
    ${CMAKE_SOURCE_DIR}/agent_cli/src
)
if(WIN32)
    target_link_libraries(ilink_image_test PRIVATE winhttp)
endif()
set_target_properties(ilink_image_test PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
