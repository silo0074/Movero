cmake_minimum_required(VERSION 3.16)
project(Movero LANGUAGES CXX)
project(${PROJECT_NAME} VERSION 1.0.2)
set(PROJECT_RELEASE 1) # package version

# Create a lowercase variable
string(TOLOWER ${PROJECT_NAME} PROJECT_NAME_LOWER)
# Result: PROJECT_NAME_LOWER = "movero"

# Pass the version to C++ as a macro
add_definitions(-DAPP_VERSION="${PROJECT_VERSION}")
add_definitions(-DAPP_NAME="${PROJECT_NAME}")

# Adds the flag to all targets in the project
add_compile_options(-fstack-protector-strong)

# Add it to the linker
add_link_options(-fstack-protector-strong)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON) # Automatically run MOC
set(CMAKE_AUTOUIC ON) # Automatically run UIC (if you use .ui files later)
set(CMAKE_AUTORCC ON) # Automatically run RCC (if you use .qrc files)

# Get project source directory
set(PROJECT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/silo0074/Movero")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus LinguistTools)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XXHASH REQUIRED libxxhash)

# It is safer to list headers and sources clearly
set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/Settings.cpp
    src/Config.cpp
    src/CopyWorker.cpp
    src/StartupHandler.cpp
    src/DetailsWindow.cpp
    src/MainWindow.ui
    src/Settings.ui
	src/LogHelper.cpp
	resources.qrc
)

set(HEADERS
    src/MainWindow.h
    src/Settings.h
    src/Config.h
    src/CopyWorker.h
    src/StartupHandler.h
    src/DetailsWindow.h
	src/LogHelper.h
)

set(TS_FILES 
    translations/Movero_ro.ts 
    translations/Movero_de.ts
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# --- This method doesn't generate the .ts files 
# but it is the recommended method in Qt6.

# This tells Qt which files to scan for translatable strings
# This command is the "all-in-one" for Qt6.
# It handles lupdate (scanning) and lrelease (compiling) automatically.
qt_add_translations(${PROJECT_NAME}
	TS_FILES ${TS_FILES}
	SOURCES ${SOURCES} ${HEADERS}  # Explicitly include HEADERS so Config.h is scanned
	LUPDATE_OPTIONS -extensions h,cpp,ui
	-no-obsolete  # This will delete the "vanished" strings
	RESOURCE_PREFIX "/translations" # Optional: sets the path inside :/. Default is "/i18n"
    # This tells Qt to put the .qm files in a specific place 
    # without confusing the resource compiler
)

# Older method from Qt5
# Generate .qm files in the source directory so resources.qrc can find them
# set_source_files_properties(${TS_FILES} PROPERTIES OUTPUT_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/translations")
# qt6_create_translation(QM_FILES ${CMAKE_CURRENT_SOURCE_DIR} ${TS_FILES})
# add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${QM_FILES})

if(CMAKE_BUILD_TYPE MATCHES "Debug")
    message(STATUS "Setting DEBUG_ENABLED to 1")
    target_compile_definitions(${PROJECT_NAME} PRIVATE DEBUG_ENABLED=1)
else()
    message(STATUS "Setting DEBUG_ENABLED to 0")
    target_compile_definitions(${PROJECT_NAME} PRIVATE DEBUG_ENABLED=0)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE 
    PROJECT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::DBus
    ${XXHASH_LIBRARIES}
)

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR} # helps find generated ui_* files
    ${XXHASH_INCLUDE_DIRS}
)


# --- CPACK
# To generate DEB on Opensuse install these
# sudo zypper install dpkg
# sudo zypper install dpkg-devel

# sudo make install (Installs on your system)
# cpack (Creates the .deb and .rpm): run inside debug or release folder
# cpack -G DEB 
# or
# cpack -G RPM

# Verify the content: 
# For DEB: dpkg -c Movero-1.0.0-Linux.deb
# For RPM: rpm -qpl Movero-1.0.0-Linux.rpm
# Verify the scripts
# rpm -qp --scripts Movero-1.0.0-Linux.rpm

# 1. Generate the packages
# cpack -G RPM && cpack -G DEB

# 2. Run the checksum script manually using the CMake interpreter
# cmake -P ../../packaging/generate_checksums.cmake

# --- INSTALLATION RULES ---
# These tell CPack what files to put inside the .deb and .rpm
# When you run cmake .., it sets CMAKE_INSTALL_PREFIX to /usr/local by default. 
# This puts the binary in /usr/local/bin.
# When CPack builds a DEB or RPM, it automatically overrides the prefix to /usr. 
# This puts the binary in /usr/bin.
install(TARGETS ${PROJECT_NAME} DESTINATION bin) # Standardized path

# Ensure these files actually exist in the project folder before running cpack
# Must match the name set in main.cpp
# app.setDesktopFileName(QString(APP_NAME));
install(FILES assets/${PROJECT_NAME}.desktop
		DESTINATION share/applications
		PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
)
install(FILES assets/${PROJECT_NAME}-settings.desktop 
		DESTINATION share/applications
		PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
)
install(FILES assets/dolphin-service-menu.desktop
        DESTINATION share/kio/servicemenus
		PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
        RENAME ${PROJECT_NAME}-paste.desktop
)
# Install the Nautilus extension only for DEB packages
if(CPACK_GENERATOR MATCHES "DEB")
    install(FILES "assets/nautilus_extension.py"
            DESTINATION "share/nautilus-python/extensions"
            RENAME ${PROJECT_NAME}-paste.py
    )
            
    # Also ensure the dependency is listed so the menu actually works
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}, python3-nautilus")
endif()

