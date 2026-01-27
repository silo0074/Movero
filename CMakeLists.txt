cmake_minimum_required(VERSION 3.16)
project(Movero LANGUAGES CXX)
project(${PROJECT_NAME} VERSION 1.0.0)

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
    src/DetailsWindow.cpp
    src/MainWindow.ui
    src/Settings.ui
	resources.qrc
)

set(HEADERS
    src/MainWindow.h
    src/Settings.h
    src/Config.h
    src/CopyWorker.h
    src/DetailsWindow.h
)

set(TS_FILES 
    translations/Movero_ro.ts 
    translations/Movero_de.ts
)

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

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

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
# sudo make install (Installs on your system)
# cpack (Creates the .deb and .rpm): run inside debug or release folder
# cpack -G DEB 
# or
# cpack -G RPM
# Verify the content: 
# For DEB: dpkg -c Movero-1.0.0-Linux.deb
# For RPM: rpm -qpl Movero-1.0.0-Linux.rpm

# --- INSTALLATION RULES ---
# These tell CPack what files to put inside the .deb and .rpm
install(TARGETS ${PROJECT_NAME} DESTINATION bin) # Standardized path

# Ensure these files actually exist in the project folder before running cpack
# install(FILES ${PROJECT_NAME}.desktop DESTINATION share/applications)
install(FILES assets/dolphin_service_menu.desktop 
        DESTINATION share/kio/servicemenus 
        RENAME movero_copy.desktop)

# --- CPACK CONFIGURATION ---
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "MoveroDeveloper")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Fast file copier with xxHash verification")
set(CPACK_PACKAGE_CONTACT "your-email@example.com") # Required for DEB
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE") # Optional

# List dependencies in the package metadata
# For RPM (OpenSuse/Fedora)
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtbase, qt6-qtbase-gui, libxxhash0")

# For DEB (Ubuntu/Debian)
set(CPACK_DEB_PACKAGE_DEPENDS "libqt6core6, libqt6widgets6, libxxhash0")

# Script to run after uninstall that would do:
# Refreshing the system icon cache.
# Refreshing the desktop database so the menu entry disappears immediately.
# Removing specific log files or system-wide temporary files.
# For Debian/Ubuntu
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "assets/postrm")
# set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/packaging/postrm.sh")
# For RPM (openSUSE/Fedora)
set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "assets/postun")
# set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/postun")

# RPM Specific
set(CPACK_RPM_PACKAGE_LICENSE "GPLv3") # license
set(CPACK_RPM_PACKAGE_GROUP "Applications/System")

set(CPACK_GENERATOR "DEB;RPM")

# --- Post install script
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/postinst_rpm.sh")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postinst")

# CMake uninstall (cmake_uninstall.cmake.in)
# This file is a template that CMake uses to generate a custom uninstaller 
# for users who install via make install instead of using .rpm or .deb packages.
if(NOT TARGET uninstall)
    configure_file(
        "assets/cmake_uninstall.cmake.in"  # custom template
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"		# generated by CMake
        IMMEDIATE @ONLY)

    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()


# This MUST be the very last line of the CPack section
include(CPack)