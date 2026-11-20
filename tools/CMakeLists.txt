find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_target(mhf_asset_builder
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/mhf_asset_builder.py" --help
    COMMENT "MHForever asset builder (hash validation and optional PS4 SDK package invocation)"
    VERBATIM
)
