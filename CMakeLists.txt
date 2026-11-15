cmake_minimum_required(VERSION 3.31)
project(smart_gateway VERSION 1.0.0 LANGUAGES C)
set(CMAKE_BUILD_TYPE Debug)

configure_file(
    ${PROJECT_SOURCE_DIR}/include/version.h.in
    ${PROJECT_SOURCE_DIR}/generated/version.h
    @ONLY
)

# 主程序
file(GLOB SOURCES "${PROJECT_SOURCE_DIR}/src/*.c")
file(GLOB SERVICES "${PROJECT_SOURCE_DIR}/services/*.c")
file(GLOB MODULES "${PROJECT_SOURCE_DIR}/modules/*.c")

set(APP smart_gateway)
set(USR_LIBS /usr/local/lib)

add_executable(${APP} ${SOURCES} ${SERVICES} ${MODULES})
target_include_directories(${APP} PRIVATE
    /usr/local/include
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/generated
)
target_link_directories(${APP} PRIVATE 
                        ${USR_LIBS}/jpeg)
target_link_libraries(${APP} PRIVATE
    # 标准库
    Threads::Threads
    m

    # 第三方库
    jpeg 

    # 自定义库
    camera 
    ipc_utils
    http_utils
    camera_client 
    display
    sensor_client
    video_utils)

find_package(Threads REQUIRED)
target_compile_options(${APP} PRIVATE -pthread)

add_subdirectory(${PROJECT_SOURCE_DIR}/tools/soc ${PROJECT_BINARY_DIR}/tools/soc)

# ipc_utils 库
add_library(ipc_utils STATIC ${PROJECT_SOURCE_DIR}/lib/ipc/ipc_utils.c)
target_include_directories(ipc_utils PUBLIC ${PROJECT_SOURCE_DIR}/include)

# http_utils 库
add_library(http_utils STATIC ${PROJECT_SOURCE_DIR}/lib/http/http_utils.c)
target_include_directories(http_utils PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(http_utils PRIVATE ipc_utils)

# video_utils 库
add_library(video_utils STATIC ${PROJECT_SOURCE_DIR}/lib/video/video_utils.c)
target_include_directories(video_utils PUBLIC 
                        /usr/local/include
                        ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(video_utils PRIVATE jpeg)

# camera 库
add_library(camera STATIC ${PROJECT_SOURCE_DIR}/lib/camera/camera.c)
target_include_directories(camera PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(camera PRIVATE rt)

# camera_client 库
add_library(camera_client STATIC ${PROJECT_SOURCE_DIR}/lib/camera_client/camera_client.c)
target_include_directories(camera_client PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(camera_client PRIVATE ipc_utils)

# display 库
add_library(display STATIC ${PROJECT_SOURCE_DIR}/lib/display/display.c)
target_include_directories(display PUBLIC ${PROJECT_SOURCE_DIR}/include)

# sensor_client 库
add_library(sensor_client STATIC ${PROJECT_SOURCE_DIR}/lib/sensor_client/sensor_client.c)
target_include_directories(sensor_client PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(sensor_client PRIVATE ipc_utils)

# cJSON 库
add_library(cJSON STATIC ${PROJECT_SOURCE_DIR}/lib/cjson/cJSON.c)
target_include_directories(cJSON PUBLIC ${PROJECT_SOURCE_DIR}/include)

# x86 架构工具
set(HOST_BUILD_DIR "${PROJECT_BINARY_DIR}/tools/host")
include(ExternalProject)
ExternalProject_Add(ota_infogen
    SOURCE_DIR "${PROJECT_SOURCE_DIR}/tools/host"
    BINARY_DIR "${HOST_BUILD_DIR}/tools/host"

    CMAKE_COMMAND "${CMAKE_COMMAND}"

    CMAKE_ARGS
        -DCMAKE_C_COMPILER=/usr/bin/gcc
        -DCMAKE_BUILD_TYPE=Release

    INSTALL_COMMAND ""
)

# 自定义指令
set(OTA_STAGE ${PROJECT_SOURCE_DIR}/ota_stage)
set(OTA_PACKAGE ${PROJECT_SOURCE_DIR}/smart_gateway.tar.gz)

add_custom_target(
    package
    DEPENDS ${APP} ota_agent

    COMMAND rm -rf ${OTA_STAGE}
    COMMAND mkdir -p ${OTA_STAGE}/smart_gateway

    COMMAND cp 
        $<TARGET_FILE:${APP}>
        $<TARGET_FILE:ota_agent>
        ${OTA_STAGE}/smart_gateway/

    COMMAND cp
        ${PROJECT_SOURCE_DIR}/init.sh
        ${PROJECT_SOURCE_DIR}/start.sh
        ${OTA_STAGE}/smart_gateway/

    COMMAND cp -r
        ${PROJECT_SOURCE_DIR}/web
        ${OTA_STAGE}/smart_gateway/

    # 生成 tar.gz
    COMMAND tar -czf
        ${OTA_PACKAGE}
        -C ${OTA_STAGE}
        smart_gateway

    COMMAND rm -rf ${OTA_STAGE}

    COMMENT "Packaging smart_gateway OTA update"
)

set(BOARD_HOME /home/rance/linux/nfs/rootfs/home/menoa/program)
set(APP_DIR smart_gateway)
add_custom_target(
    send
    DEPENDS ${APP} ota_agent

    COMMAND sudo mkdir -p 
        ${BOARD_HOME}/${APP_DIR}/

    COMMAND sudo cp -a
        $<TARGET_FILE:${APP}>
        $<TARGET_FILE:ota_agent>
        ${BOARD_HOME}/${APP_DIR}/

    COMMAND sudo cp -a
        ${PROJECT_SOURCE_DIR}/init.sh
        ${PROJECT_SOURCE_DIR}/start.sh
        ${BOARD_HOME}/${APP_DIR}/

    COMMAND sudo cp -r
        ${PROJECT_SOURCE_DIR}/web
        ${BOARD_HOME}/${APP_DIR}/

    # COMMAND bash -c
    #     "if [ -f '${OTA_PACKAGE}' ]\; then sudo cp '${OTA_PACKAGE}' '${BOARD_HOME}/'\; fi"

    # COMMAND sudo cp $<TARGET_FILE:camera_ctl>
    #         ${BOARD_HOME}/
    # COMMAND sudo cp $<TARGET_FILE:camera_viewer>
    #         ${BOARD_HOME}/
    # COMMAND sudo cp $<TARGET_FILE:sensor_ctl>
    #         ${BOARD_HOME}/
    COMMENT "Copying programs to board"
)