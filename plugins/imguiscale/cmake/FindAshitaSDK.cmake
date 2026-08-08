####################################################################################################
# Locates and prepares the Ashita SDK for usage with CMAKE.
####################################################################################################

if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Ashita plugins must be compiled as 32bit (x86) modules!")
endif()

find_path(ASHITA_SDK_PATH
    NAMES "Ashita.h"
    PATHS
        ENV ASHITA4_SDK_PATH
        "${CMAKE_CURRENT_LIST_DIR}/../sdk"
        "${CMAKE_CURRENT_LIST_DIR}/../../sdk"
)

if (NOT ASHITA_SDK_PATH)
    message(FATAL_ERROR "Failed to locate the Ashita SDK! Set ASHITA4_SDK_PATH or place this project under plugins/.")
endif()

set(ASHITA_SDK_PATH_D3D8 ${ASHITA_SDK_PATH}/d3d8)
set(ASHITA_SDK_PATH_D3D8_INC ${ASHITA_SDK_PATH_D3D8}/includes/)
set(ASHITA_SDK_PATH_D3D8_LIB ${ASHITA_SDK_PATH_D3D8}/lib/)
set(ASHITA_SDK_PATH_FFXI ${ASHITA_SDK_PATH}/ffxi/)

find_library(ASHITA_SDK_LIB_D3D8 NAMES d3d8 PATHS ${ASHITA_SDK_PATH_D3D8_LIB} NO_DEFAULT_PATH)
find_library(ASHITA_SDK_LIB_D3DX8 NAMES d3dx8 PATHS ${ASHITA_SDK_PATH_D3D8_LIB} NO_DEFAULT_PATH)
find_library(ASHITA_SDK_LIB_DINPUT8 NAMES dinput8 PATHS ${ASHITA_SDK_PATH_D3D8_LIB} NO_DEFAULT_PATH)
find_library(ASHITA_SDK_LIB_DXERR8 NAMES dxerr8 PATHS ${ASHITA_SDK_PATH_D3D8_LIB} NO_DEFAULT_PATH)
find_library(ASHITA_SDK_LIB_DXGUID NAMES dxguid PATHS ${ASHITA_SDK_PATH_D3D8_LIB} NO_DEFAULT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(AshitaSDK
    DEFAULT_MSG
    ASHITA_SDK_PATH
    ASHITA_SDK_PATH_D3D8
    ASHITA_SDK_PATH_D3D8_INC
    ASHITA_SDK_PATH_FFXI
)

set(ASHITA_SDK_INCLUDE_DIRS
    ${ASHITA_SDK_PATH}
    ${ASHITA_SDK_PATH_D3D8_INC}
    ${ASHITA_SDK_PATH_FFXI}
)

set(ASHITA_SDK_LIBRARY_DIRS ${ASHITA_SDK_PATH_D3D8_LIB})
set(ASHITA_SDK_LIBRARY_LIBS
    ${ASHITA_SDK_LIB_D3D8}
    ${ASHITA_SDK_LIB_D3DX8}
    ${ASHITA_SDK_LIB_DINPUT8}
    ${ASHITA_SDK_LIB_DXERR8}
    ${ASHITA_SDK_LIB_DXGUID}
)

if (MSVC)
    function(ashita_sdk_initialize)
        set(CMAKE_EXPORT_COMPILE_COMMANDS ON PARENT_SCOPE)
        set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH OFF PARENT_SCOPE)
        set(CMAKE_POSITION_INDEPENDENT_CODE ON PARENT_SCOPE)
        unset(CMAKE_CXX_FLAGS CACHE)
        unset(CMAKE_CXX_FLAGS_DEBUG CACHE)
        unset(CMAKE_CXX_FLAGS_MINSIZEREL CACHE)
        unset(CMAKE_CXX_FLAGS_RELEASE CACHE)
        unset(CMAKE_CXX_FLAGS_RELWITHDEBINFO CACHE)
        if (NOT "${CMAKE_CXX_STANDARD}")
            set(CMAKE_CXX_STANDARD 23 CACHE STRING "Default C++ standard to be used.")
        endif()
    endfunction()

    function(ashita_sdk_attach target)
        target_include_directories(${target} PRIVATE ${ASHITA_SDK_INCLUDE_DIRS})
        if (ASHITA_SDK_LIBRARY_DIRS)
            target_link_directories(${target} PRIVATE ${ASHITA_SDK_LIBRARY_DIRS})
        endif()
        if (ASHITA_SDK_LIBRARY_LIBS)
            target_link_libraries(${target} PRIVATE ${ASHITA_SDK_LIBRARY_LIBS})
        endif()
        target_link_libraries(${target} PRIVATE "legacy_stdio_definitions.lib")
    endfunction()

    function(ashita_sdk_set_compiler_defaults target)
        target_compile_options(${target} PUBLIC
            /analyze- /bigobj /D_MBCS /DWIN32 /D_WINDOWS /diagnostics:column /EHa
            /errorReport:prompt /FC /fp:precise /Gd /GF /Gm- /GS /MP /nologo /Oy-
            /permissive- /sdl /Zc:forScope /Zc:inline /Zc:wchar_t
        )
        if (CMAKE_BUILD_TYPE STREQUAL "Debug")
            target_compile_options(${target} PUBLIC /DDEBUG /MDd /Ob0 /Od /RTC1 /Zi)
        endif()
        if (CMAKE_BUILD_TYPE STREQUAL "Release")
            target_compile_options(${target} PUBLIC /DNDEBUG /GL /Gy- /MD /Ob2 /Oi /O2)
        endif()
    endfunction()

    function(ashita_sdk_set_linker_defaults target)
        target_link_options(${target} PUBLIC
            /ERRORREPORT:PROMPT /MACHINE:X86 /NOLOGO /NXCOMPAT /TLBID:1
        )
        if (CMAKE_BUILD_TYPE STREQUAL "Debug")
            target_link_options(${target} PUBLIC /INCREMENTAL)
        endif()
        if (CMAKE_BUILD_TYPE STREQUAL "Release")
            target_link_options(${target} PUBLIC /INCREMENTAL:NO /LTCG:incremental /NOCOFFGRPINFO /OPT:ICF /OPT:REF)
        endif()
    endfunction()

    function(ashita_sdk_set_compiler_warn_level target level)
        if (level STREQUAL "")
            set(level 3)
        endif()
        target_compile_options(${target} PUBLIC "/W${level}")
    endfunction()

    function(ashita_sdk_set_linker_enable_debug_linking target)
        target_link_options(${target} PUBLIC /DEBUG)
    endfunction()

    function(ashita_sdk_set_linker_disable_debug_linking target)
        target_link_options(${target} PUBLIC /DEBUG:NONE)
    endfunction()

    function(ashita_sdk_set_linker_enable_safe_seh target)
        target_link_options(${target} PUBLIC /SAFESEH)
    endfunction()
endif()
