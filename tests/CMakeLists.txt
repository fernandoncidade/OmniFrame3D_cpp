cmake_minimum_required(VERSION 3.28)

set(OMNIFRAME_TEST_LIST
    test_Material
    test_CrossSection
    test_Element3D
    test_CoordinateTransform
    test_CantileverBenchmark
    test_EquilibriumReactions
    test_ProjectSerializer
    test_DistributedAndInternalForces
    test_ReleasesAndThermal
    test_SpringsSettlementsAndExamples
)

foreach(T_NAME ${OMNIFRAME_TEST_LIST})
    add_executable(${T_NAME} ${T_NAME}.cpp)
    target_include_directories(${T_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../source
    )
    target_link_libraries(${T_NAME} PRIVATE
        OmniFrameCore
        Qt6::Core
    )
    if(COMMAND omniframe_apply_compiler_options)
        omniframe_apply_compiler_options(${T_NAME})
    elseif(MSVC)
        target_compile_options(${T_NAME} PRIVATE /W4 /utf-8 /permissive-)
    else()
        target_compile_options(${T_NAME} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
    add_test(NAME ${T_NAME} COMMAND ${T_NAME})
    if(WIN32)
        set_tests_properties(${T_NAME} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
    endif()
endforeach()

# Target agregado para compatibilidade e conveniência (compila todos os testes)
add_custom_target(OmniFrameTest DEPENDS ${OMNIFRAME_TEST_LIST})

