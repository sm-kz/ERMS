cmake_minimum_required(VERSION 3.31)
project(ota_infogen LANGUAGES C)

find_package(OpenSSL REQUIRED COMPONENTS Crypto)

add_executable(ota_infogen ota_infogen.c)
target_link_libraries(ota_infogen PRIVATE
    OpenSSL::Crypto
)
set_target_properties(ota_infogen PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/../../bin"
)