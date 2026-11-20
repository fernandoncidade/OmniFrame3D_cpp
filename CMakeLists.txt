cmake_minimum_required(VERSION 3.28)

project(OmniFrame
    VERSION 2026.11.14.0
    DESCRIPTION "OmniFrame 3D - Porticos e Trelicas Espaciais 3D via FEM (C++23 / Qt6 / OpenGL / Eigen3)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ==============================================================================
# Selecao de alvos separados em builds dedicados:
#   build/OmniFrame3D     -> BUILD_APP=ON
#   build/Version         -> BUILD_VERSION=ON
#   build/CompileLanguage -> BUILD_COMPILE_LANGUAGE=ON
#   build/Testes          -> BUILD_TESTS=ON
# ==============================================================================
option(BUILD_APP "Compila o aplicativo GUI principal OmniFrame 3D" ON)
option(BUILD_TESTS "Compila a suite de testes automatizados (CTest)" OFF)
option(BUILD_VERSION "Compila o editor/auditor de versao VersionEditor" OFF)
option(BUILD_COMPILE_LANGUAGE "Compila as ferramentas CompileTranslationsGUI e CompileTranslationsTerminal" OFF)

function(omniframe_apply_compiler_options target_name)
    if(WIN32)
        target_compile_definitions(${target_name} PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            UNICODE
            _UNICODE
        )
    endif()
    if(MSVC)
        target_compile_options(${target_name} PRIVATE
            /W4
            /utf-8
            /permissive-
            $<$<CONFIG:Release>:/O2 /Oi /Ot>
        )
    else()
        target_compile_options(${target_name} PRIVATE
            -Wall
            -Wextra
            -Wno-unused-parameter
            $<$<CONFIG:Release>:-O3>
        )
    endif()
endfunction()

# Determina componentes Qt6 necessarios conforme os alvos ativos
set(_OMNIFRAME_QT_COMPONENTS Core)
if(BUILD_APP OR BUILD_TESTS OR BUILD_VERSION OR BUILD_COMPILE_LANGUAGE)
    list(APPEND _OMNIFRAME_QT_COMPONENTS Gui)
endif()
if(BUILD_APP OR BUILD_TESTS OR BUILD_VERSION OR BUILD_COMPILE_LANGUAGE)
    list(APPEND _OMNIFRAME_QT_COMPONENTS Widgets)
endif()
if(BUILD_APP OR BUILD_TESTS)
    list(APPEND _OMNIFRAME_QT_COMPONENTS OpenGL OpenGLWidgets)
endif()
list(REMOVE_DUPLICATES _OMNIFRAME_QT_COMPONENTS)

find_package(Qt6 6.5 REQUIRED COMPONENTS ${_OMNIFRAME_QT_COMPONENTS})

get_target_property(_QT_MOC_LOCATION Qt6::moc IMPORTED_LOCATION)
if(NOT _QT_MOC_LOCATION)
    get_target_property(_QT_MOC_LOCATION Qt6::moc IMPORTED_LOCATION_RELEASE)
endif()
if(_QT_MOC_LOCATION)
    get_filename_component(OMNIFRAME_QT_BIN_DIR "${_QT_MOC_LOCATION}" DIRECTORY)
elseif(DEFINED ENV{QTDIR} AND EXISTS "$ENV{QTDIR}/bin")
    set(OMNIFRAME_QT_BIN_DIR "$ENV{QTDIR}/bin")
else()
    set(OMNIFRAME_QT_BIN_DIR "C:/Qt/6.11.1/msvc2022_64/bin")
endif()

if(WIN32)
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/source/assets/icons/omniframe.ico" OMNIFRAME_APP_ICON_RC_PATH)
    set(OMNIFRAME_GENERATED_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/generated/windows_app_icon.rc")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows_app_icon.rc.in"
        "${OMNIFRAME_GENERATED_ICON_RC}"
        @ONLY
    )
endif()

# ==============================================================================
# Nucleo Computacional (Eigen3 + OmniFrameCore) - somente App e Testes
# ==============================================================================
if(BUILD_APP OR BUILD_TESTS)
    find_package(Eigen3 3.4 QUIET NO_MODULE)
    if(NOT Eigen3_FOUND)
        message(STATUS "Eigen3 nao encontrado no sistema. Obtendo Eigen 3.4.0 via FetchContent (header-only)...")
        include(FetchContent)
        set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(EIGEN_BUILD_DOC OFF CACHE BOOL "" FORCE)
        set(EIGEN_BUILD_PKGCONFIG OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            Eigen3
            GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
            GIT_TAG 3.4.0
            GIT_SHALLOW TRUE
            SOURCE_SUBDIR __cmake_skip_subdirectory
        )
        FetchContent_MakeAvailable(Eigen3)
        if(NOT TARGET Eigen3::Eigen)
            add_library(Eigen3::Eigen INTERFACE IMPORTED)
            set_target_properties(Eigen3::Eigen PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${eigen3_SOURCE_DIR}"
            )
        endif()
    endif()

    set(OMNIFRAME_KERNEL_SOURCES
        source/kernel/core/KernelTypes.hpp
        source/kernel/core/DofManager.hpp
        source/kernel/core/DofManager.cpp
        source/kernel/materials/IConstitutiveModel.hpp
        source/kernel/materials/LinearElasticMaterial.hpp
        source/kernel/materials/LinearElasticMaterial.cpp
        source/kernel/materials/J2PlasticityMaterial.hpp
        source/kernel/materials/J2PlasticityMaterial.cpp
        source/kernel/geometry/CrossSectionGeometry.hpp
        source/kernel/geometry/CrossSectionGeometry.cpp
        source/kernel/elements/IFiniteElement.hpp
        source/kernel/elements/BeamTruss1D.hpp
        source/kernel/elements/BeamTruss1D.cpp
        source/kernel/elements/Corotational1D.hpp
        source/kernel/elements/Corotational1D.cpp
        source/kernel/elements/ShellElements.hpp
        source/kernel/elements/ShellElements.cpp
        source/kernel/elements/SolidElements.hpp
        source/kernel/elements/SolidElements.cpp
        source/kernel/mesh/MeshPrimitives.hpp
        source/kernel/mesh/MeshGenerator.hpp
        source/kernel/mesh/MeshGenerator.cpp
        source/kernel/assembly/GlobalAssembler.hpp
        source/kernel/assembly/GlobalAssembler.cpp
        source/kernel/solver/LinearSolverSuite.hpp
        source/kernel/solver/LinearSolverSuite.cpp
        source/kernel/analysis/LoadSystem.hpp
        source/kernel/analysis/LoadSystem.cpp
        source/kernel/analysis/BucklingSolver.hpp
        source/kernel/analysis/BucklingSolver.cpp
        source/kernel/analysis/DynamicSolver.hpp
        source/kernel/analysis/DynamicSolver.cpp
        source/kernel/analysis/NonlinearSolver.hpp
        source/kernel/analysis/NonlinearSolver.cpp
        source/kernel/postprocessing/PostProcessor.hpp
        source/kernel/postprocessing/PostProcessor.cpp
        source/kernel/codes/NbrCodeChecks.hpp
        source/kernel/codes/NbrCodeChecks.cpp
    )

    add_library(OmniFrameKernel STATIC
        ${OMNIFRAME_KERNEL_SOURCES}
    )
    target_include_directories(OmniFrameKernel PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/source"
    )
    target_link_libraries(OmniFrameKernel PUBLIC
        Eigen3::Eigen
    )
    omniframe_apply_compiler_options(OmniFrameKernel)

    set(OMNIFRAME_CORE_SOURCES
        source/core/Types.hpp
        source/core/Material.hpp
        source/core/Node.hpp
        source/core/CrossSection.hpp
        source/core/CrossSection.cpp
        source/core/Element.hpp
        source/core/Element.cpp
        source/core/StructuralModel.hpp
        source/core/StructuralModel.cpp
        source/solver/FemSolver3D.hpp
        source/solver/FemSolver3D.cpp
        source/io/ProjectSerializer.hpp
        source/io/ProjectSerializer.cpp
        source/io/DxfInterop.hpp
        source/io/DxfInterop.cpp
        source/io/IfcInterop.hpp
        source/io/IfcInterop.cpp
        source/io/PdfReportGenerator.hpp
        source/io/PdfReportGenerator.cpp
    )

    add_library(OmniFrameCore STATIC
        ${OMNIFRAME_CORE_SOURCES}
    )
    target_include_directories(OmniFrameCore PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/source"
    )
    target_link_libraries(OmniFrameCore PUBLIC
        OmniFrameKernel
        Qt6::Core
        Qt6::Gui
    )
    omniframe_apply_compiler_options(OmniFrameCore)

    add_executable(omniframe-cli
        source/cli/main_cli.cpp
    )
    set_target_properties(omniframe-cli PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        OUTPUT_NAME "omniframe-cli"
    )
    target_include_directories(omniframe-cli PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/source"
    )
    target_link_libraries(omniframe-cli PRIVATE
        OmniFrameCore
        Qt6::Core
        Qt6::Gui
    )
    omniframe_apply_compiler_options(omniframe-cli)
    if(WIN32 AND BUILD_APP)
        add_custom_command(TARGET omniframe-cli POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                -DBINARY_PATH=$<TARGET_FILE:omniframe-cli>
                -DQT_BIN_DIR=${OMNIFRAME_QT_BIN_DIR}
                -DCOPY_QT_TRANSLATIONS=OFF
                -DCLEANUP_ARTIFACTS=OFF
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/deploy_optional_qt_binary.cmake"
            COMMENT "Executando deploy Qt6 para omniframe-cli.exe"
        )
    endif()

    find_package(OpenGL REQUIRED)

    set(OMNIFRAME_UI_SOURCES
        source/locale/GerenciadorTraducao.hpp
        source/locale/GerenciadorTraducao.cpp
        source/render/Camera3D.hpp
        source/render/Camera3D.cpp
        source/render/RenderSettings.hpp
        source/render/RenderSettings.cpp
        source/render/ShaderCollection.hpp
        source/render/Viewport3D.hpp
        source/render/Viewport3D.cpp
        source/ui/AboutDialog.hpp
        source/ui/AboutDialog.cpp
        source/ui/Dialogs.hpp
        source/ui/Dialogs.cpp
        source/ui/EngineeringDialogs.hpp
        source/ui/EngineeringDialogs.cpp
        source/ui/GuidedSessionDialog.hpp
        source/ui/GuidedSessionDialog.cpp
        source/ui/ManualDialog.hpp
        source/ui/ManualDialog.cpp
        source/ui/RenderSettingsDialog.hpp
        source/ui/RenderSettingsDialog.cpp
        source/ui/ThemeManager.hpp
        source/ui/ThemeManager.cpp
        source/ui/WindowZOrderManager.hpp
        source/ui/WindowZOrderManager.cpp
        source/ui/MainWindow.hpp
        source/ui/MainWindow.cpp
    )

    add_library(OmniFrameUi STATIC
        ${OMNIFRAME_UI_SOURCES}
    )
    target_include_directories(OmniFrameUi PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/source"
    )
    target_link_libraries(OmniFrameUi PUBLIC
        OmniFrameCore
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets
        OpenGL::GL
    )
    omniframe_apply_compiler_options(OmniFrameUi)
endif()

# ==============================================================================
# Aplicativo Principal: OmniFrame 3D (build/OmniFrame3D)
# ==============================================================================
if(BUILD_APP)
    find_package(Qt6 COMPONENTS LinguistTools QUIET)

    if(TARGET Qt6::lrelease)
        set(OMNIFRAME_LRELEASE_EXECUTABLE Qt6::lrelease)
    else()
        find_program(OMNIFRAME_LRELEASE_EXECUTABLE
            NAMES lrelease lrelease-qt6
            HINTS "${OMNIFRAME_QT_BIN_DIR}"
            REQUIRED
        )
    endif()

    set(OMNIFRAME_GUI_SOURCES
        source/main.cpp
    )
    if(WIN32)
        list(APPEND OMNIFRAME_GUI_SOURCES "${OMNIFRAME_GENERATED_ICON_RC}")
    endif()

    add_executable(OmniFrame WIN32 ${OMNIFRAME_GUI_SOURCES})
    set_target_properties(OmniFrame PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        OUTPUT_NAME "OmniFrame"
    )
    target_include_directories(OmniFrame PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/source"
    )
    target_link_libraries(OmniFrame PRIVATE
        OmniFrameUi
    )
    omniframe_apply_compiler_options(OmniFrame)

    # Compilacao dos catalogos de traducao (.ts -> .qm)
    set(_OMNIFRAME_QM_FILES)
    foreach(_LOCALE_CODE IN ITEMS pt_BR en_US)
        set(_TS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/source/locale/language/omniframe_${_LOCALE_CODE}.ts")
        set(_QM_FILE "${CMAKE_BINARY_DIR}/translations/omniframe_${_LOCALE_CODE}.qm")
        add_custom_command(
            OUTPUT "${_QM_FILE}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/translations"
            COMMAND "${OMNIFRAME_LRELEASE_EXECUTABLE}" "${_TS_FILE}" -qm "${_QM_FILE}"
            DEPENDS "${_TS_FILE}"
            COMMENT "Compilando traducao Qt: omniframe_${_LOCALE_CODE}.ts -> omniframe_${_LOCALE_CODE}.qm"
            VERBATIM
        )
        set_source_files_properties("${_QM_FILE}" PROPERTIES GENERATED TRUE)
        list(APPEND _OMNIFRAME_QM_FILES "${_QM_FILE}")
    endforeach()
    add_custom_target(OmniFrameTranslations DEPENDS ${_OMNIFRAME_QM_FILES})
    add_dependencies(OmniFrame OmniFrameTranslations)
    add_dependencies(OmniFrame omniframe-cli)

    # Copia apenas os diretorios/arquivos necessarios de source/assets para runtime
    set(_OMNIFRAME_ASSET_DIRS
        ABOUT
        CLC
        COPYRIGHT
        EULA
        LICENSES
        MANUAL
        NOTICES
        PRIVACY_POLICY
        RELEASE
    )
    set(_OMNIFRAME_EMBEDDED_ASSET_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/source/assets/icons/omniframe.ico"
        "${CMAKE_CURRENT_SOURCE_DIR}/source/assets/icons/omniframe.png"
    )
    foreach(_ASSET_DIR IN LISTS _OMNIFRAME_ASSET_DIRS)
        file(GLOB_RECURSE _DIR_ASSET_FILES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/source/assets/${_ASSET_DIR}/*"
        )
        list(APPEND _OMNIFRAME_EMBEDDED_ASSET_FILES ${_DIR_ASSET_FILES})
        add_custom_command(TARGET OmniFrame POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/source/assets/${_ASSET_DIR}"
                "$<TARGET_FILE_DIR:OmniFrame>/assets/${_ASSET_DIR}"
        )
    endforeach()

    # Embute assets de runtime e traducoes .qm no executavel (fallback independente de diretorio)
    qt_add_resources(OmniFrame "omniframe_runtime_assets"
        PREFIX "/omniframe"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/source"
        FILES ${_OMNIFRAME_EMBEDDED_ASSET_FILES}
    )
    qt_add_resources(OmniFrame "omniframe_runtime_translations"
        PREFIX "/omniframe"
        BASE "${CMAKE_BINARY_DIR}"
        FILES ${_OMNIFRAME_QM_FILES}
    )

    add_custom_command(TARGET OmniFrame POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:OmniFrame>/assets/icons"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/source/assets/icons/omniframe.ico"
            "$<TARGET_FILE_DIR:OmniFrame>/assets/icons/omniframe.ico"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/source/assets/icons/omniframe.png"
            "$<TARGET_FILE_DIR:OmniFrame>/assets/icons/omniframe.png"
    )
    foreach(_QM_FILE IN LISTS _OMNIFRAME_QM_FILES)
        get_filename_component(_QM_NAME "${_QM_FILE}" NAME)
        add_custom_command(TARGET OmniFrame POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:OmniFrame>/translations"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_QM_FILE}"
                "$<TARGET_FILE_DIR:OmniFrame>/translations/${_QM_NAME}"
        )
    endforeach()

    if(WIN32)
        add_custom_command(TARGET OmniFrame POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                -DBINARY_PATH=$<TARGET_FILE:OmniFrame>
                -DQT_BIN_DIR=${OMNIFRAME_QT_BIN_DIR}
                -DCOPY_QT_TRANSLATIONS=ON
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/deploy_optional_qt_binary.cmake"
            COMMENT "Executando deploy Qt6 e limpeza de artefatos para OmniFrame.exe"
        )
    endif()
endif()

# ==============================================================================
# Ferramentas Auxiliares: VersionEditor (build/Version) e CompileLanguage
# ==============================================================================
if(BUILD_VERSION OR BUILD_COMPILE_LANGUAGE)
    add_subdirectory(mocks)
endif()

# ==============================================================================
# Suite de Testes Automatizados (build/Testes)
# ==============================================================================
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
