cmake_minimum_required(VERSION 3.31)

# camera_ctl 调试程序
add_executable(camera_ctl ${CMAKE_CURRENT_SOURCE_DIR}/camera_ctl.c)
target_include_directories(camera_ctl PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(camera_ctl PRIVATE ipc_utils)
set_target_properties(camera_ctl PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin"
)

# camera_viewer 调试程序
add_executable(camera_viewer ${CMAKE_CURRENT_SOURCE_DIR}/camera_viewer.c)
target_include_directories(camera_viewer PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(camera_viewer PRIVATE camera_client rt)
set_target_properties(camera_viewer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin"
)

# sensor_ctl 调试程序
add_executable(sensor_ctl ${CMAKE_CURRENT_SOURCE_DIR}/sensor_ctl.c)
target_include_directories(sensor_ctl PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(sensor_ctl PRIVATE ipc_utils)
set_target_properties(sensor_ctl PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin"
)

# 开发板 OTA 信息检查工具
add_executable(ota_agent ${CMAKE_CURRENT_SOURCE_DIR}/ota_agent.c)
target_include_directories(ota_agent PRIVATE
    /usr/local/include
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/generated
)
target_link_directories(ota_agent PRIVATE 
                        ${USR_LIBS}/openssl)
target_link_libraries(
    ota_agent PRIVATE 
    crypto
    cJSON
    ipc_utils
    http_utils
)
set_target_properties(ota_agent PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin"
)