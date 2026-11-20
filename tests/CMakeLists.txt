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
    test_Phase0ValidationAndBenchmarks
    test_Phase1KernelAndSolvers
    test_Phase2SectionsAnd1D
    test_Phase3NonlinearAndBuckling
    test_Phase4Dynamics
    test_Phase5Shells
    test_Phase6Solids
    test_Phase7MeshInteropCli
    test_Phase8ComplianceAndReports
    test_Phase9UiAndWorkflows
)

foreach(T_NAME ${OMNIFRAME_TEST_LIST})
    add_executable(${T_NAME} ${T_NAME}.cpp)
    target_include_directories(${T_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../source
    )
    if("${T_NAME}" STREQUAL "test_Phase9UiAndWorkflows" AND TARGET OmniFrameUi)
        target_link_libraries(${T_NAME} PRIVATE
            OmniFrameUi
            Qt6::Widgets
            Qt6::OpenGL
            Qt6::OpenGLWidgets
            OpenGL::GL
        )
        add_custom_command(TARGET ${T_NAME} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${T_NAME}>/translations"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/source/locale/language/omniframe_pt_BR.qm"
                "$<TARGET_FILE_DIR:${T_NAME}>/translations/omniframe_pt_BR.qm"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/source/locale/language/omniframe_en_US.qm"
                "$<TARGET_FILE_DIR:${T_NAME}>/translations/omniframe_en_US.qm"
        )
    else()
        target_link_libraries(${T_NAME} PRIVATE
            OmniFrameCore
            Qt6::Core
        )
    endif()
    if(("${T_NAME}" STREQUAL "test_Phase7MeshInteropCli" OR "${T_NAME}" STREQUAL "test_Phase8ComplianceAndReports") AND TARGET omniframe-cli)
        add_dependencies(${T_NAME} omniframe-cli)
        target_compile_definitions(${T_NAME} PRIVATE
            OMNIFRAME_CLI_PATH="$<TARGET_FILE:omniframe-cli>"
        )
    endif()
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
            ENVIRONMENT "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
    endif()
endforeach()

# Target agregado para compatibilidade e conveniência (compila todos os testes)
add_custom_target(OmniFrameTest DEPENDS ${OMNIFRAME_TEST_LIST})

