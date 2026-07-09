cmake_minimum_required(VERSION 3.21)

project(PeekWarden VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent LinguistTools Network)

qt_standard_project_setup()

qt_add_executable(PeekWarden WIN32
    src/main.cpp
    src/AppSettings.cpp
    src/AppSettings.h
    src/BwClient.cpp
    src/BwClient.h
    src/I18n.cpp
    src/I18n.h
    src/QuickWindow.cpp
    src/QuickWindow.h
    src/SettingsDialog.cpp
    src/SettingsDialog.h
    src/TranslationCatalog.cpp
    src/WinHotkey.cpp
    src/WinHotkey.h
)

target_link_libraries(PeekWarden PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Network)

set(PEEKWARDEN_TRANSLATIONS
    translations/PeekWarden_ko.ts
)

qt_add_lrelease(PeekWarden
    TS_FILES ${PEEKWARDEN_TRANSLATIONS}
    QM_FILES_OUTPUT_VARIABLE PEEKWARDEN_QM_FILES
)

add_custom_target(PeekWarden_copy_translations ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PeekWarden>/translations"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PEEKWARDEN_QM_FILES} "$<TARGET_FILE_DIR:PeekWarden>/translations"
    DEPENDS ${PEEKWARDEN_QM_FILES}
    VERBATIM
)
add_dependencies(PeekWarden_copy_translations PeekWarden_lrelease)

if(WIN32)
    target_compile_definitions(PeekWarden PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(PeekWarden PRIVATE advapi32)
endif()

if(MSVC)
    target_compile_options(PeekWarden PRIVATE /utf-8)
endif()

install(TARGETS PeekWarden
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
