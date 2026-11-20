add_library(mhf_runtime
    core/RuntimeBootstrap.cpp
    core/RuntimeBootstrap.h
    core/RuntimeContext.cpp
    core/RuntimeContext.h
    core/ServiceRegistry.cpp
    core/ServiceRegistry.h
    interfaces/IRenderer.h
    interfaces/IAudioSystem.h
    interfaces/IInputSystem.h
    interfaces/IFileSystem.h
    interfaces/INetworkSystem.h
    interfaces/ILogger.h
    memory/MemoryAllocator.cpp
    memory/MemoryAllocator.h
)

target_include_directories(mhf_runtime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_compile_features(mhf_runtime PUBLIC cxx_std_20)
set_target_properties(mhf_runtime PROPERTIES
    CXX_EXTENSIONS OFF
)
mhf_target_warnings(mhf_runtime)

if(MHF_BUILD_TESTS)
    add_executable(mhf_runtime_memory_tests
        tests/MemoryAllocatorTests.cpp
    )
    target_include_directories(mhf_runtime_memory_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${PROJECT_SOURCE_DIR}/tests/support
    )
    target_compile_features(mhf_runtime_memory_tests PRIVATE cxx_std_20)
    target_link_libraries(mhf_runtime_memory_tests PRIVATE mhf_runtime)
    mhf_target_warnings(mhf_runtime_memory_tests)
    add_test(NAME runtime_memory_allocator COMMAND mhf_runtime_memory_tests)

    add_executable(mhf_runtime_core_tests
        tests/RuntimeContextTests.cpp
    )
    target_include_directories(mhf_runtime_core_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_features(mhf_runtime_core_tests PRIVATE cxx_std_20)
    target_link_libraries(mhf_runtime_core_tests PRIVATE mhf_runtime)
    mhf_target_warnings(mhf_runtime_core_tests)
    add_test(NAME runtime_context COMMAND mhf_runtime_core_tests)
endif()
