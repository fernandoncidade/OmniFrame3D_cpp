cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED BUILD_VERSION)
    set(BUILD_VERSION ON)
endif()
if(NOT DEFINED BUILD_COMPILE_LANGUAGE)
    set(BUILD_COMPILE_LANGUAGE ON)
endif()

get_filename_component(_OMNIFRAME_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
set(_OMNIFRAME_ICONS_SRC "${_OMNIFRAME_ROOT_DIR}/source/assets/icons")
set(_DEPLOY_SCRIPT "${_OMNIFRAME_ROOT_DIR}/cmake/deploy_optional_qt_binary.cmake")

if(WIN32 AND NOT DEFINED OMNIFRAME_GENERATED_ICON_RC)
    file(TO_CMAKE_PATH "${_OMNIFRAME_ICONS_SRC}/omniframe.ico" OMNIFRAME_APP_ICON_RC_PATH)
    set(OMNIFRAME_GENERATED_ICON_RC "${CMAKE_BINARY_DIR}/generated/windows_app_icon.rc")
    if(EXISTS "${_OMNIFRAME_ROOT_DIR}/cmake/windows_app_icon.rc.in")
        configure_file(
            "${_OMNIFRAME_ROOT_DIR}/cmake/windows_app_icon.rc.in"
            "${OMNIFRAME_GENERATED_ICON_RC}"
            @ONLY
        )
    else()
        file(WRITE "${OMNIFRAME_GENERATED_ICON_RC}"
            "IDI_ICON1 ICON \"${OMNIFRAME_APP_ICON_RC_PATH}\"\n1 ICON \"${OMNIFRAME_APP_ICON_RC_PATH}\"\n"
        )
    endif()
endif()

function(_omniframe_copy_runtime_icons target_name)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target_name}>/assets/icons"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_OMNIFRAME_ICONS_SRC}/omniframe.ico"
            "$<TARGET_FILE_DIR:${target_name}>/assets/icons/omniframe.ico"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_OMNIFRAME_ICONS_SRC}/omniframe.png"
            "$<TARGET_FILE_DIR:${target_name}>/assets/icons/omniframe.png"
    )
endfunction()

function(_omniframe_deploy_tool target_name)
    if(WIN32 AND EXISTS "${_DEPLOY_SCRIPT}")
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                -DBINARY_PATH=$<TARGET_FILE:${target_name}>
                -DQT_BIN_DIR=${OMNIFRAME_QT_BIN_DIR}
                -DBUILD_CONFIG=$<CONFIG>
                -DCOPY_QT_TRANSLATIONS=OFF
                -P "${_DEPLOY_SCRIPT}"
            COMMENT "Executando deploy Qt6 para ${target_name}"
        )
    endif()
endfunction()

# ==============================================================================
# Build separado: build/Version -> VersionEditor.exe (GUI + CLI --check / --apply)
# ==============================================================================
if(BUILD_VERSION)
    set(_VERSION_SOURCES
        VersionEditor.hpp
        VersionEditor.cpp
    )
    if(WIN32)
        list(APPEND _VERSION_SOURCES "${OMNIFRAME_GENERATED_ICON_RC}")
    endif()

    # Subsistema Windows GUI (sem terminal preto) com suporte a CLI anexado ao terminal pai
    add_executable(VersionEditor WIN32 ${_VERSION_SOURCES})
    set_target_properties(VersionEditor PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        OUTPUT_NAME "VersionEditor"
    )
    target_include_directories(VersionEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(VersionEditor PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )
    if(COMMAND omniframe_apply_compiler_options)
        omniframe_apply_compiler_options(VersionEditor)
    endif()
    qt_add_resources(VersionEditor "version_editor_icons"
        PREFIX "/mocks"
        BASE "${_OMNIFRAME_ROOT_DIR}/source/assets"
        FILES
            "${_OMNIFRAME_ICONS_SRC}/omniframe.ico"
            "${_OMNIFRAME_ICONS_SRC}/omniframe.png"
    )
    _omniframe_copy_runtime_icons(VersionEditor)
    _omniframe_deploy_tool(VersionEditor)
endif()

# ==============================================================================
# Build separado: build/CompileLanguage -> CompileTranslationsGUI / Terminal
# ==============================================================================
if(BUILD_COMPILE_LANGUAGE)
    set(_COMPILE_GUI_SOURCES
        CompileTranslationsGUI.hpp
        CompileTranslationsGUI.cpp
    )
    set(_COMPILE_TERM_SOURCES
        CompileTranslationsTerminal.hpp
        CompileTranslationsTerminal.cpp
    )
    if(WIN32)
        list(APPEND _COMPILE_GUI_SOURCES "${OMNIFRAME_GENERATED_ICON_RC}")
        list(APPEND _COMPILE_TERM_SOURCES "${OMNIFRAME_GENERATED_ICON_RC}")
    endif()

    add_executable(CompileTranslationsGUI WIN32 ${_COMPILE_GUI_SOURCES})
    set_target_properties(CompileTranslationsGUI PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        OUTPUT_NAME "CompileTranslationsGUI"
    )
    target_include_directories(CompileTranslationsGUI PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(CompileTranslationsGUI PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )
    if(COMMAND omniframe_apply_compiler_options)
        omniframe_apply_compiler_options(CompileTranslationsGUI)
    endif()
    qt_add_resources(CompileTranslationsGUI "compile_translations_icons"
        PREFIX "/mocks"
        BASE "${_OMNIFRAME_ROOT_DIR}/source/assets"
        FILES
            "${_OMNIFRAME_ICONS_SRC}/omniframe.ico"
            "${_OMNIFRAME_ICONS_SRC}/omniframe.png"
    )

    add_executable(CompileTranslationsTerminal ${_COMPILE_TERM_SOURCES})
    set_target_properties(CompileTranslationsTerminal PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        OUTPUT_NAME "CompileTranslationsTerminal"
    )
    target_include_directories(CompileTranslationsTerminal PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(CompileTranslationsTerminal PRIVATE
        Qt6::Core
    )
    if(COMMAND omniframe_apply_compiler_options)
        omniframe_apply_compiler_options(CompileTranslationsTerminal)
    endif()

    _omniframe_copy_runtime_icons(CompileTranslationsGUI)

    if(WIN32 AND EXISTS "${_DEPLOY_SCRIPT}")
        add_custom_target(DeployCompileLanguage ALL
            DEPENDS CompileTranslationsGUI CompileTranslationsTerminal
            COMMAND "${CMAKE_COMMAND}"
                -DBINARY_PATH=$<TARGET_FILE:CompileTranslationsGUI>
                -DQT_BIN_DIR=${OMNIFRAME_QT_BIN_DIR}
                -DBUILD_CONFIG=$<CONFIG>
                -DCOPY_QT_TRANSLATIONS=OFF
                -P "${_DEPLOY_SCRIPT}"
            COMMAND "${CMAKE_COMMAND}"
                -DBINARY_PATH=$<TARGET_FILE:CompileTranslationsTerminal>
                -DQT_BIN_DIR=${OMNIFRAME_QT_BIN_DIR}
                -DBUILD_CONFIG=$<CONFIG>
                -DCOPY_QT_TRANSLATIONS=OFF
                -P "${_DEPLOY_SCRIPT}"
            COMMENT "Executando deploy Qt6 sequencial para CompileTranslationsGUI e CompileTranslationsTerminal"
        )
    endif()
endif()
