cmake_minimum_required(VERSION 3.16)

project(XGallery VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Sql Concurrent)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Sql Concurrent LinguistTools)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        database.h
        database.cpp
        scanner.h
        scanner.cpp
        busydialog.h
        busydialog.cpp
        imageviewer.h
        imageviewer.cpp
        gallerydialog.h
        gallerydialog.cpp
        imagemetadialog.h
        imagemetadialog.cpp
        tagpickerdialog.h
        tagpickerdialog.cpp
        migrations.h
        migrations.cpp
        mediafiles.h
        mediafiles.cpp
        duplicatesdialog.h
        duplicatesdialog.cpp
        imagerotation.h
        imagerotation.cpp
        resources.qrc
        i18n.h
        i18n.cpp
        languagedialog.h
        languagedialog.cpp
        virtualpath.h
        virtualpath.cpp
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(XGallery
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET XGallery APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(XGallery SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(XGallery
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(XGallery PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    Qt${QT_VERSION_MAJOR}::Concurrent
)

if(WIN32)
    # Program icon for the .exe (Explorer, taskbar)
    target_sources(XGallery PRIVATE xgallery.rc)
    # windres doesn't track the .ico it embeds
    set_source_files_properties(xgallery.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/xgallery.ico)
    # Explorer thumbnails for videos (mediafiles.cpp)
    target_link_libraries(XGallery PRIVATE ole32 shell32 uuid)
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.XGallery)
endif()
set_target_properties(XGallery PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS XGallery
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# UI translations (source strings are Ukrainian). Update the .ts files with lupdate, then fill in
# the translations; the .qm files are built and embedded under :/i18n automatically.
qt_add_translations(XGallery
    TS_FILES translations/xgallery_en.ts translations/xgallery_fr.ts
    RESOURCE_PREFIX /i18n
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(XGallery)
endif()