# install(FILES "icons/${PROJECT_NAME}-icon.png"
#         DESTINATION "share/icons/hicolor/48x48/apps"
#         RENAME "${PROJECT_NAME_LOWER}.png"
# )

# # Install to 256x256 (standard high-res baseline)
# install(FILES "icons/${PROJECT_NAME}-icon.png"
#         DESTINATION "share/icons/hicolor/256x256/apps"
#         RENAME "${PROJECT_NAME_LOWER}.png" # Uses "movero.png"
# )

# sudo zypper install ImageMagick  # openSUSE
# sudo apt install imagemagick      # Ubuntu
find_program(CONVERT_TOOL NAMES magick convert)

if(CONVERT_TOOL)
    set(ICON_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/icons/${PROJECT_NAME}-icon.png")
    set(SIZES 16 32 48 64 128 256)

    foreach(SIZE ${SIZES})
        set(OUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/movero_${SIZE}.png")
        
        # This command runs during 'make'
        add_custom_command(
            OUTPUT "${OUT_FILE}"
            COMMAND ${CONVERT_TOOL} "${ICON_SOURCE}" -resize ${SIZE}x${SIZE} "${OUT_FILE}"
            DEPENDS "${ICON_SOURCE}"
            COMMENT "Resizing icon to ${SIZE}x${SIZE}"
        )
        
        # Add to a list so we can install them later
        list(APPEND RESIZED_ICONS "${OUT_FILE}")

        # Install to the correct theme directory
        install(FILES "${OUT_FILE}"
                DESTINATION "share/icons/hicolor/${SIZE}x${SIZE}/apps"
                RENAME "${PROJECT_NAME_LOWER}.png")
    endforeach()

    # Create a target so these commands actually run
    add_custom_target(resize_icons ALL DEPENDS ${RESIZED_ICONS})
else()
    message(WARNING "ImageMagick not found. Icons will not be resized!")
endif()

install(FILES icons/${PROJECT_NAME}-icon.png
        DESTINATION "share/icons/hicolor/1024x1024/apps"
		RENAME "${PROJECT_NAME_LOWER}.png" # Uses "movero.png"
)

# Fallback: Install to scalable (ensures visibility across all UI scales)
install(FILES "icons/${PROJECT_NAME}-icon.png"
        DESTINATION "share/icons/hicolor/scalable/apps"
        RENAME "${PROJECT_NAME_LOWER}.png"
)

# Install changelog to the standard doc directory
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.spec"
        DESTINATION "share/doc/${PROJECT_NAME_LOWER}"
        RENAME "changelog"
)

# --- CPACK CONFIGURATION ---
set(CPACK_PACKAGE_VENDOR "Liviu Istrate")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A file transfer for Linux with checksum feature.")
set(CPACK_PACKAGE_CONTACT "github.com@silo0074") # Required for DEB
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE") # Optional
set(CPACK_RPM_CHANGELOG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.spec")

set(CPACK_RPM_PACKAGE_RELEASE ${PROJECT_RELEASE})

set(CPACK_DEBIAN_PACKAGE_RELEASE ${PROJECT_RELEASE})
set(CPACK_DEBIAN_PACKAGE_LICENSE "GPL-3.0")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
    set(CPACK_SYSTEM_NAME "x86_64")
else()
    set(CPACK_SYSTEM_NAME ${CMAKE_SYSTEM_PROCESSOR})
endif()

# set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}-${CPACK_SYSTEM_NAME}")
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}-${PROJECT_RELEASE}-${CPACK_SYSTEM_NAME}")
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_GENERATOR "DEB;RPM")

# --- Script to run after install/uninstall that would do:
# Refreshing the system icon cache.
# Refreshing the desktop database so the menu entry disappears immediately.
# Removing specific log files or system-wide temporary files.

# Paths to scripts
set(DEB_POSTINST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postinst")
set(DEB_POSTRM   "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postrm")
set(RPM_POSTINST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/postinst_rpm.sh")
set(RPM_POSTUN   "${CMAKE_CURRENT_SOURCE_DIR}/packaging/postun_rpm.sh")

# DEB Settings
# set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# Manually define the "safe" package names that exist in all versions
# Note: Ubuntu provides aliases so 'libqt6widgets6' will work even if 'libqt6widgets6t64' is the real file.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6, libqt6widgets6, libqt6gui6, libqt6core6, libqt6dbus6, libxxhash0")

set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${DEB_POSTINST};${DEB_POSTRM}")

# Standardize the DEB architecture name to match Debian's 'amd64'
if(CPACK_SYSTEM_NAME STREQUAL "x86_64")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
else()
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE ${CPACK_SYSTEM_NAME})
endif()

# RPM Settings
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${RPM_POSTINST}")
set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "${RPM_POSTUN}")
set(CPACK_RPM_PACKAGE_LICENSE "GPLv3")
set(CPACK_RPM_PACKAGE_GROUP "Applications/System")

if(CPACK_GENERATOR MATCHES "DEB")
    # Ubuntu/Debian standard location for copyright/license
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
            DESTINATION "share/doc/${PROJECT_NAME_LOWER}"
            RENAME "copyright")
endif()

# CMake uninstall (cmake_uninstall.cmake.in)
# This file is a template that CMake uses to generate a custom uninstaller 
# for users who install via make install instead of using .rpm or .deb packages.
if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        IMMEDIATE @ONLY)

    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")
endif()

# This MUST be the very last line of the CPack section
include(CPack)

