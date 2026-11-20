cmake_minimum_required(VERSION 3.28)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "BUILD_FUZZERS requer compilador Clang com suporte a libFuzzer (-fsanitize=fuzzer).")
endif()

set(_OMNIFRAME_FUZZ_TARGETS
    fuzz_project_serializer
    fuzz_dxf_interop
    fuzz_ifc_interop
)

foreach(F_NAME ${_OMNIFRAME_FUZZ_TARGETS})
    add_executable(${F_NAME} ${F_NAME}.cpp)
    target_include_directories(${F_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/source
    )
    target_link_libraries(${F_NAME} PRIVATE
        OmniFrameCore
        Qt6::Core
    )
    if(COMMAND omniframe_apply_compiler_options)
        omniframe_apply_compiler_options(${F_NAME})
    endif()
    set(_FUZZ_SAN_FLAGS "fuzzer")
    if(NOT OMNIFRAME_ENABLE_ASAN AND NOT OMNIFRAME_ENABLE_TSAN)
        string(APPEND _FUZZ_SAN_FLAGS ",address")
    endif()
    if(NOT OMNIFRAME_ENABLE_UBSAN AND NOT OMNIFRAME_ENABLE_TSAN)
        string(APPEND _FUZZ_SAN_FLAGS ",undefined")
    endif()
    target_compile_options(${F_NAME} PRIVATE -fsanitize=${_FUZZ_SAN_FLAGS} -fno-omit-frame-pointer)
    target_link_options(${F_NAME} PRIVATE -fsanitize=${_FUZZ_SAN_FLAGS})
endforeach()
