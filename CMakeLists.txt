cmake_minimum_required(VERSION 3.20)

project(IphoneGallery VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

set(QT_SDK_DIR "C:/Qt/6.11.1/mingw_64" CACHE PATH "Qt SDK directory")
set(ELAWIDGETTOOLS_DIR "D:/github/ElaWidgetTools" CACHE PATH "ElaWidgetTools source directory")

list(APPEND CMAKE_PREFIX_PATH ${QT_SDK_DIR})

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent Multimedia MultimediaWidgets)

if (EXISTS "${ELAWIDGETTOOLS_DIR}/ElaWidgetTools/CMakeLists.txt")
    add_subdirectory("${ELAWIDGETTOOLS_DIR}/ElaWidgetTools" "${CMAKE_BINARY_DIR}/ElaWidgetTools")
else()
    find_package(ElaWidgetTools REQUIRED)
endif()

set(PROJECT_SOURCES
    main.cpp
    core/PhotoItem.h
    core/PhotoLibrary.h
    core/PhotoLibrary.cpp
    core/ExifReader.h
    core/ExifReader.cpp
    core/MediaUtils.h
    core/MediaUtils.cpp
    core/ThumbnailService.h
    core/ThumbnailService.cpp
    ui/LibraryPage.h
    ui/LibraryPage.cpp
    ui/PhotoGridView.h
    ui/PhotoGridView.cpp
    ui/PhotoViewer.h
    ui/PhotoViewer.cpp
    ui/MediaCanvas.h
    ui/MediaCanvas.cpp
    ui/VideoCanvas.h
    ui/VideoCanvas.cpp
    ui/MainWindow.h
    ui/MainWindow.cpp
)

add_executable(IphoneGallery WIN32 ${PROJECT_SOURCES})

target_include_directories(IphoneGallery PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
)

target_link_libraries(IphoneGallery PRIVATE
    Ela::WidgetTools
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Multimedia
    Qt6::MultimediaWidgets
)

if (WIN32)
    target_link_libraries(IphoneGallery PRIVATE D3D11 DXGI dwmapi)
endif()

add_custom_command(TARGET IphoneGallery POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:ElaWidgetTools>"
        "$<TARGET_FILE_DIR:IphoneGallery>"
    COMMENT "Copy ElaWidgetTools runtime"
)

find_program(FFMPEG_EXECUTABLE ffmpeg)
if (FFMPEG_EXECUTABLE)
    add_custom_command(TARGET IphoneGallery POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FFMPEG_EXECUTABLE}"
            "$<TARGET_FILE_DIR:IphoneGallery>/ffmpeg.exe"
        COMMENT "Copy ffmpeg runtime"
    )
endif()

find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_SDK_DIR}/bin")
if (WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET IphoneGallery POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            --no-translations
            --no-system-d3d-compiler
            --no-opengl-sw
            --no-quick-import
            "$<TARGET_FILE:IphoneGallery>"
        COMMENT "Deploy Qt runtime"
    )
endif()
