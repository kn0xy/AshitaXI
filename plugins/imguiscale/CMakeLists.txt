cmake_minimum_required(VERSION 3.22)

set(ASHITA_PROJECT_NAME "imguiscale")
project(${ASHITA_PROJECT_NAME} CXX)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

if (NOT CMAKE_BUILD_PARALLEL_LEVEL)
    include(ProcessorCount)
    ProcessorCount(N)
    if (NOT N EQUAL 0)
        set(CMAKE_BUILD_PARALLEL_LEVEL ${N})
    endif()
endif()

find_package(AshitaSDK REQUIRED)
ashita_sdk_initialize()

set(${ASHITA_PROJECT_NAME}_src
    "src/exports.def"
    "src/imguiscale.cpp"
    "src/imguiscale.hpp"
)

add_library(${ASHITA_PROJECT_NAME} MODULE ${${ASHITA_PROJECT_NAME}_src})
set_target_properties(${ASHITA_PROJECT_NAME} PROPERTIES
    CXX_EXTENSIONS ON
    CXX_STANDARD_REQUIRED ON
    LINKER_LANGUAGE CXX
)

if (WIN32)
    set_target_properties(${ASHITA_PROJECT_NAME} PROPERTIES
        OUTPUT_NAME ${ASHITA_PROJECT_NAME}
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    )
endif()

ashita_sdk_attach(${ASHITA_PROJECT_NAME})
ashita_sdk_set_compiler_defaults(${ASHITA_PROJECT_NAME})
ashita_sdk_set_compiler_warn_level(${ASHITA_PROJECT_NAME} 4)
ashita_sdk_set_linker_defaults(${ASHITA_PROJECT_NAME})

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    ashita_sdk_set_linker_enable_debug_linking(${ASHITA_PROJECT_NAME})
else()
    ashita_sdk_set_linker_disable_debug_linking(${ASHITA_PROJECT_NAME})
endif()

ashita_sdk_set_linker_enable_safe_seh(${ASHITA_PROJECT_NAME})
