cmake_minimum_required(VERSION 3.21)
project(NyaEdit VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Concurrent)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Concurrent Svg)

include(CTest)
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/core/CMakeLists.txt")
    message(FATAL_ERROR "Core submodule is missing. Run: git submodule update --init --recursive")
endif()
add_subdirectory(src/core)

# Project task orchestration belongs to the application, independently of the core submodule.
add_library(nya_tasks STATIC
    src/tasks/TaskConfiguration.cpp src/tasks/TaskConfiguration.h
    src/tasks/TaskRunner.cpp src/tasks/TaskRunner.h)
target_include_directories(nya_tasks PUBLIC src/tasks)
target_link_libraries(nya_tasks PUBLIC Qt${QT_VERSION_MAJOR}::Core)
if(MSVC)
    target_compile_options(nya_tasks PRIVATE /W4 /utf-8)
else()
    target_compile_options(nya_tasks PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(nya_widgets STATIC
    assets/icons.qrc
    src/ui/Animation.cpp src/ui/Animation.h
    src/ui/AnimationSettingsDialog.cpp src/ui/AnimationSettingsDialog.h
    src/ui/BackgroundSettingsDialog.cpp src/ui/BackgroundSettingsDialog.h
    src/ui/EditorBackground.cpp src/ui/EditorBackground.h
    src/ui/Editor.cpp src/ui/Editor.h
    src/ui/FileTreeView.cpp src/ui/FileTreeView.h
    src/ui/FontSettingsDialog.cpp src/ui/FontSettingsDialog.h
    src/ui/Highlighter.cpp src/ui/Highlighter.h
    src/ui/LanguageSettingsDialog.cpp src/ui/LanguageSettingsDialog.h
    src/ui/MainWindow.cpp src/ui/MainWindow.h
    src/ui/TaskPanel.cpp src/ui/TaskPanel.h
    src/ui/WelcomePage.cpp src/ui/WelcomePage.h
    src/ui/Theme.cpp src/ui/Theme.h)
target_include_directories(nya_widgets PUBLIC src/ui)
target_link_libraries(nya_widgets PUBLIC nya_core nya_tasks Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Concurrent Qt${QT_VERSION_MAJOR}::Svg)

add_executable(NyaEdit MACOSX_BUNDLE WIN32 src/main.cpp)
target_link_libraries(NyaEdit PRIVATE nya_widgets)
set_target_properties(NyaEdit PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME NyaEdit
    MACOSX_BUNDLE_GUI_IDENTIFIER dev.nyaedit.editor
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    WIN32_EXECUTABLE TRUE)
if(APPLE)
    set_source_files_properties(assets/logo.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(NyaEdit PRIVATE assets/logo.icns)
    set_target_properties(NyaEdit PROPERTIES MACOSX_BUNDLE_ICON_FILE logo.icns)
elseif(WIN32)
    enable_language(RC)
    configure_file(packaging/nyaedit.rc.in ${CMAKE_CURRENT_BINARY_DIR}/nyaedit.rc @ONLY)
    target_sources(NyaEdit PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/nyaedit.rc)
endif()
if(MSVC)
    target_compile_options(nya_widgets PRIVATE /W4 /utf-8)
    target_compile_options(NyaEdit PRIVATE /utf-8)
else()
    target_compile_options(nya_widgets PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
    add_executable(nya_task_tests tests/TaskTests.cpp)
    target_link_libraries(nya_task_tests PRIVATE nya_tasks Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME nya_task_tests COMMAND nya_task_tests)
    set_tests_properties(nya_task_tests PROPERTIES TIMEOUT 60)
    add_executable(nya_ui_tests tests/WidgetTests.cpp)
    target_link_libraries(nya_ui_tests PRIVATE nya_widgets Qt${QT_VERSION_MAJOR}::Test)
    add_dependencies(nya_ui_tests nya_tree_sitter_tests nya_task_tests)
    if(MSVC)
        target_compile_options(nya_ui_tests PRIVATE /utf-8)
        target_compile_options(nya_task_tests PRIVATE /utf-8)
    endif()
    add_test(NAME nya_ui_tests COMMAND nya_ui_tests -platform offscreen)
    set_tests_properties(nya_ui_tests PROPERTIES TIMEOUT 90)
endif()

option(NYA_BUILD_TOOLS "Build preview and performance measurement tools" OFF)
if(NYA_BUILD_TOOLS)
    add_executable(nya_preview tools/Preview.cpp)
    target_link_libraries(nya_preview PRIVATE nya_widgets)
    add_executable(nya_benchmark tools/Benchmark.cpp)
    target_link_libraries(nya_benchmark PRIVATE nya_widgets)
    if(APPLE)
        enable_language(OBJC)
        add_executable(nya_set_file_icon tools/SetFileIcon.m)
        set_target_properties(nya_set_file_icon PROPERTIES AUTOMOC OFF AUTORCC OFF)
        target_compile_options(nya_set_file_icon PRIVATE -fobjc-arc)
        target_link_libraries(nya_set_file_icon PRIVATE "-framework AppKit")
        add_custom_target(nya_refresh_file_icons
            COMMAND $<TARGET_FILE:nya_set_file_icon> ${PROJECT_SOURCE_DIR}/assets/logo.icns
                $<TARGET_FILE:NyaEdit> $<TARGET_BUNDLE_DIR:NyaEdit>
            DEPENDS NyaEdit nya_set_file_icon
            COMMENT "Updating local Finder icons for NyaEdit"
            VERBATIM)
    endif()
    if(MSVC)
        target_compile_options(nya_preview PRIVATE /utf-8)
        target_compile_options(nya_benchmark PRIVATE /utf-8)
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS NyaEdit BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE)
    install(FILES packaging/nyaedit.desktop DESTINATION share/applications)
    install(FILES assets/logo.svg DESTINATION share/icons/hicolor/scalable/apps RENAME nyaedit.svg)
endif()
