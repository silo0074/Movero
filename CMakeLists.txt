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
