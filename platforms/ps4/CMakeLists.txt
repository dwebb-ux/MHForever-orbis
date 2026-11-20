option(MHF_BUILD_PS4 "Build the PlayStation 4 backend" OFF)

if(NOT MHF_BUILD_PS4)
    return()
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Orbis")
    message(FATAL_ERROR
        "MHF_BUILD_PS4=ON requires -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/orbis.cmake")
endif()

if(NOT DEFINED ENV{SCE_ORBIS_SDK_DIR})
    message(FATAL_ERROR
        "MHF_BUILD_PS4=ON requires SCE_ORBIS_SDK_DIR")
endif()

add_library(mhf_platform_ps4 STATIC
    Ps4FileSystem.cpp
    Ps4FileSystem.h
    Ps4Input.cpp
    Ps4Input.h
    Ps4Logger.cpp
    Ps4Logger.h
    Ps4RuntimeBootstrap.cpp
    Ps4RuntimeBootstrap.h
)
target_include_directories(mhf_platform_ps4
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_compile_features(mhf_platform_ps4 PUBLIC cxx_std_20)
set_target_properties(mhf_platform_ps4 PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(mhf_platform_ps4 PUBLIC mhf_runtime)
mhf_target_warnings(mhf_platform_ps4)
